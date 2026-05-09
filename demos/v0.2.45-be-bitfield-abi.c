/* v0.2.45-g3 demo — BE bitfield ABI fix.
 *
 * Pre-fix, tcc on PowerPC laid out bitfields LSB-first within each
 * storage container, while gcc-4.0 (and the SysV/AIX PowerPC ABI)
 * lays them out MSB-first on big-endian. This was a silent ABI
 * mismatch: every program that initialized a bitfield-containing
 * struct via a "sibling" union member, a pointer cast, or an
 * external producer (gcc-built code, on-disk data) and then read it
 * via the bitfield path would observe a byte-swapped value.
 *
 * This program exercises three paths that surfaced the bug:
 *   (1) union {uint32_t f0; unsigned f1:14;} — overlap via union
 *   (2) union { ...; struct { unsigned f1:14; }; } — bitfield in nested struct
 *   (3) (union *)&plain_uint — bitfield read via pointer cast
 *
 * On PPC BE, gcc-4.0 reads (val >> 18) & 0x3fff for `unsigned f1:14`
 * sharing storage with `uint32_t f0`. That's the standard MSB-first
 * BE-ABI bitfield layout. tcc now matches.
 */
#include <stdio.h>

union U1 {
    unsigned f0;
    unsigned f1 : 14;
    unsigned f2 : 14;
};

union UU {
    unsigned f0;
    struct {
        unsigned f1 : 14;
    } s;
};

static int fail;

static void check(const char *what, unsigned got, unsigned expect)
{
    if (got != expect) {
        printf("FAIL %s: got 0x%x expect 0x%x\n", what, got, expect);
        fail = 1;
    } else {
        printf("PASS %s: 0x%x\n", what, got);
    }
}

int main(void)
{
    unsigned v = 0x024A1329;
    unsigned expect_msb14 = (v >> 18) & 0x3fff;  /* 0x092 */

    /* Case 1: union with overlapping uint and bitfield. */
    union U1 u;
    u.f0 = v;
    check("union sibling read", u.f1, expect_msb14);

    /* Case 2: union containing struct of bitfield. */
    union UU uu;
    uu.f0 = v;
    check("union->struct->bf", uu.s.f1, expect_msb14);

    /* Case 3: pointer-cast access. */
    unsigned w = v;
    union U1 *up = (union U1 *)&w;
    check("ptr-cast bf read", up->f1, expect_msb14);

    /* Case 4: write via bitfield, read via uint sibling — round-trip. */
    union U1 r;
    r.f0 = 0;
    r.f1 = 0x0092;
    /* Standard BE-ABI: 0x0092 in top 14 bits = 0x0092 << 18 = 0x02480000. */
    check("bf write + sibling read", r.f0, 0x02480000u);

    /* Case 5: a few more values. */
    unsigned vs[] = { 0x80000000, 0xff000000, 0xaaaaaaaa, 0x55555555,
                      0x00040000, 0xffff0000 };
    int i;
    for (i = 0; i < (int)(sizeof(vs)/sizeof(vs[0])); i++) {
        union U1 t;
        t.f0 = vs[i];
        char tag[40];
        int n = 0;
        const char *fmt = "f0=0x";
        while (*fmt) tag[n++] = *fmt++;
        unsigned x = vs[i];
        int j;
        for (j = 28; j >= 0; j -= 4) {
            unsigned digit = (x >> j) & 0xf;
            tag[n++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        }
        tag[n] = 0;
        check(tag, t.f1, (vs[i] >> 18) & 0x3fff);
    }

    if (fail) {
        printf("\n*** FAIL ***\n");
        return 1;
    }
    printf("\nAll %s\n", "PASS");
    return 0;
}
