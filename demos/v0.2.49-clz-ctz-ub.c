/* v0.2.49-g3 demo — libtcc1.a __builtin_clz/ctz on x==0 matches gcc-PPC.
 *
 * The gcc spec says __builtin_clz(0) / __builtin_ctz(0) / their long
 * and long-long siblings are *undefined behavior* — each compiler
 * picks something. On PowerPC:
 *
 *   gcc-4.0 -O0 on PPC (cntlzw inline + libgcc __ctzdi2):
 *     clz(0)   = 32   ctz(0)   = -1
 *     clzll(0) = 64   ctzll(0) = 31
 *
 *   tcc libtcc1.a, upstream de-Bruijn implementation (pre-v0.2.49):
 *     clz(0)   = 31   ctz(0)   = 0
 *     clzll(0) = 63   ctzll(0) = 0
 *
 * Both are "within their rights" per the spec, but the divergence
 * was a recurring nuisance for csmith differential testing — csmith
 * --builtins emits clz(0)/ctz(0) patterns freely and the two
 * compilers' programs diverged. Session 062 papered over it with a
 * UB-wrapper header (`builtin_compat.h`) that we -included on both
 * sides.
 *
 * v0.2.49 fixes the actual source: tcc/lib/builtin.c's clz/clzll/ctz/
 * ctzll function bodies now short-circuit x==0 to the gcc-PPC values
 * before falling through to the de-Bruijn lookup. clrsb() still uses
 * the bare CLZI/CLZL macros — clrsb(0) must return 31/63 per the
 * (well-defined) spec, and the unmodified macros already do.
 *
 * Verified end-to-end by this demo, which prints each value next to
 * its gcc-PPC reference and asserts equality.
 */
#include <stdio.h>

static int fail;

static void check(const char *what, int got, int expect)
{
    if (got != expect) {
        printf("FAIL %s: got %d expect %d\n", what, got, expect);
        fail = 1;
    } else {
        printf("PASS %s: %d\n", what, got);
    }
}

int main(void)
{
    /* The UB cases — the whole point of this demo. */
    unsigned int u32_0 = 0;
    unsigned long u_0 = 0UL;
    unsigned long long u64_0 = 0ULL;

    check("__builtin_clz  (0)            ", __builtin_clz(u32_0),     32);
    check("__builtin_clzl (0)            ", __builtin_clzl(u_0),      32);
    check("__builtin_clzll(0)            ", __builtin_clzll(u64_0),   64);
    check("__builtin_ctz  (0)            ", __builtin_ctz(u32_0),     -1);
    check("__builtin_ctzl (0)            ", __builtin_ctzl(u_0),      -1);
    check("__builtin_ctzll(0)            ", __builtin_ctzll(u64_0),   31);

    /* Defined-behavior smoke: non-zero inputs unchanged. */
    check("__builtin_clz  (1)            ", __builtin_clz(1u),                 31);
    check("__builtin_clz  (0xFFFFFFFF)   ", __builtin_clz(0xFFFFFFFFu),         0);
    check("__builtin_ctz  (1)            ", __builtin_ctz(1u),                  0);
    check("__builtin_ctz  (0x80000000)   ", __builtin_ctz(0x80000000u),        31);
    check("__builtin_clzll(1)            ", __builtin_clzll(1ULL),             63);
    check("__builtin_clzll(~0ULL)        ", __builtin_clzll(~0ULL),             0);
    check("__builtin_ctzll(1)            ", __builtin_ctzll(1ULL),              0);
    check("__builtin_ctzll(1ULL << 63)   ", __builtin_ctzll(1ULL << 63),       63);

    /* clrsb(0) must still be 31/63 (well-defined per the gcc spec —
     * 0 has the MSB-as-sign and 31/63 following bits identical to it). */
    check("__builtin_clrsb  (0)          ", __builtin_clrsb((int)0),           31);
    check("__builtin_clrsbll(0)          ", __builtin_clrsbll((long long)0),   63);
    check("__builtin_clrsb  (-1)         ", __builtin_clrsb(-1),               31);
    check("__builtin_clrsbll(-1LL)       ", __builtin_clrsbll(-1LL),           63);

    if (fail) {
        printf("FAIL\n");
        return 1;
    }
    printf("All PASS\n");
    return 0;
}
