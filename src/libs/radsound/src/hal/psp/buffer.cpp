//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: buffer.cpp  (PSP HAL)
//
//=============================================================================

#include <string.h>

#include "buffer.hpp"
#include "bufferloader.hpp"
#include "radsoundpsp.hpp"

//============================================================================
// Static Initialization
//============================================================================

template<> radSoundHalBufferPsp * radLinkedClass<radSoundHalBufferPsp>::s_pLinkedClassHead = NULL;
template<> radSoundHalBufferPsp * radLinkedClass<radSoundHalBufferPsp>::s_pLinkedClassTail = NULL;

//========================================================================
// radSoundHalBufferPsp::radSoundHalBufferPsp
//========================================================================

radSoundHalBufferPsp::radSoundHalBufferPsp( void )
    :
    m_SizeInFrames( 0 ),
    m_pLockedLoadBuffer( NULL ),
    m_Looping( false ),
    m_Streaming( false ),
    m_refIRadSoundHalAudioFormat( NULL ),
    m_refIRadMemoryObject( NULL ),
    m_refIRadSoundHalBufferLoadCallback( NULL )
{
}

//========================================================================
// radSoundHalBufferPsp::~radSoundHalBufferPsp
//========================================================================

radSoundHalBufferPsp::~radSoundHalBufferPsp( void )
{
}

//========================================================================
// radSoundHalBufferPsp::Initialize
//========================================================================

void radSoundHalBufferPsp::Initialize
(
    IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
    IRadMemoryObject * pIRadMemoryObject,
    unsigned int sizeInFrames,
    bool looping,
    bool streaming
)
{
    rAssert( pIRadSoundHalAudioFormat != NULL );
    rAssert( pIRadSoundHalAudioFormat->GetEncoding() == IRadSoundHalAudioFormat::PCM );
    rAssert( pIRadMemoryObject->GetMemorySize( ) >= ::radSoundHalBufferCalculateMemorySize(
        IRadSoundHalAudioFormat::Bytes, sizeInFrames,
        IRadSoundHalAudioFormat::Frames, pIRadSoundHalAudioFormat ) );

    m_refIRadSoundHalAudioFormat = pIRadSoundHalAudioFormat;
    m_refIRadMemoryObject = pIRadMemoryObject;
    m_Looping = looping;
    m_Streaming = streaming;
    m_SizeInFrames = sizeInFrames;
}

//========================================================================
// radSoundHalBufferPsp::GetFormat / GetMemoryObject / IsLooping / sizes
//========================================================================

IRadSoundHalAudioFormat * radSoundHalBufferPsp::GetFormat( void )
{
    return m_refIRadSoundHalAudioFormat;
}

IRadMemoryObject * radSoundHalBufferPsp::GetMemoryObject( void )
{
    return m_refIRadMemoryObject;
}

bool radSoundHalBufferPsp::IsLooping( void )
{
    return m_Looping;
}

bool radSoundHalBufferPsp::IsStreaming( void )
{
    return m_Streaming;
}

unsigned int radSoundHalBufferPsp::GetSizeInFrames( void )
{
    return m_SizeInFrames;
}

unsigned int radSoundHalBufferPsp::GetSizeInBytes( void )
{
    return m_refIRadSoundHalAudioFormat->FramesToBytes( m_SizeInFrames );
}

const unsigned char * radSoundHalBufferPsp::GetSampleData( void )
{
    if ( m_refIRadMemoryObject == NULL )
    {
        return NULL;
    }

    return static_cast< const unsigned char * >( m_refIRadMemoryObject->GetMemoryAddress( ) );
}

//========================================================================
// radSoundHalBufferPsp::ClearAsync
//========================================================================

