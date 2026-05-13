/*
 * bswap_compat.h — prototypes for the bswap_compat.c shim functions.
 *
 * gcc-4.0 lacks __builtin_bswap32 / __builtin_bswap64 (added in gcc 4.3)
 * and treats __builtin_ia32_crc32qi as an ordinary extern call. Without
 * a visible prototype, csmith --builtins-emitted call sites fall back
 * to K&R implicit declaration (return int, args promoted), which is
 * ABI-incompatible with the actual 64-bit / 32-bit signatures on PPC32
 * (64-bit args/returns use r3:r4, 32-bit uses r3 only). The ABI mismatch
 * is undefined behavior — both gcc and tcc happen to leave matching
 * garbage in r4 in many cases, but not always (seed-8255 in the session
 * 062 sweep hit a case where they diverge). Declaring the prototypes
 * here forces both compilers to use the correct ABI.
 *
 * This file is the post-v0.2.49 successor to
 * docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h —
 * trimmed to remove the clz/ctz UB-wrapping macros. v0.2.49-g3
 * (tcc/lib/builtin.c) makes tcc's libtcc1.a return gcc-PPC's observed
 * UB values for clz(0) / clzll(0) / ctz(0) / ctzll(0) directly, so
 * the macros are no longer required.
 *
 * -include this header on both the gcc-4.0 and tcc compile lines.
 * Link the matching bswap_compat.c into the binary on both sides.
 */

#ifndef __BSWAP_COMPAT_H_
#define __BSWAP_COMPAT_H_

extern unsigned int __builtin_bswap32(unsigned int x);
extern unsigned long long __builtin_bswap64(unsigned long long x);
extern unsigned int __builtin_ia32_crc32qi(unsigned int crc, unsigned char data);

#endif
