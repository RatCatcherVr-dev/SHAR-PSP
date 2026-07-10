//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: system.cpp  (PSP HAL)
//
// Description: sceAudio-backed radsound HAL for the PSP.  The PSP exposes no
//              per-voice hardware mixing, so this is a small software mixer:
//              pspaudiolib runs the sceAudio output thread (correct priority +
//              double-buffering) and calls rsdPspAudioCallback to fill each
//              44.1kHz stereo block; the callback walks the voice list and sums
//              every playing voice (voice.cpp::MixInto) into an int32 accumulator
//              then clamps to int16.  (An earlier hand-rolled thread +
//              sceAudioSRCOutputBlocking sounded "farty" on real hardware --
//              single-buffer DMA overwrite + audio-thread starvation under the
//              render load; pspaudiolib handles both correctly.)
//
//=============================================================================

#include <string.h>

#include <pspkernel.h>
#include <pspthreadman.h>
#include <pspaudiolib.h>

#include <radplatform.hpp>
#include <radtime.hpp>

#include "system.hpp"
#include "buffer.hpp"
#include "voice.hpp"
#include "radsoundpsp.hpp"
#include "../common/banner.hpp"
#include "../common/memoryregion.hpp"
#include "../common/softwarelistener.hpp"
#include "../../common/radsoundupdatableobject.hpp"

//============================================================================
// Static / file-scope state
//============================================================================

radSoundHalSystemPsp * radSoundHalSystemPsp::s_pRsdSystem = NULL;

// Cross-component lock (see radsoundpsp.hpp).  A binary semaphore used as a
// mutex; created lazily so voices/buffers built before the system are safe.
static SceUID s_SoundLockSema = 0;

// int32 mix accumulator (touched only by the pspaudiolib callback thread).
static int s_AccumBlock[ PSP_NUM_AUDIO_SAMPLES * RSD_PSP_OUTPUT_CHANNELS ];

//============================================================================
// ::radSoundPspLock / ::radSoundPspUnlock
//============================================================================

void radSoundPspLock( void )
{
    if ( s_SoundLockSema != 0 )
    {
        sceKernelWaitSema( s_SoundLockSema, 1, NULL );
    }
}

void radSoundPspUnlock( void )
{
    if ( s_SoundLockSema != 0 )
    {
        sceKernelSignalSema( s_SoundLockSema, 1 );
    }
}

//============================================================================
// pspaudiolib fill callback (runs on pspaudiolib's channel-0 output thread)
//============================================================================

static void rsdPspAudioCallback( void * buf, unsigned int reqn, void * pdata )
{
    (void) pdata;

    short * out = (short *) buf;
    unsigned int frames = reqn;
    if ( frames > PSP_NUM_AUDIO_SAMPLES )
    {
        frames = PSP_NUM_AUDIO_SAMPLES;
    }
    unsigned int samples = frames * RSD_PSP_OUTPUT_CHANNELS;

    ::memset( s_AccumBlock, 0, samples * sizeof( int ) );

    // Sum every playing voice.  The lock guards the voice list + per-voice state
    // against the main thread (Play/Stop/SetBuffer/ctor/dtor).
    radSoundPspLock( );

    radSoundHalVoicePsp * pVoice = radSoundHalVoicePsp::GetLinkedClassHead( );
    while ( pVoice != NULL )
    {
        pVoice->MixInto( s_AccumBlock, frames );
        pVoice = pVoice->GetLinkedClassNext( );
    }

    radSoundPspUnlock( );

    // Clamp int32 accumulator down to int16.
    for ( unsigned int i = 0; i < samples; i++ )
    {
        int s = s_AccumBlock[ i ];
        if ( s >  32767 ) s =  32767;
        if ( s < -32768 ) s = -32768;
        out[ i ] = (short) s;
    }
}

//============================================================================
// radSoundHalSystemPsp::radSoundHalSystemPsp
//============================================================================

