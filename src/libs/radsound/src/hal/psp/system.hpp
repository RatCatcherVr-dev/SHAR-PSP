//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: system.hpp  (PSP HAL)
//
//=============================================================================

#ifndef PSP_SYSTEM_HPP
#define PSP_SYSTEM_HPP

#include <radsound_hal.hpp>
#include <radsoundobject.hpp>

#define RSD_PSP_MAX_AUX_SENDS 2

//============================================================================
// Component: radSoundHalSystemPsp
//
// Owns the sound memory region, the software listener, and the sceAudio output
// thread that drives the software mixer.
//============================================================================

class radSoundHalSystemPsp
    :
    public IRadSoundHalSystem,
    public radSoundObject
{
    public:

        IMPLEMENT_REFCOUNTED( "radSoundHalSystemPsp" )

        radSoundHalSystemPsp( radMemoryAllocator allocator );
        ~radSoundHalSystemPsp( void );

        virtual void Initialize( const SystemDescription & systemDescription );
        virtual IRadSoundHalMemoryRegion * GetRootMemoryRegion( void );
        virtual unsigned int GetNumAuxSends( void );
        virtual void SetOutputMode( radSoundOutputMode mode );
        virtual radSoundOutputMode GetOutputMode( void );
        virtual void Service( void );
        virtual void ServiceOncePerFrame( void );
        virtual void GetStats( IRadSoundHalSystem::Stats * pStats );
        virtual void SetAuxEffect( unsigned int auxNumber, IRadSoundHalEffect * pIRadSoundHalEffect );
        virtual IRadSoundHalEffect * GetAuxEffect( unsigned int auxNumber );
        virtual void SetAuxGain( unsigned int aux, float gain );
        virtual float GetAuxGain( unsigned int aux );

        static radSoundHalSystemPsp * GetInstance( void );

        static radSoundHalSystemPsp * s_pRsdSystem;

    private:

        void *        m_pSoundMemory;
        unsigned int  m_NumAuxSends;
        unsigned int  m_LastServiceTime;

        bool          m_AudioInited;

        radRef< IRadSoundHalEffect > m_refIRadSoundHalEffect[ RSD_PSP_MAX_AUX_SENDS ];
};

#endif // PSP_SYSTEM_HPP
