/* Probe: bitfield in struct (not union) — does it have the same BE bug? */
#include <stdio.h>

struct S {
    unsigned f1 : 14;
    unsigned f2 : 14;
};

union U {
    unsigned   f0;
    unsigned f1 : 14;
};

union UU {
    unsigned   f0;
    struct {
        unsigned f1 : 14;
    } s;
};

int main(void)
{
    /* (1) struct member: write+read should round-trip. */
    struct S s; s.f1 = 0x0092; s.f2 = 0x1234;
    printf("STRUCT s.f1=0x%x (expect 0x92)  s.f2=0x%x (expect 0x1234)\n",
           s.f1, s.f2);

    /* (2) union with overlapping uint and bitfield. */
    union U u; u.f0 = 0x024A1329;
    printf("UNION  u.f1=0x%x (expect 0x92 — top 14 bits of 0x024A1329)\n",
           u.f1);

    /* (3) union containing struct of bitfield. */
    union UU uu; uu.f0 = 0x024A1329;
    printf("UU.s.f1=0x%x (expect 0x92)\n", uu.s.f1);

    /* (4) Cast pointer load. */
    unsigned w = 0x024A1329;
    union U *up = (union U *)&w;
    printf("CAST   up->f1=0x%x (expect 0x92)\n", up->f1);
    return 0;
}
