//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        pspdrives.cpp
//
// Subsystem:   Radical Drive System
//
// Description: Implementation of radPspDrive. Backs radDrive with newlib
//              stdio, which pspdev routes to Memory Stick file I/O. These
//              methods run serially on the drive thread, so blocking is fine.
//
//=============================================================================

//=============================================================================
// Include Files
//=============================================================================

#include "pch.hpp"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <radstring.hpp>
#include "pspdrives.hpp"

static void dlog(const char* s)
{
    FILE* f = pspDiagFopen("ms0:/shar_drive.log", "a");
    if (f) { fputs(s, f); fputc('\n', f); fclose(f); }
}

//=============================================================================
// Public Functions
//=============================================================================

//=============================================================================
// Function:    radPspDriveFactory
//=============================================================================

void radPspDriveFactory
(
    radDrive**         ppDrive,
    const char*        pDriveName,
    radMemoryAllocator alloc
)
{
    *ppDrive = new( alloc ) radPspDrive( pDriveName, alloc );
    rAssert( *ppDrive != NULL );
}

//=============================================================================
// Public Member Functions
//=============================================================================

//=============================================================================
// Function:    radPspDrive::radPspDrive
//=============================================================================

radPspDrive::radPspDrive( const char* pdrivespec, radMemoryAllocator alloc )
    :
    radDrive( ),
    m_Capabilities( 0 ),
    m_OpenFiles( 0 ),
    m_pMutex( NULL )
{
    //
    // Create a mutex for lock/unlock
    //
    radThreadCreateMutex( &m_pMutex, alloc );
    rAssert( m_pMutex != NULL );

    //
    // Create the drive thread (base class owns request execution on it).
    //
    // The drive thread runs newlib stdio (fopen/fread/fseek) directly, plus
    // BuildPath/OpenFile's on-stack path buffers. The radDriveThread default
    // stack of 4 KB overflows on the very first Memory Stick open (newlib's
    // first-call setup is stack-hungry), corrupting the thread. Since this is
    // a high-priority thread that PSP's strict scheduler runs ahead of the
    // load worker, that overflow takes down file IO before anything completes.
    // Give it a real stack, in line with the load worker's 64 KB.
    //
    m_pDriveThread = new( alloc ) radDriveThread( m_pMutex, alloc, 32 * 1024 );
    rAssert( m_pDriveThread != NULL );

    //
    // Record the drive name. If this isn't the default drive, treat the
    // spec as a path prefix (e.g. "ms0:/data") prepended to opened files.
    //
    //
    // Capture the calling (main) thread's CWD as the base path for relative
    // filenames. The drive thread created below gets a separate CWD that may
    // not match, so we resolve relative paths here using the main thread's
    // directory (which PPSSPP / the PSP OS sets to the game directory).
    //
    m_DrivePath[ 0 ] = '\0';
    {
        char cwd[ radFileFilenameMax + 1 ];
        cwd[ 0 ] = '\0';
        if ( getcwd( cwd, radFileFilenameMax ) && cwd[ 0 ] != '\0' )
        {
            size_t len = strlen( cwd );
            // Ensure trailing slash.
            if ( len < (size_t)radFileFilenameMax && cwd[ len - 1 ] != '/' )
            {
                cwd[ len ]     = '/';
                cwd[ len + 1 ] = '\0';
            }
            strncpy( m_DrivePath, cwd, radFileFilenameMax );
            m_DrivePath[ radFileFilenameMax ] = '\0';
            dlog( m_DrivePath );
        }
        else
        {
            dlog( "getcwd failed; relative paths will use drive-thread CWD" );
        }
    }
    radGetDefaultDrive( m_DriveName );
    if ( strcmp( m_DriveName, pdrivespec ) != 0 )
    {
        strncpy( m_DriveName, pdrivespec, radFileDrivenameMax );
        m_DriveName[ radFileDrivenameMax ] = '\0';
    }

    m_Capabilities = ( radDriveWriteable | radDriveFile );
}

//=============================================================================
// Function:    radPspDrive::~radPspDrive
//=============================================================================

radPspDrive::~radPspDrive( void )
{
    m_pMutex->Release( );
    m_pDriveThread->Release( );
}

//=============================================================================
// Function:    radPspDrive::Lock / Unlock
//=============================================================================

void radPspDrive::Lock( void )
{
    m_pMutex->Lock( );
}

void radPspDrive::Unlock( void )
{
    m_pMutex->Unlock( );
}

//=============================================================================
// Function:    radPspDrive::GetCapabilities
//=============================================================================

unsigned int radPspDrive::GetCapabilities( void )
{
    return m_Capabilities;
}

//=============================================================================
// Function:    radPspDrive::GetDriveName
//=============================================================================

const char* radPspDrive::GetDriveName( void )
{
    return m_DriveName;
}

//=============================================================================
// Function:    radPspDrive::Initialize
//=============================================================================

radDrive::CompletionStatus radPspDrive::Initialize( void )
{
    //
    // The Memory Stick is always assumed present; we don't query free space.
    //
    strncpy( m_MediaInfo.m_VolumeName, m_DriveName, 64 );
    m_MediaInfo.m_VolumeName[ 64 ] = '\0';
    m_MediaInfo.m_SectorSize  = 512;
    m_MediaInfo.m_MediaState  = IRadDrive::MediaInfo::MediaPresent;
    m_MediaInfo.m_FreeSpace   = UINT_MAX;
    m_MediaInfo.m_FreeFiles   = m_MediaInfo.m_FreeSpace / m_MediaInfo.m_SectorSize;

    m_LastError = Success;
    return Complete;
}

