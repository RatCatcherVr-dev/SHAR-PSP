//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _PLAT_TYPES_HPP
#define _PLAT_TYPES_HPP

//
// Plain int-width types for MIPS32 (PSP). Deliberately NOT <cstdint>:
// newlib maps uint32_t to 'unsigned long' on MIPS, which mismatches pddi's
// PDDI_U32 ('unsigned int'). Using the plain int family keeps P3D_U* and
// PDDI_U* the same underlying type so pointers interconvert cleanly.
//
typedef long long          P3D_S64;
typedef unsigned long long P3D_U64;
typedef int                P3D_S32;
typedef unsigned           P3D_U32;
typedef short              P3D_S16;
typedef unsigned short     P3D_U16;
typedef signed char        P3D_S8;
typedef unsigned char      P3D_U8;
// 16-bit to match the win32 reference (uint16_t) and Scrooby's UnicodeChar
// (unsigned short). The font/text + Scrooby code is all written for 16-bit
// unicode; a 32-bit P3D_UNICODE mismatches UnicodeChar* and breaks text.
typedef unsigned short     P3D_UNICODE;

#endif
