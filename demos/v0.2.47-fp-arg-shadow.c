/* v0.2.47-g3 demo — FP-arg GPR-shadow no longer clobbers a previously
 * computed int arg.
 *
 * Pre-fix, tcc's gfunc_call on PPC populated the variadic GPR shadow
 * for an FP arg by emitting `lwz r{gslot+3}, 16(r1)` directly (after
 * `stfs/stfd` of the FPR), without first calling save_reg(). If an
 * earlier-evaluated int arg's value happened to live in that target
 * shadow GPR, the shadow load clobbered it. Pass 2 of arg setup then
 * read the corrupted register and emitted `mr r3, r{shadow}` — putting
 * the float bit pattern in r3 where the int arg should have been.
 *
 * Specifically: a call shape like
 *
 *     f((x &= helper(...)), 5.0f)
 *
 * with `f` having signature `(unsigned, float)` lands the `&=` result
 * in r4 (since the helper call clobbered r3 with its return). When
 * pass 2 then materializes 5.0f into f1 and does `lwz r4, 16(r1)`
 * for the variadic shadow, r4's int value is lost; the subsequent
 * `mr r3, r4` copies float bits to r3. printf in the callee shows
 * `a=1084227584` (= 0x40A00000 = float 5.0 bit pattern) instead of 0.
 *
 * Found via csmith --float seed 3228 (--max-funcs 6 --max-block-depth 3).
 *
 * Fix: call save_reg(gslot) (and gslot+1 for VT_DOUBLE both-in-GPR)
 * before the shadow lwz. Mirrors what the LL straddle / both-in-GPR
 * paths already do.
 */
#include <stdio.h>

static int fail;

static unsigned x;

static unsigned helper(int a, int b, int c, int d) {
    /* takes 4 args so the call uses r3..r6, returns 0 in r3 */
    return 0;
}

static void f_int_float(const char *tag, unsigned expect, unsigned a, float b) {
    if (a != expect || b != 5.0f) {
        printf("FAIL %s: got a=%u (expect %u), b=%f\n",
               tag, a, expect, (double)b);
        fail = 1;
        return;
    }
    printf("PASS %s: a=%u b=%f\n", tag, a, (double)b);
}

static void f_int_double(const char *tag, unsigned expect, unsigned a, double b) {
    if (a != expect || b != 5.0) {
        printf("FAIL %s: got a=%u (expect %u), b=%f\n",
               tag, a, expect, b);
        fail = 1;
        return;
    }
    printf("PASS %s: a=%u b=%f\n", tag, a, b);
}

static void f_int_float_float(const char *tag, unsigned expect,
                              unsigned a, float b, float c) {
    if (a != expect || b != 5.0f || c != 6.0f) {
        printf("FAIL %s: got a=%u (expect %u), b=%f, c=%f\n",
               tag, a, expect, (double)b, (double)c);
        fail = 1;
        return;
    }
    printf("PASS %s: a=%u b=%f c=%f\n", tag, a, (double)b, (double)c);
}

int main(void)
{
    unsigned char c5 = 5;

    /* &= 0 -> 0 */
    x = 7; f_int_float("(x &= helper) , 5.0f       ", 0,
                       (x &= helper(1, 2, 3, 4)), 5.0f);

    /* &= 0 -> 0, with uint8_t->float promotion of arg2 */
    x = 7; f_int_float("(x &= helper) , (uint8_t)5 ", 0,
                       (x &= helper(1, 2, 3, 4)), c5);

    /* ^= 0 -> unchanged (7) */
    x = 7; f_int_float("(x ^= helper) , 5.0f       ", 7,
                       (x ^= helper(1, 2, 3, 4)), 5.0f);

    /* |= 0 -> unchanged (7) */
    x = 7; f_int_float("(x |= helper) , 5.0f       ", 7,
                       (x |= helper(1, 2, 3, 4)), 5.0f);

    /* += 0 -> unchanged (3) */
    x = 3; f_int_float("(x += helper) , 5.0f       ", 3,
                       (x += helper(1, 2, 3, 4)), 5.0f);

    /* -= 0 -> unchanged (3) */
    x = 3; f_int_float("(x -= helper) , 5.0f       ", 3,
                       (x -= helper(1, 2, 3, 4)), 5.0f);

    /* VT_DOUBLE shadow: both halves of the double's GPR shadow get
     * loaded; both targets need save_reg. */
    x = 7; f_int_double("(x &= helper) , 5.0 (dbl)  ", 0,
                        (x &= helper(1, 2, 3, 4)), 5.0);

    /* Two FP args after the int — first FP arg's shadow GPR is the
     * one we worried about, but second FP arg's shadow uses a
     * different GPR; both need correct save_reg behavior. */
    x = 7; f_int_float_float("(x &= helper) , 5f, 6f     ", 0,
                             (x &= helper(1, 2, 3, 4)), 5.0f, 6.0f);

    if (fail) {
        printf("FAIL\n");
        return 1;
    }
    printf("All PASS\n");
    return 0;
}