//=============================================================================
// Function:    radPspDrive::BuildPath
//=============================================================================

void radPspDrive::BuildPath( char* dest, unsigned int destSize, const char* fileName )
{
    //
    // PSP absolute paths carry a device prefix ("ms0:/", "disc0:/") or start
    // with '/'. Only prepend m_DrivePath (the captured game directory) for
    // relative filenames so we don't corrupt already-absolute paths.
    //
    bool isRelative = ( strchr( fileName, ':' ) == NULL && fileName[ 0 ] != '/' );

    if ( isRelative && m_DrivePath[ 0 ] != '\0' )
    {
        strncpy( dest, m_DrivePath, destSize - 1 );
        dest[ destSize - 1 ] = '\0';
        strncat( dest, fileName, destSize - strlen( dest ) - 1 );
    }
    else
    {
        strncpy( dest, fileName, destSize - 1 );
        dest[ destSize - 1 ] = '\0';
    }

    for ( char* p = dest; *p != '\0'; ++p )
    {
        if ( *p == '\\' )
        {
            *p = '/';
        }
    }
}

//=============================================================================
// Function:    radPspDrive::OpenFile
//=============================================================================

radDrive::CompletionStatus radPspDrive::OpenFile
(
    const char*         fileName,
    radFileOpenFlags    flags,
    bool                writeAccess,
    radFileHandle*      pHandle,
    unsigned int*       pSize
)
{
    char path[ radFileFilenameMax + 1 ];
    BuildPath( path, sizeof( path ), fileName );

    dlog( path );   // log the resolved path so ms0:/shar_drive.log shows exactly what fopen sees

    //
    // Translate flags to a stdio mode string. CreateAlways truncates/creates;
    // OpenExisting requires the file to already exist.
    //
    const char* mode;
    if ( flags == CreateAlways )
    {
        mode = writeAccess ? "w+b" : "wb";
    }
    else
    {
        mode = writeAccess ? "r+b" : "rb";
    }

    FILE* fp = fopen( path, mode );
    if ( fp == NULL )
    {
        dlog( "fopen FAILED" );
        *pSize = 0;
        m_LastError = FileNotFound;
        return Error;
    }
    dlog( "fopen OK" );

    //
    // Determine file size.
    //
    *pSize = 0;
    if ( fseek( fp, 0, SEEK_END ) == 0 )
    {
        long end = ftell( fp );
        if ( end >= 0 )
        {
            *pSize = (unsigned int) end;
        }
        fseek( fp, 0, SEEK_SET );
    }

    *pHandle = fp;
    m_OpenFiles++;
    m_LastError = Success;
    return Complete;
}

//=============================================================================
// Function:    radPspDrive::CloseFile
//=============================================================================

radDrive::CompletionStatus radPspDrive::CloseFile( radFileHandle handle, const char* fileName )
{
    if ( handle != NULL )
    {
        fclose( handle );
        m_OpenFiles--;
    }
    m_LastError = Success;
    return Complete;
}

//=============================================================================
// Function:    radPspDrive::ReadFile
//=============================================================================

radDrive::CompletionStatus radPspDrive::ReadFile
(
    radFileHandle   handle,
    const char*     fileName,
    IRadFile::BufferedReadState buffState,
    unsigned int    position,
    void*           pData,
    unsigned int    bytesToRead,
    unsigned int*   bytesRead,
    radMemorySpace  pDataSpace
)
{
    rAssertMsg( pDataSpace == radMemorySpace_Local,
                "radFileSystem: radPspDrive: External memory not supported for reads." );

    if ( fseek( handle, (long) position, SEEK_SET ) != 0 )
    {
        m_LastError = FileNotFound;
        return Error;
    }

    size_t read = fread( pData, 1, bytesToRead, handle );
    if ( ferror( handle ) )
    {
        m_LastError = FileNotFound;
        return Error;
    }

    *bytesRead = (unsigned int) read;
    m_LastError = Success;
    return Complete;
}

//=============================================================================
// Function:    radPspDrive::WriteFile
//=============================================================================

radDrive::CompletionStatus radPspDrive::WriteFile
(
    radFileHandle     handle,
    const char*       fileName,
    IRadFile::BufferedReadState buffState,
    unsigned int      position,
    const void*       pData,
    unsigned int      bytesToWrite,
    unsigned int*     bytesWritten,
    unsigned int*     pSize,
    radMemorySpace    pDataSpace
)
{
    if ( !( m_Capabilities & radDriveWriteable ) )
    {
        rWarningMsg( m_Capabilities & radDriveWriteable, "This drive does not support the WriteFile function." );
        return Error;
    }

    rAssertMsg( pDataSpace == radMemorySpace_Local,
                "radFileSystem: radPspDrive: External memory not supported for writes." );

    if ( fseek( handle, (long) position, SEEK_SET ) != 0 )
    {
        m_LastError = FileNotFound;
        return Error;
    }

    size_t written = fwrite( pData, 1, bytesToWrite, handle );
    if ( ferror( handle ) )
    {
        m_LastError = FileNotFound;
        return Error;
    }

    *bytesWritten = (unsigned int) written;

    //
    // Report new size.
    //
    fseek( handle, 0, SEEK_END );
    long end = ftell( handle );
    *pSize = ( end >= 0 ) ? (unsigned int) end : 0;

    m_LastError = Success;
    return Complete;
}
