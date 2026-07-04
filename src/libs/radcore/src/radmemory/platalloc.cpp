//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================



#include "pch.hpp"
#include <raddebug.hpp>
#include "platalloc.hpp"

#if defined WIN32 || defined RAD_UWP || defined RAD_PSP

    #include <stdlib.h>
    #include <malloc.h>

    #if defined MALLOC_DEBUG
        #include <crtdbg.h>
    #endif

#endif

#if defined RAD_PSP
    #include <stdio.h>
    // Real heap accounting for the OOM investigation. sceKernelTotalFreeMemSize()
    // reports the *system partition* free space, which is constant because
    // PSP_HEAP_SIZE_KB pre-reserves the whole app heap up front — it does NOT
    // track newlib malloc usage. mallinfo() reads the actual malloc arena, so
    // uordblks (bytes currently handed out) is the true high-water signal, and
    // this is where we find out if a real PSP's ~24 MB user heap is the wall.
    // On a NULL return we log the failing size + arena stats to ms0:/shar_oom.log
    // exactly once per run's first few failures (the fault that follows is the
    // unchecked NULL deref that powers the console off).
    static void pspOomLog( const char* what, unsigned int wanted )
    {
        static int s_count = 0;
        if( s_count++ > 8 ) return;   // first few only; the file IO itself allocs
        struct mallinfo mi = mallinfo();
        FILE* f = fopen( "ms0:/shar_oom.log", "a" );
        if( f )
        {
            fprintf( f, "%s FAILED want=%u | arena=%d used=%d free=%d keepcost=%d\n",
                     what, wanted, mi.arena, mi.uordblks, mi.fordblks, mi.keepcost );
            fclose( f );
        }
    }
#endif

//============================================================================
// ::radMemoryPlatInitialize
//============================================================================
void radMemoryPlatInitialize( void )
{

}

//============================================================================
// ::radMemoryPlatTerminate
//============================================================================

void radMemoryPlatTerminate( void )
{
}

//============================================================================
// ::radMemoryPlatAlloc
//============================================================================

void * radMemoryPlatAlloc( unsigned int numberOfBytes )
{
    void * pMemory;
    //
    // C++ standard says you can allocate 0 byte memory object.
    //
    if ( numberOfBytes == 0 )
    {
        numberOfBytes = 1;
    }

    pMemory = malloc( numberOfBytes );

#if defined RAD_PSP
    if( pMemory == NULL ) pspOomLog( "malloc", numberOfBytes );
#endif

    rWarningMsg( pMemory != NULL, "radMemory: Platform (malloc) allocator failed to allocate memory\n" );
    return pMemory;
}

//============================================================================
// ::radMemoryPlatFree
//============================================================================

void radMemoryPlatFree( void * pMemory )
{
    free( pMemory );
}

//============================================================================
// ::radMemoryPlatAllocAligned
//============================================================================

void * radMemoryPlatAllocAligned( unsigned int numberOfBytes, unsigned int alignment )
{
	#ifndef WIN32

		// C11 aligned_alloc requires size to be a multiple of alignment; round
		// up so callers passing e.g. an 8-bit 191x245 texture (46795 B, align 16)
		// get a valid request rather than UB. Harmless slack on every platform.
		if( alignment != 0 && (numberOfBytes % alignment) != 0 )
			numberOfBytes += alignment - (numberOfBytes % alignment);

		void * pMemory = ::aligned_alloc( alignment, numberOfBytes );
		#if defined RAD_PSP
			if( pMemory == NULL ) pspOomLog( "aligned_alloc", numberOfBytes );
		#endif
		return pMemory;

	#else

        return _aligned_malloc( numberOfBytes, alignment );

	#endif
}

//============================================================================
// ::radMemoryPlatFreeAligned
//============================================================================

void radMemoryPlatFreeAligned( void * pAlignedMemory )
{

	#ifndef WIN32
		
		free( pAlignedMemory );
	
	#else

        _aligned_free( pAlignedMemory );

	#endif
}
