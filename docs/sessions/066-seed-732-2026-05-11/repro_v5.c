/* Minimal r12-clobber reproducer.
 *
 * Tcc's load() for VT_CONST | VT_SYM | VT_LVAL (a global rvalue
 * read) hardcodes r12 as the scratch register that holds the
 * symbol's HA-half address (`lis r12, ha(sym)` then `lbz/lwz rD,
 * lo(sym)(r12)`). r12 is *also* in tcc's reg allocator (TREG slot
 * 9), so a prior get_reg() may have placed a live value in r12 —
 * and that value gets destroyed by the next `lis r12, ha`.
 *
 * To force tcc to allocate r12 for a vstack entry we need to spill
 * r3..r11 with other live values. Once r12 is in use, evaluating a
 * second global rvalue clobbers it.
 *
 * Concrete shape that triggers it: `*p_ptr |= g_byte;` where p_ptr
 * is an int8_t pointer (so the lhs load goes through gv(RC_INT) and
 * stays in whatever reg the allocator gives), AND enough surrounding
 * pressure that r12 is the chosen reg.
 *
 * Run with gcc and tcc; expected output (gcc): "g_a=A7". A buggy
 * tcc produces something like 7 or 0x20007.
 */
#include <stdio.h>
#include <stdint.h>

static int8_t g_a = (int8_t)0xA4;  /* the byte whose |= gets corrupted */
static int8_t g_b = 7;

/* 10 unused integer globals to bait the allocator into needing
 * many GPRs simultaneously. */
static int u1 = 1, u2 = 2, u3 = 3, u4 = 4, u5 = 5,
           u6 = 6, u7 = 7, u8 = 8, u9 = 9, u10 = 10;

static int8_t bug(int8_t *p)
{
    /* Build a hairy expression with many live temporaries so r12
     * ends up holding a vstack value. The `*p |= g_b` is the
     * statement whose result must equal (old_*p | g_b). */
    int s = u1 + u2 + u3 + u4 + u5 + u6 + u7 + u8 + u9 + u10;
    int8_t r = ((*p) |= g_b);
    return (int8_t)(r + (int8_t)(s & 1));  /* keep s live */
}

int main(void) {
    int8_t r = bug(&g_a);
    fprintf(stderr, "g_a=0x%02x r=0x%02x\n",
            (unsigned char)g_a, (unsigned char)r);
    return 0;
}
