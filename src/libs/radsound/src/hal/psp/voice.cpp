//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: voice.cpp  (PSP HAL)
//
//=============================================================================

#include <math.h>

#include <radsound.hpp>
#include <radsoundmath.hpp>

#include "voice.hpp"
#include "radsoundpsp.hpp"

//============================================================================
// Static Initialization
//============================================================================

template<> radSoundHalVoicePsp * radLinkedClass<radSoundHalVoicePsp>::s_pLinkedClassHead = NULL;
template<> radSoundHalVoicePsp * radLinkedClass<radSoundHalVoicePsp>::s_pLinkedClassTail = NULL;

//========================================================================
// radSoundHalVoicePsp::radSoundHalVoicePsp
//========================================================================

radSoundHalVoicePsp::radSoundHalVoicePsp( void )
    :
    m_Priority( 5 ),
    m_Pitch( 1.0f ),
    m_Volume( 1.0f ),
    m_Trim( 1.0f ),
    m_MuteFactor( 1.0f ),
    m_Pan( 0.0f ),
    m_PosVolume( 1.0f ),
    m_PosPan( 0.0f ),
    m_PosPitch( 1.0f ),
    m_Playing( false ),
    m_CursorFrames( 0.0 ),
    m_xRadSoundHalBufferPsp( NULL ),
    m_xIRadSoundHalAudioFormat( NULL ),
    m_xRadSoundHalPositionalGroup( NULL )
{
}

//========================================================================
// radSoundHalVoicePsp::~radSoundHalVoicePsp
//========================================================================

radSoundHalVoicePsp::~radSoundHalVoicePsp( void )
{
    // Take the voice off the mixer before we (and the linked-list node) go away.
    radSoundPspLock( );
    m_Playing = false;
    radSoundPspUnlock( );

    if ( m_xRadSoundHalPositionalGroup != NULL )
    {
        m_xRadSoundHalPositionalGroup->RemovePositionalEntity( this );
    }
}

//========================================================================
// Priority
//========================================================================

void radSoundHalVoicePsp::SetPriority( unsigned int priority )
{
    m_Priority = priority;
}

unsigned int radSoundHalVoicePsp::GetPriority( void )
{
    return m_Priority;
}

//========================================================================
// radSoundHalVoicePsp::SetBuffer
//========================================================================

void radSoundHalVoicePsp::SetBuffer( IRadSoundHalBuffer * pIRadSoundHalBuffer )
{
    radSoundPspLock( );

    m_Playing = false;
    m_CursorFrames = 0.0;
    m_xRadSoundHalBufferPsp = NULL;
    m_xIRadSoundHalAudioFormat = NULL;

    if ( pIRadSoundHalBuffer != NULL )
    {
        m_xRadSoundHalBufferPsp = static_cast< radSoundHalBufferPsp * >( pIRadSoundHalBuffer );
        m_xIRadSoundHalAudioFormat = m_xRadSoundHalBufferPsp->GetFormat( );
    }

    radSoundPspUnlock( );
}

IRadSoundHalBuffer * radSoundHalVoicePsp::GetBuffer( void )
{
    return m_xRadSoundHalBufferPsp;
}

//========================================================================
// Transport
//========================================================================

void radSoundHalVoicePsp::Play( void )
{
    radSoundPspLock( );
    if ( m_xRadSoundHalBufferPsp != NULL )
    {
        m_Playing = true;
    }
    radSoundPspUnlock( );
}

void radSoundHalVoicePsp::Stop( void )
{
    radSoundPspLock( );
    m_Playing = false;
    m_CursorFrames = 0.0;
    radSoundPspUnlock( );
}

bool radSoundHalVoicePsp::IsPlaying( void )
{
    return m_Playing;
}

unsigned int radSoundHalVoicePsp::GetPlaybackPositionInSamples( void )
{
    // A frame == a sample for playback-position purposes here.
    return (unsigned int) m_CursorFrames;
}

void radSoundHalVoicePsp::SetPlaybackPositionInSamples( unsigned int positionInSamples )
{
    radSoundPspLock( );
    m_CursorFrames = (double) positionInSamples;
    radSoundPspUnlock( );
}

//========================================================================
// Gain / pan / pitch
//========================================================================

