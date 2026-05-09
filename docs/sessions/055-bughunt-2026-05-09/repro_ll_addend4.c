/* Repro for the LLONG addend+4 PIC-store overflow noted in
 * session-054 HANDOFF.
 *
 * Storing a long long via PIC into &extern_struct.member where the
 * member offset is in [0x7ffc, 0x7fff]. After ppc_adjust_extra_off
 * brings addend into 15-bit signed range, addend+4 sign-extends to
 * negative — the high word lands at the wrong VA.
 *
 * Compile with tcc:  tcc -B./tcc -L./tcc -I./tcc/include -o repro repro.c probe.c
 * Run: ./repro  -> if buggy, prints "FAIL".
 */

#include <stdio.h>
#include <string.h>

/* Defined in probe.c so we get a real PIC reference. */
extern struct Big big;
extern void check(void);

/* Mirrored type — the writer side. */
struct Big {
    char pad[0x7ffc];
    long long ll;
    char tail[0x10];
};

int main(void)
{
    big.ll = 0x123456789abcdef0LL;
    check();
    return 0;
}
