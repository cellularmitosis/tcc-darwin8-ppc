/* Minimal reproducer attempt #2 for seed-732.
 *
 * Found via instrumented seed-732: the bug is in evaluating
 *   (p_9 = ((*l_505) |= g_26.f0))
 * inside func_8, when p_9 is the int8_t first parameter and l_505
 * points to g_359.f0 (a 1-byte union global passed by value as
 * p_11, the middle arg).
 *
 * Before: g_359.f0=-92 (0xA4), g_26.f0=7
 * Expected: *l_505 becomes 0xA7 (0xA4 | 7), p_9 also 0xA7
 * Actual (tcc):  *l_505 becomes 7, p_9 also 7 — looks like *l_505
 * was read as 0 before the OR (or the OR collapsed to plain
 * assignment).
 */
#include <stdio.h>
#include <stdint.h>

union U0 { int8_t f0; };
static union U0 g_26  = {(int8_t)7L};
static union U0 g_359 = {(int8_t)0xA4L};

static int8_t func_8(int8_t p_9, int32_t p_10, union U0 p_11,
                     int32_t *p_12, uint16_t p_13)
{
    int8_t *l_505 = &g_359.f0;
    int8_t r = (p_9 = ((*l_505) |= g_26.f0));
    fprintf(stderr, "INSIDE g_359.f0=%d *l_505=%d p_9=%d r=%d\n",
            (int)g_359.f0, (int)*l_505, (int)p_9, (int)r);
    (void)p_10; (void)p_11; (void)p_12; (void)p_13;
    return r;
}

int main(void) {
    int32_t local = 0;
    fprintf(stderr, "BEFORE g_359.f0=%d g_26.f0=%d\n",
            (int)g_359.f0, (int)g_26.f0);
    int8_t r = func_8((int8_t)1, 2, g_359, &local, 3);
    fprintf(stderr, "AFTER  g_359.f0=%d r=%d\n",
            (int)g_359.f0, (int)r);
    /* Expected: g_359.f0 == 0xA7 (== -89) */
    return 0;
}
