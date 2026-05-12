/* repro #4: progressively add surrounding nested-assignment context. */
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

union U0 { int8_t f0; };
static union U0 g_26  = {(int8_t)7L};
static union U0 g_359 = {(int8_t)0xA4L};
static int16_t g_185[5][3];
static int16_t *g_184 = &g_185[0][2];
static int16_t g_55 = 0;
static int16_t *l_521 = &g_55;
static int16_t g_523[4][5];
static int16_t *l_522 = &g_523[1][4];
static int64_t g_355 = 0;
static int64_t *l_524 = &g_355;

#define safe_lshift_func_int8_t_s_s(_left,_right) \
  ({ int8_t left = (_left); int right = (_right) ; \
   ((((int8_t)(left)) < ((int8_t)0)) \
  || (((int)(right)) < ((int8_t)0)) \
  || (((int)(right)) >= sizeof(int8_t)*CHAR_BIT) \
  || (((int8_t)(left)) > ((INT8_MAX) >> ((int)(right))))) \
  ? ((int8_t)(left)) \
  : (((int8_t)(left)) << ((int)(right)));})

static int8_t func_8(int8_t p_9, int32_t p_10, union U0 p_11,
                     int32_t *p_12, uint16_t p_13)
{
    int8_t *l_505 = &g_359.f0;
    fprintf(stderr, "BEFORE g_359.f0=%d *l_505=%d\n",
            (int)g_359.f0, (int)*l_505);
    /* Mimic the structure from line 467:
     *   (*l_522) = (*l_521) = (*g_184) = (lshift(p_9 = (*l_505 |= g_26.f0), p_10)) && X
     * where X is a "long thing". */
    int8_t result;
    (void)(((*l_522) = ((*l_521) = ((*g_184) =
         ((safe_lshift_func_int8_t_s_s((p_9 = ((*l_505) |= g_26.f0)), p_10))
         && (p_13))))) ^ p_11.f0);
    result = p_9;
    fprintf(stderr, "INSIDE g_359.f0=%d *l_505=%d p_9=%d\n",
            (int)g_359.f0, (int)*l_505, (int)p_9);
    (void)result; (void)p_12;
    return p_9;
}

int main(void) {
    int32_t local = 0;
    int8_t r = func_8((int8_t)1, 2, g_359, &local, 3);
    fprintf(stderr, "AFTER  g_359.f0=%d r=%d\n", (int)g_359.f0, (int)r);
    return 0;
}
