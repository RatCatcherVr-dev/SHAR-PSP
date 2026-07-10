//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: buffer.hpp  (PSP HAL)
//
//=============================================================================

#ifndef PSP_BUFFER_HPP
#define PSP_BUFFER_HPP

#include <radlinkedclass.hpp>
#include <radsoundobject.hpp>
#include <radsound_hal.hpp>

//============================================================================
// Component: radSoundHalBufferPsp
//
// Holds raw PCM in a main-memory IRadMemoryObject.  There is no separate
// hardware copy on PSP: the mixer (voice.cpp) reads these bytes directly, so
// Load/Clear just fill the memory object in place.
//============================================================================

class radSoundHalBufferPsp
    :
    public IRadSoundHalBuffer,
    public IRadSoundHalBufferLoadCallback,
    public radLinkedClass< radSoundHalBufferPsp >,
    public radSoundObject
{
    public:

        radSoundHalBufferPsp( void );

        IMPLEMENT_REFCOUNTED( "radSoundHalBufferPsp" )

        // IRadSoundHalBuffer

        virtual void Initialize(
            IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
            IRadMemoryObject * pIRadMemoryObject,
            unsigned int sizeInFrames,
            bool looping,
            bool streaming );

        virtual IRadSoundHalAudioFormat * GetFormat( void );
        virtual IRadMemoryObject * GetMemoryObject( void );
        virtual bool IsLooping( void );
        virtual unsigned int GetSizeInFrames( void );

        virtual void LoadAsync(
            IRadSoundHalDataSource * pIRadSoundHalDataSource,
            unsigned int startPositionInFrames,
            unsigned int numberOfFrames,
            IRadSoundHalBufferLoadCallback * pIRadSoundHalBufferLoadCallback );

        virtual void ClearAsync(
            unsigned int startPositionInFrames,
            unsigned int numberOfFrames,
            IRadSoundHalBufferClearCallback * pIRadSoundHalBufferClearCallback );

        virtual void CancelAsyncOperations( void );

        virtual unsigned int GetMinTransferSize( IRadSoundHalAudioFormat::SizeType sizeType );

        virtual void ReSetAudioFormat( IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat ) { }

        // IRadSoundHalBufferLoadCallback

        virtual void OnBufferLoadComplete( unsigned int dataSourceFrames );

        // Internal (mixer + stats)

        unsigned int GetSizeInBytes( void );
        bool IsStreaming( void );
        const unsigned char * GetSampleData( void );

    private:

        virtual ~radSoundHalBufferPsp( void );

        unsigned int m_SizeInFrames;
        void *       m_pLockedLoadBuffer;
        bool         m_Looping;
        bool         m_Streaming;

        radRef< IRadSoundHalAudioFormat >        m_refIRadSoundHalAudioFormat;
        radRef< IRadMemoryObject >               m_refIRadMemoryObject;
        radRef< IRadSoundHalBufferLoadCallback > m_refIRadSoundHalBufferLoadCallback;
};

#endif // PSP_BUFFER_HPP
