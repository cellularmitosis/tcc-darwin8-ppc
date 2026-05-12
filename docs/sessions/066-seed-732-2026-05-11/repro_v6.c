/* Minimal r12-clobber reproducer #6.
 *
 * Chain many live pointer writes inside one expression to force the
 * tcc allocator to put a vstack value into r12. Then a global rvalue
 * load (= `lis r12, ha(sym)`) clobbers it.
 *
 * Build: gcc-4.0 -O0 -w repro_v6.c -o v6.gcc
 *        tcc -B... repro_v6.c -o v6.tcc
 * Expected (gcc):   g_a=0xa7
 * Buggy   (tcc):    g_a=0x07
 */
#include <stdio.h>
#include <stdint.h>

static int8_t g_a = (int8_t)0xA4;
static int8_t g_b = 7;

static int16_t s1, s2, s3, s4, s5, s6, s7;
static int16_t *p1 = &s1, *p2 = &s2, *p3 = &s3, *p4 = &s4,
               *p5 = &s5, *p6 = &s6, *p7 = &s7;

int main(void) {
    int8_t *l = &g_a;
    /* Nested assignment chain forces 7+ live pointer values on
     * the vstack, exhausting r3..r10 and pushing the result of
     * `*l` into r12. Then `g_b` is loaded with `lis r12, ha`. */
    (*p1) = (*p2) = (*p3) = (*p4) = (*p5) = (*p6) = (*p7) =
        (int16_t)((*l) |= g_b);
    fprintf(stderr, "g_a=0x%02x\n", (unsigned char)g_a);
    return 0;
}
