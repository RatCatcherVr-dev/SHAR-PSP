//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: voice.hpp  (PSP HAL)
//
//=============================================================================

#ifndef PSP_VOICE_HPP
#define PSP_VOICE_HPP

#include <radlinkedclass.hpp>
#include <radsound_hal.hpp>
#include <radsoundobject.hpp>
#include "buffer.hpp"
#include "../common/softwarepositionalgroup.hpp"

//============================================================================
// Component: radSoundHalVoicePsp
//
// A software voice.  Holds a fractional read cursor into its buffer plus gain/
// pan/pitch; the output thread calls MixInto() to render this voice into the
// hardware block.  Positional voices get their volume/pan/pitch scaled by the
// software listener via the common positional group (OnApplyPositionalSettings).
//============================================================================

class radSoundHalVoicePsp
    :
    public IRadSoundHalVoice,
    public radSoundHalPositionalEntity,
    public radLinkedClass< radSoundHalVoicePsp >,
    public radSoundObject
{
    public:

        radSoundHalVoicePsp( void );

        IMPLEMENT_REFCOUNTED( "radSoundHalVoicePsp" )

        virtual void SetPriority( unsigned int priority );
        virtual unsigned int GetPriority( void );

        virtual void SetBuffer( IRadSoundHalBuffer * pIRadSoundHalBuffer );
        virtual IRadSoundHalBuffer * GetBuffer( void );

        virtual void Play( void );
        virtual void Stop( void );
        virtual bool IsPlaying( void );

        virtual void SetPlaybackPositionInSamples( unsigned int position );
        virtual unsigned int GetPlaybackPositionInSamples( void );

        virtual void SetVolume( float volume );
        virtual float GetVolume( void );
        virtual void SetTrim( float trim );
        virtual float GetTrim( void );
        virtual void SetMuted( bool muteOn );
        virtual bool GetMuted( void );
        virtual void SetPan( float pan );
        virtual float GetPan( void );
        virtual void SetPitch( float pitch );
        virtual float GetPitch( void );

        virtual void SetAuxMode( unsigned int aux, radSoundAuxMode mode );
        virtual radSoundAuxMode GetAuxMode( unsigned int aux );
        virtual void SetAuxGain( unsigned int aux, float gain );
        virtual float GetAuxGain( unsigned int aux );

        virtual void SetPositionalGroup( IRadSoundHalPositionalGroup * pIRshpg );
        virtual IRadSoundHalPositionalGroup * GetPositionalGroup( void );

        // radSoundHalPositionalEntity
        virtual void OnApplyPositionalSettings( void );

        // Called by the output thread (under radSoundPspLock) to render this
        // voice into a stereo int32 accumulation block of 'frames' frames at
        // RSD_PSP_OUTPUT_RATE.
        void MixInto( int * pAccum, unsigned int frames );

    protected:

        virtual ~radSoundHalVoicePsp( void );

    private:

        unsigned int m_Priority;

        float m_Pitch;
        float m_Volume;
        float m_Trim;
        float m_MuteFactor;
        float m_Pan;

        // Positional adjustments pushed in by the listener/group.
        float m_PosVolume;
        float m_PosPan;
        float m_PosPitch;

        // Playback state (read on the output thread).
        bool   m_Playing;
        double m_CursorFrames;

        radRef< radSoundHalBufferPsp >           m_xRadSoundHalBufferPsp;
        radRef< IRadSoundHalAudioFormat >        m_xIRadSoundHalAudioFormat;
        radRef< radSoundHalPositionalGroup >     m_xRadSoundHalPositionalGroup;
};

#endif // PSP_VOICE_HPP
