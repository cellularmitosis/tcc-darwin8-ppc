/* Minimal reproducer for the gfunc_call struct-arg-clobbers-prior-arg
 * bug surfaced by csmith seeds 1536 / 1705 / 8271 / 8389.
 *
 * Pre-fix: tcc emits `mr r3, r5` immediately before the bl, where r5
 * has just been loaded with the high half of l_46[1] (= 0xF0000000
 * because BE PPC + MSB-first bitfield packing puts f0:4 = -1 in the
 * top nibble). r3 — which should hold arg1 = &g_27 — gets clobbered.
 *
 * Post-fix: tcc spills r3..r10 vstack entries before loading struct
 * words into ABI GPR slots, so arg1's pointer survives.
 *
 * Verification:
 *   gcc-4.0  -O0 test_arg_clobber.c   ; ./a.out  -> "result=1"
 *   tcc      -w  test_arg_clobber.c   ; ./a.out
 *     - pre-fix: SIGSEGV at *p_41 deref (p_41 = 0xF0000000)
 *     - post-fix: "result=1" (matches gcc)
 */
#include <stdio.h>
#include <stdint.h>

union U2 {
    signed f0 : 4;
    int64_t f1;
    int16_t f2;
    int16_t f3;
    const int32_t f4;
};

int g_27 = 0xC0DE;
int *l_44 = &g_27;
int **l_45 = &l_44;
int16_t l_46_target = 0;
int16_t *l_246 = &l_46_target;

int func_40(int *p_41, int p_42, union U2 p_43)
{
    printf("func_40: p_41=%p p_42=%d p_43.f1=%llx\n",
           (void*)p_41, p_42, (long long)p_43.f1);
    if (p_41) printf("*p_41=%x\n", *p_41);
    return 1;
}

int main(void) {
    int p_24v = 42;
    int *p_24 = &p_24v;
    union U2 l_46[4] = {{-1L},{-1L},{-1L},{-1L}};
    int r = ((*l_246) = func_40(((*l_45) = l_44), (*p_24), l_46[1]));
    printf("result=%d\n", r);
    return 0;
}
