/* repro #3: use the actual safe_lshift_func_int8_t_s_s statement-expression
 * form, which is how seed-732 wraps the |= expression. */
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

union U0 { int8_t f0; };
static union U0 g_26  = {(int8_t)7L};
static union U0 g_359 = {(int8_t)0xA4L};

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
    int8_t r = safe_lshift_func_int8_t_s_s((p_9 = ((*l_505) |= g_26.f0)), p_10);
    fprintf(stderr, "INSIDE g_359.f0=%d *l_505=%d p_9=%d r=%d\n",
            (int)g_359.f0, (int)*l_505, (int)p_9, (int)r);
    (void)p_11; (void)p_12; (void)p_13;
    return r;
}

int main(void) {
    int32_t local = 0;
    int8_t r = func_8((int8_t)1, 2, g_359, &local, 3);
    fprintf(stderr, "AFTER  g_359.f0=%d r=%d\n", (int)g_359.f0, (int)r);
    return 0;
}
