//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: bufferloader.cpp  (PSP HAL) -- port of the Win32 loader.
//
//=============================================================================

#include <string.h>

#include "bufferloader.hpp"

template<> radSoundBufferLoaderPsp * radLinkedClass< radSoundBufferLoaderPsp >::s_pLinkedClassHead = NULL;
template<> radSoundBufferLoaderPsp * radLinkedClass< radSoundBufferLoaderPsp >::s_pLinkedClassTail = NULL;

//=========================================================================
// radSoundBufferLoaderPsp::radSoundBufferLoaderPsp
//=========================================================================

radSoundBufferLoaderPsp::radSoundBufferLoaderPsp
(
    IRefCount * pIRefCount_Owner,
    void * pBuffer,
    IRadSoundHalDataSource * pIRadSoundHalDataSource,
    IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
    unsigned int numberOfFrames,
    IRadSoundHalBufferLoadCallback * pISoundBufferCallback
)
    :
    m_xIRadSoundHalBufferLoadCallback( pISoundBufferCallback ),
    m_xIRadSoundHalDataSource( pIRadSoundHalDataSource ),
    m_xIRefCount_Owner( pIRefCount_Owner ),
    m_NumberOfFrames( numberOfFrames ),
    m_pBuffer( pBuffer ),
    m_Cancelled( false )
{
    rAssert( m_xIRadSoundHalDataSource != NULL );

    AddRef( );

    // Only one load runs at a time; if we are at the head, kick it off.
    if ( GetLinkedClassHead( ) == this )
    {
        Start( );
    }
}

void radSoundBufferLoaderPsp::Finish( void )
{
    if ( GetLinkedClassNext( ) )
    {
        GetLinkedClassNext( )->Start( );
    }

    Release( );
}

void radSoundBufferLoaderPsp::Start( void )
{
    if ( m_Cancelled == true )
    {
        Finish( );
    }
    else
    {
        rAssert( m_xIRadSoundHalDataSource != NULL );
        rAssert( m_xIRadSoundHalBufferLoadCallback != NULL );

        m_xIRadSoundHalDataSource->GetFramesAsync(
            ( char * ) m_pBuffer,
            radMemorySpace_Local,
            m_NumberOfFrames,
            this );
    }
}

//=========================================================================
// radSoundBufferLoaderPsp::OnDataSourceFramesLoaded
//=========================================================================

void radSoundBufferLoaderPsp::OnDataSourceFramesLoaded( unsigned int framesActuallyRead )
{
    if ( framesActuallyRead < m_NumberOfFrames )
    {
        // Datasource ran short -- pad the rest of the request with silence.
        unsigned int offsetInBytes = m_xIRadSoundHalDataSource->GetFormat( )->FramesToBytes( framesActuallyRead );
        unsigned int sizeInBytes = m_xIRadSoundHalDataSource->GetFormat( )->FramesToBytes( m_NumberOfFrames - framesActuallyRead );
        unsigned char fillChar = ( m_xIRadSoundHalDataSource->GetFormat( )->GetBitResolution( ) == 8 ) ? 128 : 0;

        ::memset( (char*) m_pBuffer + offsetInBytes, fillChar, sizeInBytes );
    }

    m_pBuffer = NULL;
    m_NumberOfFrames = 0;

    if ( m_Cancelled == false )
    {
        m_xIRadSoundHalBufferLoadCallback->OnBufferLoadComplete( framesActuallyRead );
    }

    Finish( );
}

void radSoundBufferLoaderPsp::Cancel( void )
{
    m_Cancelled = true;
}

void radSoundBufferLoaderPsp::CancelOperations( IRefCount * pIRefCount_Owner )
{
    radSoundBufferLoaderPsp * pSearch = radSoundBufferLoaderPsp::GetLinkedClassHead( );

    while ( pSearch != NULL )
    {
        if ( pSearch->m_xIRefCount_Owner == pIRefCount_Owner )
        {
            pSearch->Cancel( );
        }
        pSearch = pSearch->GetLinkedClassNext( );
    }
}
