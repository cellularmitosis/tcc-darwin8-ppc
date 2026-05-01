/*
 * ppc-macho-stubs.c — minimal Mach-O integration shims for PPC32.
 *
 * The real tccmacho.c is hardcoded for x86_64 / arm64 (deals with
 * Rela / 64-bit load commands / arm64 specifics) and won't compile
 * for PPC32 without significant rework. We defer that rework
 * until we need real .o file emission. Until then, this file
 * provides the minimum integration symbols that libtcc.c references
 * unconditionally on Mach-O targets, so the build links and tcc -c
 * produces (currently ELF-formatted) object files.
 *
 * Symbols provided:
 *   - tcc_add_macos_sdkpath: add Tiger-appropriate library paths.
 *   - macho_load_dll / macho_load_tbd: not supported on PPC32 yet.
 *   - macho_output_file: not supported on PPC32 yet (stubs to error).
 *
 * When real Mach-O emission lands (roadmap phase 3), this file goes
 * away and tccmacho.c picks up PPC32 alongside its x86_64/arm64.
 *
 * Copyright (c) 2026 the tcc-darwin8-ppc contributors. MIT.
 */

#include "tcc.h"

ST_FUNC void tcc_add_macos_sdkpath(TCCState *s)
{
    /* Tiger does not have xcode-select or .sdk bundles. The system
     * libraries live in /usr/lib and headers in /usr/include — both
     * are flat, no SDK indirection. The configure script already
     * configures /usr/include as the system include dir; here we
     * just add /usr/lib as a library search path. */
    tcc_add_library_path(s, "/usr/lib");
}

ST_FUNC int macho_load_dll(TCCState *s1, int fd, const char *filename, int lev)
{
    (void)fd; (void)lev;
    tcc_error_noabort("ppc-macho-stubs: dynamic library loading "
                      "not yet supported (file: %s)", filename);
    return -1;
}

ST_FUNC int macho_load_tbd(TCCState *s1, int fd, const char *filename, int lev)
{
    (void)fd; (void)lev;
    tcc_error_noabort("ppc-macho-stubs: .tbd library loading "
                      "not yet supported (file: %s)", filename);
    return -1;
}

ST_FUNC int macho_output_file(TCCState *s1, const char *filename)
{
    (void)s1;
    tcc_error_noabort("ppc-macho-stubs: Mach-O output not yet "
                      "implemented (would write %s)", filename);
    return -1;
}

ST_FUNC char *macho_tbd_soname(int fd)
{
    (void)fd;
    return NULL;  /* no .tbd parsing yet */
}

/* tccrun.c calls __clear_cache() to flush the icache after writing
 * JITted code. Tiger's gcc-4.0 / libgcc don't provide this symbol
 * (it's a libgcc helper that wasn't shipped with the Apple GCC of
 * the era); 10.5+ has sys_icache_invalidate() in libkern, but Tiger
 * predates that. Provide our own using the PPC dcbst/icbi/sync/isync
 * sequence, one cache line at a time. G3 cache lines are 32 bytes;
 * we use 32 to be safe across the whole G3/G4/G5 family. */
#if defined __APPLE__ && defined __ppc__
void __clear_cache(void *begin, void *end)
{
    char *p = (char *)begin;
    char *e = (char *)end;
    /* Round to cache-line boundaries. */
    char *p0 = (char *)((uintptr_t)p & ~31);
    char *q;
    /* Step 1: write dirty d-cache lines back to memory. */
    for (q = p0; q < e; q += 32)
        __asm__ volatile("dcbst 0, %0" : : "r"(q) : "memory");
    __asm__ volatile("sync" : : : "memory");
    /* Step 2: invalidate i-cache lines so they refetch the new code. */
    for (q = p0; q < e; q += 32)
        __asm__ volatile("icbi 0, %0" : : "r"(q) : "memory");
    __asm__ volatile("sync" : : : "memory");
    __asm__ volatile("isync" : : : "memory");
}
#endif