radSoundHalSystemPsp::radSoundHalSystemPsp( radMemoryAllocator allocator )
    :
    m_pSoundMemory( NULL ),
    m_NumAuxSends( 0 ),
    m_LastServiceTime( ::radTimeGetMilliseconds( ) ),
    m_AudioInited( false )
{
    s_pRsdSystem = this;

    for ( unsigned int i = 0; i < RSD_PSP_MAX_AUX_SENDS; i++ )
    {
        m_refIRadSoundHalEffect[ i ] = NULL;
    }

    if ( s_SoundLockSema == 0 )
    {
        s_SoundLockSema = sceKernelCreateSema( "radSoundPspLock", 0, 1, 1, NULL );
    }

    ::radSoundPrintBanner( );
}

//============================================================================
// radSoundHalSystemPsp::~radSoundHalSystemPsp
//============================================================================

radSoundHalSystemPsp::~radSoundHalSystemPsp( void )
{
    // Stop pspaudiolib calling us, then shut its output threads down.
    if ( m_AudioInited )
    {
        pspAudioSetChannelCallback( 0, NULL, NULL );
        pspAudioEnd( );
        m_AudioInited = false;
    }

    radSoundHalListener::Terminate( );
    radSoundHalMemoryRegion::Terminate( );

    if ( m_pSoundMemory != NULL )
    {
        ::radMemoryFreeAligned( GetThisAllocator( ), m_pSoundMemory );
        m_pSoundMemory = NULL;
    }

    if ( s_SoundLockSema != 0 )
    {
        sceKernelDeleteSema( s_SoundLockSema );
        s_SoundLockSema = 0;
    }

    s_pRsdSystem = NULL;
}

//============================================================================
// radSoundHalSystemPsp::Initialize
//============================================================================

void radSoundHalSystemPsp::Initialize( const SystemDescription & systemDescription )
{
    m_NumAuxSends = 0; // No effects bus on the PSP mixer.

    // Reserve the sound-memory pool (buffers are carved from this region).
    m_pSoundMemory = ::radMemoryAllocAligned(
        GetThisAllocator( ),
        systemDescription.m_ReservedSoundMemory,
        radSoundHalDataSourceReadAlignmentGet( ) );

    rAssert( m_pSoundMemory != NULL );

    radSoundHalMemoryRegion::Initialize(
        m_pSoundMemory,
        systemDescription.m_ReservedSoundMemory,
        systemDescription.m_MaxRootAllocations,
        radSoundHalDataSourceReadAlignmentGet( ),
        radMemorySpace_Local,
        GetThisAllocator( ) );

    radSoundHalListener::Initialize( GetThisAllocator( ) );

    // pspaudiolib owns the sceAudio output thread(s) + double-buffering; we just
    // register a fill callback on channel 0 (44.1kHz interleaved stereo).
    if ( pspAudioInit( ) == 0 )
    {
        pspAudioSetChannelCallback( 0, rsdPspAudioCallback, NULL );
        m_AudioInited = true;
    }
    else
    {
        rAssertMsg( false, "radsound PSP: pspAudioInit failed" );
    }
}

//============================================================================
// radSoundHalSystemPsp::GetRootMemoryRegion
//============================================================================

IRadSoundHalMemoryRegion * radSoundHalSystemPsp::GetRootMemoryRegion( void )
{
    return radSoundHalMemoryRegion::GetRootRegion( );
}

unsigned int radSoundHalSystemPsp::GetNumAuxSends( void )
{
    return m_NumAuxSends;
}

//============================================================================
// Output mode -- PSP is stereo only.
//============================================================================

void radSoundHalSystemPsp::SetOutputMode( radSoundOutputMode mode )
{
}

radSoundOutputMode radSoundHalSystemPsp::GetOutputMode( void )
{
    return radSoundOutputMode_Stereo;
}

//============================================================================
// radSoundHalSystemPsp::Service / ServiceOncePerFrame
//============================================================================

