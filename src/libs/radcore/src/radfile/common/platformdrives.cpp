//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include "pch.hpp"
#include <string.h>
#include <radstring.hpp>
#include "platformdrives.hpp"
#if !defined(RAD_PSP)
#include "remotedrive.hpp"
#endif

#if defined(RAD_WIN32) || defined(RAD_UWP)
#include "../win32/win32drive.hpp"
#elif defined(RAD_PSP)
#include "../psp/pspdrives.hpp"
#endif

//=============================================================================
// Function:    PlatformDrivesGetDefaultDrive
//=============================================================================
// Description: Copy over the name of the default drive.
//
// Parameters:  char* string
//
// Returns:   
//
//------------------------------------------------------------------------------
void PlatformDrivesGetDefaultDrive( char* driveSpec )
{
//
// Get the current drive
//
#if defined(RAD_WIN32) || defined(RAD_UWP)
    char bigDir[ radFileFilenameMax + 1 ];
    ::GetCurrentDirectory( radFileFilenameMax, bigDir );
    strncpy( driveSpec, bigDir, 2 );
    driveSpec[ 2 ] = '\0';
    strupr( driveSpec );
#elif defined(RAD_PSP)
    strncpy( driveSpec, "ms0:", radFileDrivenameMax );
    driveSpec[ radFileDrivenameMax ] = '\0';
#endif // RAD_WIN32 || RAD_UWP
}

//=============================================================================
// Function:    PlatformDrivesValidateDriveName
//=============================================================================

bool PlatformDrivesValidateDriveName( const char* driveSpec )
{
#if !defined(RAD_PSP)
    if ( strcmp( driveSpec, s_RemoteDriveName ) == 0 )
    {
        return true;
    }
#endif

#if defined(RAD_WIN32) || defined(RAD_UWP)
    if( (strlen( driveSpec ) == 2) && (*driveSpec >= 'A') && (*driveSpec <= 'Z') )
    {
        unsigned int index = *driveSpec - 'A';
        DWORD drives = GetLogicalDrives( );
        drives >>= index;
        return ( drives & 1 );
    }
    else
    {
        return false;
    }
#elif defined(RAD_PSP)
    return true;
#endif // RAD_WIN32 || RAD_UWP
}

//=============================================================================
// Function:    PlatformDrivesFactory
//=============================================================================

void PlatformDrivesFactory( radDrive** ppDrive, const char* driveSpec, radMemoryAllocator alloc )
{
    rAssert( ppDrive != NULL );
    rAssert( driveSpec != NULL );

#if !defined(RAD_PSP)
    if ( strcmp( driveSpec, s_RemoteDriveName ) == 0 )
    {
        radRemoteDriveFactory( ppDrive, driveSpec, alloc );
        return;
    }
#endif

#if defined(RAD_WIN32) || defined(RAD_UWP)
    radWin32DriveFactory( ppDrive, driveSpec, alloc );
#elif defined(RAD_PSP)
    radPspDriveFactory( ppDrive, driveSpec, alloc );
#endif
}
