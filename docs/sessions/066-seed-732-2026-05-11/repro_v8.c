/* repro #8: chain 9 simultaneously-live lvalue pointers through a
 * single nested assignment expression. The lvalues consume r3..r11
 * (9 GPRs); the |= operation's loaded *p1 byte then has to land in
 * r12. The rhs `g_b.f0` is a global, so its load emits
 * `lis r12, ha(g_b); lbz rD, lo(g_b)(r12)` — clobbering r12.
 *
 * Expected gcc:  g_a=0xa7  (= 0xa4 | 7)
 * Buggy pre-fix tcc: g_a=0x07  (lost the lhs byte)
 * Post-fix tcc:  g_a=0xa7  (matches gcc)
 */
#include <stdio.h>
#include <stdint.h>

union U0 { int8_t f0; };
static union U0 g_a = {(int8_t)0xA4};
static union U0 g_b = {(int8_t)7};
static int8_t s1, s2, s3, s4, s5, s6, s7, s8;

int main(void) {
    int8_t *p1 = &g_a.f0;
    int8_t *p2 = &s1, *p3 = &s2, *p4 = &s3, *p5 = &s4,
           *p6 = &s5, *p7 = &s6, *p8 = &s7, *p9 = &s8;
    /* Single huge assignment chain. */
    (*p9) = (*p8) = (*p7) = (*p6) = (*p5) = (*p4) = (*p3) = (*p2) =
        ((*p1) |= g_b.f0);
    fprintf(stderr, "g_a=0x%02x  s1=0x%02x s8=0x%02x\n",
            (unsigned char)g_a.f0,
            (unsigned char)s1, (unsigned char)s8);
    return 0;
}