void radSoundHalVoicePsp::SetMuted( bool muted )
{
    m_MuteFactor = muted ? 0.0f : 1.0f;
}

bool radSoundHalVoicePsp::GetMuted( void )
{
    return m_MuteFactor == 0.0f;
}

void radSoundHalVoicePsp::SetVolume( float volume )
{
    ::radSoundVerifyAnalogVolume( volume );
    m_Volume = volume;
}

float radSoundHalVoicePsp::GetVolume( void )
{
    return m_Volume;
}

void radSoundHalVoicePsp::SetTrim( float trim )
{
    ::radSoundVerifyAnalogVolume( trim );
    m_Trim = trim;
}

float radSoundHalVoicePsp::GetTrim( void )
{
    return m_Trim;
}

void radSoundHalVoicePsp::SetPitch( float pitch )
{
    ::radSoundVerifyAnalogPitch( pitch );
    m_Pitch = pitch;
}

float radSoundHalVoicePsp::GetPitch( void )
{
    return m_Pitch;
}

void radSoundHalVoicePsp::SetPan( float pan )
{
    ::radSoundVerifyAnalogPan( pan );
    m_Pan = pan;
}

float radSoundHalVoicePsp::GetPan( void )
{
    return m_Pan;
}

//========================================================================
// Aux sends -- PSP mixer has no effects bus (see system.cpp).
//========================================================================

radSoundAuxMode radSoundHalVoicePsp::GetAuxMode( unsigned int aux )
{
    return radSoundAuxMode_Off;
}

void radSoundHalVoicePsp::SetAuxMode( unsigned int aux, radSoundAuxMode mode )
{
}

float radSoundHalVoicePsp::GetAuxGain( unsigned int aux )
{
    return 0.0f;
}

void radSoundHalVoicePsp::SetAuxGain( unsigned int aux, float gain )
{
}

//========================================================================
// Positional group
//========================================================================

void radSoundHalVoicePsp::SetPositionalGroup( IRadSoundHalPositionalGroup * pIRshpg )
{
    radSoundHalPositionalGroup * pGroup =
        dynamic_cast< radSoundHalPositionalGroup * >( pIRshpg );

    if ( pGroup != m_xRadSoundHalPositionalGroup )
    {
        if ( m_xRadSoundHalPositionalGroup != NULL )
        {
            m_xRadSoundHalPositionalGroup->RemovePositionalEntity( this );
        }

        m_xRadSoundHalPositionalGroup = pGroup;

        if ( m_xRadSoundHalPositionalGroup != NULL )
        {
            m_xRadSoundHalPositionalGroup->AddPositionalEntity( this );
        }
    }

    if ( m_xRadSoundHalPositionalGroup != NULL )
    {
        OnApplyPositionalSettings( );
    }
    else
    {
        radSoundPspLock( );
        m_PosVolume = 1.0f;
        m_PosPan = 0.0f;
        m_PosPitch = 1.0f;
        radSoundPspUnlock( );
    }
}

IRadSoundHalPositionalGroup * radSoundHalVoicePsp::GetPositionalGroup( void )
{
    return m_xRadSoundHalPositionalGroup;
}

void radSoundHalVoicePsp::OnApplyPositionalSettings( void )
{
    if ( m_xRadSoundHalPositionalGroup == NULL )
    {
        return;
    }

    const radSoundHalPositionalInformation & info =
        m_xRadSoundHalPositionalGroup->m_RadSoundHalPositionalInformation;

    radSoundPspLock( );
    m_PosVolume = info.m_VolumeAdjust;
    m_PosPan    = info.m_PanAdjust;
    m_PosPitch  = info.m_PitchAdjust;
    radSoundPspUnlock( );
}

//========================================================================
// radSoundHalVoicePsp::MixInto
//
// Called by the output thread under radSoundPspLock.  Renders this voice into
// a stereo int32 accumulation block, resampling from the buffer's sample rate
// (and pitch) to RSD_PSP_OUTPUT_RATE with nearest-neighbour sampling.
//========================================================================

