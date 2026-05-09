/* Probe union-bitfield read on PPC BE with multiple values. */
#include <stdio.h>

union U1 {
    unsigned f0;
    unsigned f1 : 14;
    unsigned f2 : 14;
};

void show(unsigned v)
{
    union U1 g;
    g.f0 = v;
    /* Expected on BE: f1 = (v >> 18) & 0x3fff = top 14 bits. */
    printf("f0=0x%08x  expect_f1=0x%04x  got_f1=0x%04x  (%s)\n",
           v, (v >> 18) & 0x3fff, g.f1,
           ((v >> 18) & 0x3fff) == g.f1 ? "OK" : "BUG");
}

int main(void)
{
    show(0x024A1329);
    show(0x80000000);
    show(0x00000001);
    show(0xffffffff);
    show(0xfffffffe);
    show(0xfffe0000);
    show(0xffff0000);
    show(0xff000000);
    show(0xaaaaaaaa);
    show(0x55555555);
    show(0x00040000);  /* one bit at LSB of bitfield */
    show(0x80000000);  /* one bit at MSB of bitfield */
    return 0;
}