void radSoundHalBufferPsp::ClearAsync
(
    unsigned int startPositionInFrames,
    unsigned int numberOfFrames,
    IRadSoundHalBufferClearCallback * pIRadSoundHalBufferClearCallback
)
{
    if ( m_refIRadMemoryObject != NULL )
    {
        rAssert( startPositionInFrames < m_SizeInFrames );
        rAssert( ( startPositionInFrames + numberOfFrames ) <= m_SizeInFrames );

        unsigned int offsetInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( startPositionInFrames );
        unsigned int sizeInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( numberOfFrames );
        unsigned char fillChar = ( m_refIRadSoundHalAudioFormat->GetBitResolution( ) == 8 ) ? 128 : 0;

        // The mixer may be reading these bytes on the output thread.
        radSoundPspLock( );
        ::memset( static_cast<char*>( m_refIRadMemoryObject->GetMemoryAddress( ) ) + offsetInBytes,
                  fillChar, sizeInBytes );
        radSoundPspUnlock( );
    }

    if ( pIRadSoundHalBufferClearCallback != NULL )
    {
        pIRadSoundHalBufferClearCallback->OnBufferClearComplete( );
    }
}

//========================================================================
// radSoundHalBufferPsp::LoadAsync
//
// The destination is the memory object itself (no separate HW copy).  We hand
// it to a buffer loader which pulls frames from the datasource asynchronously
// and calls us back at OnBufferLoadComplete.
//========================================================================

void radSoundHalBufferPsp::LoadAsync
(
    IRadSoundHalDataSource * pIRadSoundHalDataSource,
    unsigned int startPositionInFrames,
    unsigned int numberOfFrames,
    IRadSoundHalBufferLoadCallback * pIRadSoundHalBufferLoadCallback
)
{
    rAssert( m_refIRadSoundHalBufferLoadCallback == NULL );

    unsigned int loadStartInBytes = m_refIRadSoundHalAudioFormat->FramesToBytes( startPositionInFrames );

    m_refIRadSoundHalBufferLoadCallback = pIRadSoundHalBufferLoadCallback;
    m_pLockedLoadBuffer = static_cast<char*>( m_refIRadMemoryObject->GetMemoryAddress( ) ) + loadStartInBytes;

    new( "radSoundBufferLoaderPsp", RADMEMORY_ALLOC_TEMP ) radSoundBufferLoaderPsp(
        static_cast< IRadSoundHalBuffer * >( this ),
        m_pLockedLoadBuffer,
        pIRadSoundHalDataSource,
        m_refIRadSoundHalAudioFormat,
        numberOfFrames,
        this );
}

//========================================================================
// radSoundHalBufferPsp::OnBufferLoadComplete
//
// Data is already in the memory object; nothing to upload on PSP.  Just relay
// completion to the client.  Bracket the swap so the mixer never sees a torn
// pointer.
//========================================================================

void radSoundHalBufferPsp::OnBufferLoadComplete( unsigned int dataSourceFrames )
{
    radSoundPspLock( );
    m_pLockedLoadBuffer = NULL;
    radRef< IRadSoundHalBufferLoadCallback > callback = m_refIRadSoundHalBufferLoadCallback;
    m_refIRadSoundHalBufferLoadCallback = NULL;
    radSoundPspUnlock( );

    if ( callback != NULL )
    {
        callback->OnBufferLoadComplete( dataSourceFrames );
    }
}

//========================================================================
// radSoundHalBufferPsp::CancelAsyncOperations
//========================================================================

void radSoundHalBufferPsp::CancelAsyncOperations( void )
{
    radSoundBufferLoaderPsp::CancelOperations( static_cast< IRadSoundHalBuffer * >( this ) );

    radSoundPspLock( );
    m_pLockedLoadBuffer = NULL;
    m_refIRadSoundHalBufferLoadCallback = NULL;
    radSoundPspUnlock( );
}

//========================================================================
// radSoundHalBufferPsp::GetMinTransferSize
//========================================================================

unsigned int radSoundHalBufferPsp::GetMinTransferSize( IRadSoundHalAudioFormat::SizeType sizeType )
{
    rAssert( m_refIRadSoundHalAudioFormat != NULL );

    return m_refIRadSoundHalAudioFormat->ConvertSizeType( sizeType,
        radMemorySpace_OptimalMultiple * m_refIRadSoundHalAudioFormat->GetNumberOfChannels( ),
        IRadSoundHalAudioFormat::Bytes );
}

//========================================================================
// ::radSoundHalBufferCreate
//========================================================================

IRadSoundHalBuffer * radSoundHalBufferCreate( radMemoryAllocator allocator )
{
    return new ( "radSoundHalBufferPsp", allocator ) radSoundHalBufferPsp( );
}
