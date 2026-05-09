/* Test gcc-4.0 vs tcc on __builtin_clzll, __builtin_popcountll. */
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    /* clzll */
    printf("clzll(1)         = %d\n", __builtin_clzll(1ULL));
    printf("clzll(0xff)      = %d\n", __builtin_clzll(0xffULL));
    printf("clzll(0xffffffff)= %d\n", __builtin_clzll(0xffffffffULL));
    printf("clzll(1ULL<<32)  = %d\n", __builtin_clzll(1ULL << 32));
    printf("clzll(1ULL<<63)  = %d\n", __builtin_clzll(1ULL << 63));

    /* popcountll */
    printf("popcountll(0)        = %d\n", __builtin_popcountll(0ULL));
    printf("popcountll(1)        = %d\n", __builtin_popcountll(1ULL));
    printf("popcountll(0xff)     = %d\n", __builtin_popcountll(0xffULL));
    printf("popcountll(0xffffffff)= %d\n", __builtin_popcountll(0xffffffffULL));
    printf("popcountll(0xffffffffffffffff) = %d\n", __builtin_popcountll(~0ULL));
    printf("popcountll(0xaaaaaaaaaaaaaaaa) = %d\n", __builtin_popcountll(0xaaaaaaaaaaaaaaaaULL));

    return 0;
}
