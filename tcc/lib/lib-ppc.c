/*
 *  TCC runtime library for 32-bit PowerPC (Apple Mach-O).
 *
 *  Provides the libgcc helpers that the PPC backend emits calls to
 *  but that aren't supplied by the system. Currently just one:
 *  __floatundidf (unsigned long long -> double) which gen_cvt_itof1
 *  in tccgen.c reaches for, but Apple's libSystem doesn't export.
 *
 *  Copying and distribution of this file, with or without modification,
 *  are permitted in any medium without royalty provided the copyright
 *  notice and this notice are preserved. This file is offered as-is,
 *  without any warranty.
 */

/* Convert by splitting into two 32-bit unsigned halves so we don't
 * recursively reach for __floatundidf. (uint32 -> double goes through
 * gen_cvt_itof's magic-constant inline expansion, no libcall.) */
double __floatundidf(unsigned long long x)
{
    double hi = (double)(unsigned int)(x >> 32);
    double lo = (double)(unsigned int)x;
    return hi * 4294967296.0 + lo;
}

/* (Earlier sessions defined a __tcc_start_main shim here. It's now
 * obsolete: the tcc -o exe path auto-injects an equivalent helper in
 * tccelf.c::tcc_output_file when crt1.o isn't available, and the
 * gcc-link bootstrap path pulls in the real /usr/lib/crt1.o which
 * does the job properly.) */
