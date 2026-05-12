/* Minimal reproducer attempt #1 for seed-732.
 *
 * seed-732 fails: g_359.f0 ends up wrong in memory after func_1 runs.
 * Suspect-shape (per session-065 HANDOFF): union U0 {int8_t f0;}
 * passed by value as a MIDDLE arg (not last) to func_8.
 *
 * Hypothesis A: the struct word load `lwz r5, 0(r12)` for the
 * 1-byte union arg overshoots — it reads 4 bytes, the next 3 bytes
 * are "after" the union in memory. If g_359 lives in a 4-byte slot
 * and adjacent bytes are part of another global, this is benign for
 * the callee (it only reads p_11.f0 = byte 0). But maybe tcc has a
 * bug where the high bits get written back, or there's an aliasing
 * issue.
 *
 * Hypothesis B: prep for callee's struct param reuses something —
 * maybe arg eval order interleaves a write to g_359.f0 with the
 * read for passing g_359 by value, leading to one being lost.
 *
 * This is a tight repro: pass g_359 by value as middle arg, then
 * also write g_359.f0 from within the callee or from a sibling
 * call. See if g_359.f0 in main differs gcc vs tcc.
 */
#include <stdio.h>
#include <stdint.h>

union U0 {
    int8_t f0;
};

/* Mimic some surrounding globals to give g_359 something to be
 * adjacent to. */
static int8_t  pad_before = 0;
static union U0 g_359 = {(int8_t)0xA4L};
static int8_t  pad_after = 0;

/* Mimic func_8: union U0 by value as middle arg, returns ptr. */
static int32_t dummy = 0;
static int32_t *func_8(int8_t p_9, int32_t p_10, union U0 p_11,
                       int32_t *p_12, uint16_t p_13)
{
    /* Inside, write to g_359.f0 (mimics what func_8 does in seed). */
    g_359.f0 = (int8_t)(p_11.f0 + 1);
    (void)p_9; (void)p_10; (void)p_12; (void)p_13;
    return &dummy;
}

int main(void) {
    int32_t local_int = 42;
    int8_t  a = 1;
    int32_t b = 2;
    uint16_t e = 3;

    printf("BEFORE: g_359.f0 = %d (= 0x%02x)\n",
           (int)g_359.f0, (unsigned char)g_359.f0);

    int32_t *r = func_8(a, b, g_359, &local_int, e);
    (void)r;

    printf("AFTER:  g_359.f0 = %d (= 0x%02x)\n",
           (int)g_359.f0, (unsigned char)g_359.f0);
    /* gcc-expected: AFTER = 0xA5 (was 0xA4, +1 inside callee).
     * If tcc misbehaves with middle-arg union, the value differs. */
    return 0;
}
