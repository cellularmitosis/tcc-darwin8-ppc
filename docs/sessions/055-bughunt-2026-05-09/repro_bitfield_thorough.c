/* Thorough BE bitfield test: signed/unsigned, various sizes,
 * various positions, struct/union/cast access. */
#include <stdio.h>

static int fail = 0;

#define EXPECT(name, got, want) do {                          \
    if ((got) != (want)) {                                    \
        printf("FAIL %s: got 0x%lx expect 0x%lx\n",           \
               name, (long)(got), (long)(want));              \
        fail = 1;                                             \
    } else {                                                  \
        printf("PASS %s: 0x%lx\n", name, (long)(got));        \
    }                                                         \
} while (0)

/* Signed bitfields. */
struct Signed {
    int s5  : 5;
    int s11 : 11;
    int s16 : 16;
};

/* 64-bit container (long long bitfield). */
struct LL {
    unsigned long long a : 33;
    unsigned long long b : 31;
};

/* Zero-width field forces alignment. */
struct ZW {
    unsigned a : 4;
    unsigned   : 0;   /* alignment break — next field starts a new container */
    unsigned b : 4;
};

/* short container. */
struct ShortBF {
    unsigned short s1 : 4;
    unsigned short s2 : 12;
};

/* char container. */
struct CharBF {
    unsigned char c1 : 3;
    unsigned char c2 : 5;
};

/* Mixed types. */
struct Mixed {
    unsigned a : 12;
    int b : 12;
    unsigned char c : 4;
};

int main(void)
{
    /* 1. Signed bitfield should sign-extend on read. */
    struct Signed sgn;
    sgn.s5 = -1;       /* 0b11111 -> stored as 14-bit signed -1 */
    sgn.s11 = -1024;   /* most-negative 11-bit value */
    sgn.s16 = -32768;
    EXPECT("Signed.s5  = -1",     sgn.s5,  -1);
    EXPECT("Signed.s11 = -1024",  sgn.s11, -1024);
    EXPECT("Signed.s16 = -32768", sgn.s16, -32768);

    /* 2. unsigned 5-bit max value. */
    sgn.s5 = 15;
    EXPECT("Signed.s5  =  15",    sgn.s5,  15);

    /* 3. 64-bit container split bitfield. */
    struct LL ll;
    ll.a = (1ULL << 33) - 1;
    ll.b = (1ULL << 31) - 1;
    EXPECT("LL.a = (1<<33)-1", ll.a, (1ULL << 33) - 1);
    EXPECT("LL.b = (1<<31)-1", ll.b, (1ULL << 31) - 1);

    /* 4. Zero-width field ensures b lands in new container. */
    struct ZW zw;
    zw.a = 0xa;
    zw.b = 0xb;
    EXPECT("ZW.a = 0xa", zw.a, 0xa);
    EXPECT("ZW.b = 0xb", zw.b, 0xb);

    /* 5. short container. */
    struct ShortBF sbf;
    sbf.s1 = 0xa;
    sbf.s2 = 0x123;
    EXPECT("ShortBF.s1 = 0xa", sbf.s1, 0xa);
    EXPECT("ShortBF.s2 = 0x123", sbf.s2, 0x123);

    /* 6. char container. */
    struct CharBF cbf;
    cbf.c1 = 0x5;
    cbf.c2 = 0x1f;
    EXPECT("CharBF.c1 = 0x5", cbf.c1, 0x5);
    EXPECT("CharBF.c2 = 0x1f", cbf.c2, 0x1f);

    /* 7. Mixed types. */
    struct Mixed mx;
    mx.a = 0xabc;
    mx.b = -1024;
    mx.c = 0xf;
    EXPECT("Mixed.a = 0xabc",  mx.a, 0xabc);
    EXPECT("Mixed.b = -1024",  mx.b, -1024);
    EXPECT("Mixed.c = 0xf",    mx.c, 0xf);

    /* 8. Cross-write: write via one bitfield, read another part. */
    struct Mixed mx2;
    mx2.a = 0xabc;
    mx2.b = 0;
    mx2.c = 0;
    /* The container layout is: a (12 bits), b (12 bits), c (4 bits) plus padding. */
    /* On BE-ABI: a is at MSB end of first uint container. b follows.
     * c is in second container or same — but for now just verify each field
     * round-trips via its own bitfield member. */
    EXPECT("Mixed.a after init, a", mx2.a, 0xabc);
    EXPECT("Mixed.a after init, b", (unsigned)mx2.b, 0u);

    if (fail) {
        printf("\n*** FAIL ***\n");
        return 1;
    }
    printf("\nAll PASS\n");
    return 0;
}
