//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        pspdrives.hpp
//
// Subsystem:   Radical Drive System
//
// Description: PSP physical drive. Backs the radDrive interface with newlib
//              stdio (fopen/fread/fseek/fclose), which the pspdev toolchain
//              maps onto Memory Stick file I/O. Modeled on radWin32Drive but
//              without std::filesystem (not needed for read-mostly asset use).
//
// Revisions:
//
//=============================================================================

#ifndef PSPDRIVE_HPP
#define PSPDRIVE_HPP

//=============================================================================
// Include Files
//=============================================================================
#include "../common/drive.hpp"
#include "../common/drivethread.hpp"

//=============================================================================
// Public Functions
//=============================================================================

//
// Every physical drive type must provide a drive factory.
//
void radPspDriveFactory( radDrive** ppDrive, const char* driveSpec, radMemoryAllocator alloc );

//=============================================================================
// Class Declarations
//=============================================================================

//
// This is a PSP Drive. It implements the appropriate radDrive members.
//
class radPspDrive : public radDrive
{
public:

    radPspDrive( const char* pdrivespec, radMemoryAllocator alloc );
    virtual ~radPspDrive( void );

    void Lock( void );
    void Unlock( void );

    unsigned int GetCapabilities( void );

    const char* GetDriveName( void );

    CompletionStatus Initialize( void );

    CompletionStatus OpenFile( const char*        fileName,
                               radFileOpenFlags   flags,
                               bool               writeAccess,
                               radFileHandle*     pHandle,
                               unsigned int*      pSize );

    CompletionStatus CloseFile( radFileHandle handle, const char* fileName );

    CompletionStatus ReadFile( radFileHandle      handle,
                               const char*        fileName,
                               IRadFile::BufferedReadState buffState,
                               unsigned int       position,
                               void*              pData,
                               unsigned int       bytesToRead,
                               unsigned int*      bytesRead,
                               radMemorySpace     pDataSpace );

    CompletionStatus WriteFile( radFileHandle     handle,
                                const char*       fileName,
                                IRadFile::BufferedReadState buffState,
                                unsigned int      position,
                                const void*       pData,
                                unsigned int      bytesToWrite,
                                unsigned int*     bytesWritten,
                                unsigned int*     size,
                                radMemorySpace    pDataSpace );

private:
    //
    // Build a full filesystem path from the drive prefix and a relative name.
    //
    void BuildPath( char* dest, unsigned int destSize, const char* fileName );

    unsigned int    m_Capabilities;
    unsigned int    m_OpenFiles;
    char            m_DriveName[ radFileDrivenameMax + 1 ];

    //
    // Path prefix prepended to opened files. Empty => relative to cwd
    // (the EBOOT directory on the Memory Stick).
    //
    char            m_DrivePath[ radFileFilenameMax + 1 ];

    //
    // Mutex for critical sections
    //
    IRadThreadMutex*    m_pMutex;
};

#endif // PSPDRIVE_HPP
