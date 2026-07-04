/*============================================================================
 * psplog.h  --  PSP diagnostic-log master switch
 *
 * The PSP port is littered with Memory-Stick diagnostic logs of the form
 *
 *     FILE* f = fopen("ms0:/shar_xxx.log", "a");
 *     if (f) { fprintf(f, ...); fclose(f); }
 *
 * On FAT media every such open/append/close costs several milliseconds
 * (directory lookup, FAT-chain walk, cluster allocate, directory flush).
 * Emitting them per-texture / per-shader / per-chunk / per-buffer-fill during
 * a load turns a few-second load into ~40s -- the cost scales with asset
 * COUNT, not byte size.
 *
 * Rather than gate all ~60 call sites individually, we exploit the fact that
 * every one null-checks the handle: route the log opens through
 * pspDiagFopen(), which returns NULL when logging is disabled, so each block
 * skips itself for the price of one branch. Real file I/O (asset paths) never
 * goes through here -- only the "ms0:/shar_*.log" diagnostic calls are
 * rewritten to pspDiagFopen().
 *
 * This header is force-included into every PSP translation unit (see
 * psp/CMakeLists.txt). Flip PSP_DIAG_LOG to 1 (here or via -DPSP_DIAG_LOG=1)
 * to restore the instrumentation.
 *==========================================================================*/
#ifndef PSP_PSPLOG_H
#define PSP_PSPLOG_H

#ifndef PSP_DIAG_LOG
#define PSP_DIAG_LOG 0
#endif

#include <stdio.h>

static inline FILE* pspDiagFopen( const char* path, const char* mode )
{
#if PSP_DIAG_LOG
    return fopen( path, mode );
#else
    (void)path; (void)mode;
    return (FILE*)0;
#endif
}

#endif /* PSP_PSPLOG_H */
