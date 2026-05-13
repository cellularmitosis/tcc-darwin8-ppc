/* v0.2.53-g3 — N_BNSYM / N_ENSYM function-body markers
 * (roadmap #7.5 Phase 2, item 1c).
 *
 * Apple's Mach-O stabs convention brackets each N_FUN entry with a
 * paired N_BNSYM (0x2e) / N_ENSYM (0x4e) pair that pins the function
 * body's start and end addresses explicitly. gcc-4.0 emits these;
 * tcc-darwin8-ppc didn't, until v0.2.53.
 *
 * The companion .sh runs `nm -ap` on the linked exe and asserts:
 *   - every function has a BNSYM at its entry address (matching the
 *     opening N_FUN) and an ENSYM at the function-end address;
 *   - the markers are paired (BNSYM count == ENSYM count);
 *   - all BNSYM/ENSYM addresses fall inside __TEXT;
 *   - the existing Phase 1 / Phase 2-1a gdb workflows are unaffected
 *     (regression check, since BNSYM/ENSYM is convention-level
 *     metadata rather than an observable Tiger-gdb-6.3 behavior fix).
 *
 * The functions below have varying sizes so the script can verify
 * BNSYM/ENSYM addresses straddle each function body correctly.
 */
#include <stdio.h>

int tiny(int x)
{
    return x + 1;
}

int small(int a, int b)
{
    int s = a + b;
    return s * 2;
}

int medium(int p, int q, int r)
{
    int t1 = p + q;
    int t2 = t1 + r;
    int t3 = t2 * 2;
    int t4 = t3 - p;
    return t4;
}

static int stat_helper(int n)
{
    int acc = 0;
    int i;
    for (i = 1; i <= n; i++)
        acc += i;
    return acc;
}

int main(void)
{
    printf("tiny=%d small=%d medium=%d stat=%d\n",
           tiny(10), small(3, 4), medium(1, 2, 3), stat_helper(5));
    return 0;
}
