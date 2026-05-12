/* repro #7: nested-assignment chain mirroring seed-732's line-467
 * structure: 3 simultaneous lvalue pointers + 2 global rvalue reads
 * keep r3..r11 busy so the int8_t *l_505 lhs of `|= g_26.f0` lands
 * in r12. Then the load of g_26.f0 (`lis r12, ha`) clobbers it.
 */
#include <stdio.h>
#include <stdint.h>

union U0 { int8_t f0; };
static union U0 g_a = {(int8_t)0xA4};
static union U0 g_b = {(int8_t)7};
static int16_t g_x[3] = {0, 0, 0};
static int16_t *g_xp = &g_x[2];
static int64_t g_y = 0;
static int16_t g_z = 0;

static int8_t func(int8_t p9, int32_t p10, union U0 p11,
                   int32_t *p12, uint16_t p13)
{
    int8_t *l_505 = &g_a.f0;          /* int8_t* alias to global */
    int16_t *l_521 = &g_z;            /* int16_t* alias */
    int16_t *l_522 = &g_x[0];         /* int16_t* alias */
    int64_t *l_524 = &g_y;            /* int64_t* alias (live but unused for write) */
    int8_t l_510 = (int8_t)0xC4;      /* live int8 local */
    (void)l_524; (void)l_510;
    /* This is the failing shape from seed-732 line 467 (heavily
     * trimmed but preserving the chain *l_522 = *l_521 = *g_xp =
     * (lshift(p9 = (*l_505 |= g_b.f0), p10) && X)). */
    int16_t outer = (int16_t)(
        ((*l_522) = ((*l_521) = ((*g_xp) =
            (int16_t)((p9 = (int8_t)((*l_505) |= g_b.f0)) && p13))))
        ^ p11.f0);
    fprintf(stderr, "INSIDE g_a.f0=0x%02x *l_505=0x%02x p9=0x%02x outer=%d\n",
            (unsigned char)g_a.f0, (unsigned char)*l_505,
            (unsigned char)p9, (int)outer);
    (void)p10; (void)p12;
    return p9;
}

int main(void) {
    int32_t local = 0;
    fprintf(stderr, "BEFORE g_a.f0=0x%02x g_b.f0=0x%02x\n",
            (unsigned char)g_a.f0, (unsigned char)g_b.f0);
    int8_t r = func((int8_t)1, 2, g_a, &local, 3);
    fprintf(stderr, "AFTER  g_a.f0=0x%02x r=0x%02x\n",
            (unsigned char)g_a.f0, (unsigned char)r);
    return 0;
}
