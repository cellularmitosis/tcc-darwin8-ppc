/* v0.2.46-g3 demo — float-to-int constant-fold saturation.
 *
 * Pre-fix, tcc on PowerPC's compile-time float-to-int constant folder
 * went through host `(int64_t)long_double` (= libgcc __fix*di on the
 * build machine), which does NOT saturate to int32 range. So a
 * source like
 *
 *     int x = (int)1e10f;
 *
 * folded to the bottom 32 bits of (int64_t)1e10 = 0x540BE400 =
 * 1410065408, while at runtime (where tcc emits the PPC `fctiwz`
 * instruction) the same expression saturates to 0x7FFFFFFF =
 * INT_MAX. Two tcc paths returned different answers for the same C
 * source. gcc-4.0 and the PPC ABI both saturate (fctiwz semantics).
 *
 * v0.2.46 fixes the const folder to emulate fctiwz when targeting
 * PPC: the int32-target branch now saturates positive overflow to
 * 0x7FFFFFFF, negative overflow / NaN to 0x80000000, and otherwise
 * round-toward-zero. Char/short targets still go through the
 * existing post-mask, which truncates the saturated int32 value.
 *
 * Found via csmith differential testing — seed 92 with
 * `--float --builtins` had a chained `*(int*)p = (...., huge_float)`
 * pattern in func_65 that exposed the divergence at `g_99`.
 */
#include <stdio.h>

static int fail;

static void check_int(const char *what, int got, int expect)
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
    /* Compile-time float constants -> int. The folder runs at compile
     * time; the result is baked into the static initializer / the
     * immediate constant load. PPC fctiwz semantics: saturate. */
    int a = (int)0x7.C870D1p+91f;     /* huge positive single */
    check_int("(int)0x7.C870D1p+91f", a, 0x7FFFFFFF);

    int b = (int)1e10f;
    check_int("(int)1e10f          ", b, 0x7FFFFFFF);

    int c = (int)-1e10f;
    check_int("(int)-1e10f         ", c, (int)0x80000000);

    int d = (int)1e10;                /* huge double */
    check_int("(int)1e10 (double)  ", d, 0x7FFFFFFF);

    int e = (int)100.5f;              /* in-range: round toward zero */
    check_int("(int)100.5f         ", e, 100);

    int f = (int)-100.5f;
    check_int("(int)-100.5f        ", f, -100);

    /* char / short overflow: post-mask truncates the saturated int32.
     * (int8_t)1e10f -> fctiwz -> 0x7FFFFFFF -> & 0xff -> 0xFF -> -1
     * (uint8_t)1e10f -> 0x7FFFFFFF -> & 0xff -> 0xFF -> 255 */
    signed char g = (signed char)1e10f;
    check_int("(int8_t)1e10f       ", g, -1);

    unsigned char h = (unsigned char)1e10f;
    check_int("(uint8_t)1e10f      ", h, 255);

    /* Const-fold result must match runtime (volatile = forces runtime
     * conversion; fctiwz at run-time saturates the same way). */
    volatile float vf = 1e10f;
    int rt = (int)vf;
    check_int("runtime (int)1e10f  ", rt, 0x7FFFFFFF);

    if (fail) {
        printf("FAIL\n");
        return 1;
    }
    printf("All PASS\n");
    return 0;
}
