//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: bufferloader.hpp  (PSP HAL)
//
// Description: Serializes async datasource reads into a buffer's memory.  This
//              is a straight port of the Win32 loader (which had no AL-specific
//              code) -- it simply drives IRadSoundHalDataSource::GetFramesAsync.
//
//=============================================================================

#ifndef PSP_BUFFERLOADER_HPP
#define PSP_BUFFERLOADER_HPP

#include <radfile.hpp>
#include <radlinkedclass.hpp>
#include <radsound_hal.hpp>
#include <radsoundobject.hpp>

class radSoundBufferLoaderPsp
    :
    public IRadSoundHalDataSourceCallback,
    public radLinkedClass< radSoundBufferLoaderPsp >,
    public radSoundObject
{
    public:

        IMPLEMENT_REFCOUNTED( "radSoundBufferLoaderPsp" )

        radSoundBufferLoaderPsp(
            IRefCount * pIRefCount_Owner,
            void * pBuffer,
            IRadSoundHalDataSource * pIRadSoundHalDataSource,
            IRadSoundHalAudioFormat * pIRadSoundHalAudioFormat,
            unsigned int numberOfFrames,
            IRadSoundHalBufferLoadCallback * pISoundBufferCallback );

        virtual void OnDataSourceFramesLoaded( unsigned int framesActuallyRead );

        static void CancelOperations( IRefCount * pIRefCount_Owner );

        void Cancel( void );

    private:

        void Start( void );
        void Finish( void );

        radRef< IRadSoundHalBufferLoadCallback > m_xIRadSoundHalBufferLoadCallback;
        radRef< IRadSoundHalDataSource >         m_xIRadSoundHalDataSource;
        radRef< IRefCount >                      m_xIRefCount_Owner;

        unsigned int m_NumberOfFrames;
        void *       m_pBuffer;
        bool         m_Cancelled;
};

#endif // PSP_BUFFERLOADER_HPP
