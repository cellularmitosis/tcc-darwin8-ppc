/* Probe TU: holds the global and inspects what got written.
 * Compiled with tcc; if the writer (compiled with tcc) corrupted
 * the store, the actual byte pattern in big.ll won't match the
 * expected long long.
 */
#include <stdio.h>
#include <string.h>

struct Big {
    char pad[0x7ffc];
    long long ll;
    char tail[0x10];
};

struct Big big;

void check(void)
{
    long long expect = 0x123456789abcdef0LL;
    if (big.ll == expect) {
        printf("PASS big.ll=0x%llx (expected 0x%llx)\n",
               big.ll, expect);
    } else {
        printf("FAIL big.ll=0x%llx (expected 0x%llx)\n",
               big.ll, expect);
        /* Dump surrounding bytes to see where the high word landed. */
        int i;
        unsigned char *p = (unsigned char *)&big.ll;
        printf("  &big.ll      = %p\n", p);
        printf("  bytes near .ll: ");
        for (i = -8; i < 16; i++)
            printf("%02x ", p[i]);
        printf("\n");
        /* Look at the last 4 bytes of .pad and after .ll for the misplaced high32. */
        printf("  big.tail[0..7]: ");
        for (i = 0; i < 8; i++)
            printf("%02x ", (unsigned char)big.tail[i]);
        printf("\n");
    }
}