void radSoundHalSystemPsp::Service( void )
{
    unsigned int now = ::radTimeGetMilliseconds( );

    // Drives streamplayer refills etc. (radSoundUpdatableObject subscribers).
    radSoundUpdatableObject::UpdateAll( now - m_LastServiceTime );

    m_LastServiceTime = now;
}

void radSoundHalSystemPsp::ServiceOncePerFrame( void )
{
    radSoundHalListener::GetInstance( )->UpdatePositionalSettings( );
}

//============================================================================
// radSoundHalSystemPsp::GetStats
//============================================================================

void radSoundHalSystemPsp::GetStats( IRadSoundHalSystem::Stats * pStats )
{
    rAssert( pStats );

    ::memset( pStats, 0, sizeof( IRadSoundHalSystem::Stats ) );

    radSoundHalVoicePsp * pVoice = radSoundHalVoicePsp::GetLinkedClassHead( );
    while ( pVoice != NULL )
    {
        if ( pVoice->GetPositionalGroup( ) != NULL )
        {
            pStats->m_NumPosVoices++;
            if ( pVoice->IsPlaying( ) )
            {
                pStats->m_NumPosVoicesPlaying++;
            }
        }
        else
        {
            pStats->m_NumVoices++;
            if ( pVoice->IsPlaying( ) )
            {
                pStats->m_NumVoicesPlaying++;
            }
        }
        pVoice = pVoice->GetLinkedClassNext( );
    }

    radSoundHalBufferPsp * pBuffer = radSoundHalBufferPsp::GetLinkedClassHead( );
    while ( pBuffer != NULL )
    {
        pStats->m_NumBuffers++;
        pStats->m_BufferMemoryUsed += pBuffer->GetSizeInBytes( );
        pBuffer = pBuffer->GetLinkedClassNext( );
    }

    pStats->m_EffectsMemoryUsed = 0;

    radSoundHalMemoryRegion::GetRootRegion( )->GetStats(
        &pStats->m_TotalFreeSoundMemory, NULL, NULL, true );
}

//============================================================================
// Aux effects -- kept as refs for API parity; the PSP mixer applies none.
//============================================================================

void radSoundHalSystemPsp::SetAuxEffect( unsigned int auxNumber, IRadSoundHalEffect * pIRadSoundHalEffect )
{
    if ( auxNumber < RSD_PSP_MAX_AUX_SENDS )
    {
        m_refIRadSoundHalEffect[ auxNumber ] = pIRadSoundHalEffect;
    }
}

IRadSoundHalEffect * radSoundHalSystemPsp::GetAuxEffect( unsigned int auxNumber )
{
    if ( auxNumber < RSD_PSP_MAX_AUX_SENDS )
    {
        return m_refIRadSoundHalEffect[ auxNumber ];
    }
    return NULL;
}

void radSoundHalSystemPsp::SetAuxGain( unsigned int aux, float gain )
{
}

float radSoundHalSystemPsp::GetAuxGain( unsigned int aux )
{
    return 0.0f;
}

//============================================================================
// radSoundHalSystemPsp::GetInstance
//============================================================================

radSoundHalSystemPsp * radSoundHalSystemPsp::GetInstance( void )
{
    return s_pRsdSystem;
}

//============================================================================
// Factory functions (public radsound_hal interface)
//============================================================================

IRadSoundHalSystem * radSoundHalSystemGet( void )
{
    rAssert( radSoundHalSystemPsp::s_pRsdSystem != NULL );
    return radSoundHalSystemPsp::s_pRsdSystem;
}

void radSoundHalSystemInitialize( radMemoryAllocator allocator )
{
    rAssert( radSoundHalSystemPsp::s_pRsdSystem == NULL );

    new( "radSoundHalSystemPsp", allocator ) radSoundHalSystemPsp( allocator );
    radSoundHalSystemPsp::s_pRsdSystem->AddRef( );
}

void radSoundHalSystemTerminate( void )
{
    rAssert( radSoundHalSystemPsp::s_pRsdSystem != NULL );
    radSoundHalSystemPsp::s_pRsdSystem->Release( );
}
