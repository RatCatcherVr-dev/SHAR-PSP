//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================
//
// File: radsoundpsp.hpp
//
// Description: Shared declarations for the PSP radsound HAL backend.  Unlike
//              the Win32 (OpenAL) or PS2 (SPU) backends, the PSP has no
//              hardware mixing voices exposed to the app -- sceAudio just
//              accepts a single interleaved stereo PCM block.  So this backend
//              is a small software mixer: buffers hold raw PCM in main RAM,
//              voices carry a fractional read cursor + gain/pan/pitch, and a
//              dedicated sceAudio output thread sums all playing voices into
//              the hardware block (see system.cpp).
//
//=============================================================================

#ifndef RADSOUNDPSP_HPP
#define RADSOUNDPSP_HPP

//============================================================================
// Mixer configuration
//============================================================================

// Output goes through pspaudiolib, which runs the sceAudio output thread (with
// the correct priority + double-buffering) and calls us back to fill a 44.1kHz
// interleaved-stereo 16-bit block.  The mixer resamples each voice from its own
// rate*pitch to 44.1kHz with LINEAR interpolation (voice.cpp) -- nearest-
// neighbour aliased audibly on real hardware.
#define RSD_PSP_OUTPUT_RATE     44100
#define RSD_PSP_OUTPUT_CHANNELS 2

//============================================================================
// Cross-component lock
//
// The output thread walks the voice list and reads voice/buffer state while
// the main thread creates/destroys/plays/stops them.  Everything that mutates
// that shared state (voice + buffer ctors/dtors, Play/Stop/SetBuffer, and the
// mixer pass itself) brackets the critical region with these.  They are safe
// to call before the system exists (no-op until the lock is created).
//============================================================================

void radSoundPspLock( void );
void radSoundPspUnlock( void );

#endif // RADSOUNDPSP_HPP
