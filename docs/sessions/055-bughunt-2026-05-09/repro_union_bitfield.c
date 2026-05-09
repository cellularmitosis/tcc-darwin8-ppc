/* Csmith seed-3 reduction: g_55.f1 (14-bit bitfield in a union) reads
 * differently in tcc vs gcc-4.0 on PPC32 BE. */
#include <stdio.h>

union U1 {
    unsigned f0;
    unsigned f1 : 14;
    unsigned f2 : 14;
    int      f3;
};

union U1 g = {0x024A1329};

int main(void)
{
    printf("f0=0x%x f1=0x%x f2=0x%x f3=0x%x\n", g.f0, g.f1, g.f2, g.f3);
    return 0;
}
