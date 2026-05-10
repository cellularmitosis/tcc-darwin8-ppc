/*
 * builtin_compat.h — UB-safe wrappers for csmith --builtins on PPC.
 *
 * csmith --builtins emits __builtin_clz/ctz/clzl/ctzl/clzll/ctzll calls
 * with UB inputs (e.g. clz(0)). gcc and tcc both have these as builtins
 * but their UB-handling differs:
 *
 *   gcc-4.0 on PPC:    clz(0)=32, ctz(0)=-1, clzll(0)=64, ctzll(0)=31
 *                       (cntlzw inlines + libgcc helpers)
 *   tcc libtcc1.a:     clz(0)=31, ctz(0)=0, clzll(0)=63, ctzll(0)=0
 *                       (de-Bruijn software impl, no UB special-casing)
 *
 * Both are within their rights per the gcc spec ("If x is 0, the result
 * is undefined"), but the divergence breaks csmith differential testing
 * because csmith doesn't avoid UB-input shapes.
 *
 * Strategy: shadow the builtins with macros that route to a static-inline
 * wrapper. Both compilers inline the wrapper, both reach the underlying
 * builtin via the same path, but the x==0 case is handled in C uniformly.
 *
 * The trick is to define the wrappers BEFORE the macros — that way the
 * preprocessor leaves __builtin_clz(x) inside the wrapper bodies alone
 * (the macro isn't yet visible), and the wrapper bodies compile using
 * each compiler's native builtin. Subsequent uses in the C file (after
 * this header is -included) pick up the macro and route through the
 * wrapper.
 *
 * For x != 0 both compilers produce identical results; for x == 0 the
 * wrapper short-circuits to a deterministic value (matching gcc-4.0's
 * PPC observed behavior, so this stays consistent with what hardware-
 * cntlzw-using PPC code expects).
 *
 * -include this header on both the gcc-4.0 and tcc compile lines.
 */

#ifndef __BUILTIN_COMPAT_H_
#define __BUILTIN_COMPAT_H_

static inline int __compat_clz(unsigned int x) {
    return x ? __builtin_clz(x) : 32;
}
static inline int __compat_clzl(unsigned long x) {
    return x ? __builtin_clzl(x) : 32;  /* long is 32-bit on PPC32 */
}
static inline int __compat_clzll(unsigned long long x) {
    return x ? __builtin_clzll(x) : 64;
}
static inline int __compat_ctz(unsigned int x) {
    return x ? __builtin_ctz(x) : -1;
}
static inline int __compat_ctzl(unsigned long x) {
    return x ? __builtin_ctzl(x) : -1;
}
static inline int __compat_ctzll(unsigned long long x) {
    return x ? __builtin_ctzll(x) : 31;  /* gcc-4.0 PPC observed value */
}

#define __builtin_clz(x)    __compat_clz(x)
#define __builtin_clzl(x)   __compat_clzl(x)
#define __builtin_clzll(x)  __compat_clzll(x)
#define __builtin_ctz(x)    __compat_ctz(x)
#define __builtin_ctzl(x)   __compat_ctzl(x)
#define __builtin_ctzll(x)  __compat_ctzll(x)

#endif