void radSoundHalVoicePsp::MixInto( int * pAccum, unsigned int frames )
{
    if ( !m_Playing || m_xRadSoundHalBufferPsp == NULL )
    {
        return;
    }

    const unsigned char * pData = m_xRadSoundHalBufferPsp->GetSampleData( );
    unsigned int sizeInFrames = m_xRadSoundHalBufferPsp->GetSizeInFrames( );

    if ( pData == NULL || sizeInFrames == 0 )
    {
        return;
    }

    unsigned int channels = m_xIRadSoundHalAudioFormat->GetNumberOfChannels( );
    unsigned int bits     = m_xIRadSoundHalAudioFormat->GetBitResolution( );
    unsigned int srcRate  = m_xIRadSoundHalAudioFormat->GetSampleRate( );

    float gain = m_Trim * m_Volume * m_MuteFactor * m_PosVolume;
    if ( gain <= 0.0f )
    {
        // Silent, but still advance so timing/positions stay correct.
        gain = 0.0f;
    }

    // Linear constant-power-ish pan split.
    float pan = m_Pan + m_PosPan;
    if ( pan < -1.0f ) pan = -1.0f;
    if ( pan >  1.0f ) pan =  1.0f;
    float leftGain  = gain * ( pan <= 0.0f ? 1.0f : ( 1.0f - pan ) );
    float rightGain = gain * ( pan >= 0.0f ? 1.0f : ( 1.0f + pan ) );

    double step = ( (double) srcRate / (double) RSD_PSP_OUTPUT_RATE )
                  * (double) ( m_Pitch * m_PosPitch );
    if ( step <= 0.0 )
    {
        return;
    }

    bool looping = m_xRadSoundHalBufferPsp->IsLooping( );

    const short * pS16 = (const short *) pData;

    double cursor = m_CursorFrames;

    for ( unsigned int i = 0; i < frames; i++ )
    {
        if ( cursor >= (double) sizeInFrames )
        {
            if ( looping )
            {
                cursor = fmod( cursor, (double) sizeInFrames );
            }
            else
            {
                m_Playing = false;
                break;
            }
        }

        unsigned int idx = (unsigned int) cursor;
        if ( idx >= sizeInFrames )
        {
            idx = sizeInFrames - 1;
        }

        // Second sample for linear interpolation (wrap when looping, else clamp).
        float frac = (float) ( cursor - (double) idx );
        unsigned int idx2 = idx + 1;
        if ( idx2 >= sizeInFrames )
        {
            idx2 = looping ? 0 : idx;
        }

        int l0, r0, l1, r1;

        if ( bits == 8 )
        {
            if ( channels >= 2 )
            {
                l0 = ( (int) pData[ idx  * 2 ]     - 128 ) << 8;
                r0 = ( (int) pData[ idx  * 2 + 1 ] - 128 ) << 8;
                l1 = ( (int) pData[ idx2 * 2 ]     - 128 ) << 8;
                r1 = ( (int) pData[ idx2 * 2 + 1 ] - 128 ) << 8;
            }
            else
            {
                l0 = r0 = ( (int) pData[ idx  ] - 128 ) << 8;
                l1 = r1 = ( (int) pData[ idx2 ] - 128 ) << 8;
            }
        }
        else
        {
            if ( channels >= 2 )
            {
                l0 = pS16[ idx  * 2 ];   r0 = pS16[ idx  * 2 + 1 ];
                l1 = pS16[ idx2 * 2 ];   r1 = pS16[ idx2 * 2 + 1 ];
            }
            else
            {
                l0 = r0 = pS16[ idx  ];
                l1 = r1 = pS16[ idx2 ];
            }
        }

        // Linear interpolation (frac == 0 for exact 1:1 playback -> l0/r0).
        float left  = (float) l0 + ( (float) ( l1 - l0 ) ) * frac;
        float right = (float) r0 + ( (float) ( r1 - r0 ) ) * frac;

        pAccum[ i * 2 ]     += (int) ( left  * leftGain );
        pAccum[ i * 2 + 1 ] += (int) ( right * rightGain );

        cursor += step;
    }

    m_CursorFrames = cursor;
}

//========================================================================
// ::radSoundHalVoiceCreate
//========================================================================

IRadSoundHalVoice * radSoundHalVoiceCreate( radMemoryAllocator allocator )
{
    return new ( "radSoundHalVoicePsp", allocator ) radSoundHalVoicePsp( );
}
