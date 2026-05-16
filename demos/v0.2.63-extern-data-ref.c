/* Demo for v0.2.63-g3 — extern *data* static-init references
 * resolve to the dyld-bound address (not to the __nl_symbol_ptr
 * slot's address).
 *
 * Before v0.2.63, `int *p = &optind;` produced a `p` that
 * pointed at __nl_symbol_ptr[optind] (a 4-byte data slot in
 * the binary's pointer table) rather than at the real
 * libSystem `optind`. After: dyld processes a Mach-O external
 * relocation in __LINKEDIT and writes &optind directly into
 * `p` at load time, matching gcc-4.0 behavior.
 *
 * `_optind` is `a000056c D _optind` in libSystem.B.dylib on
 * Tiger PPC — a real `int` data symbol, perfect for the test.
 */
#include <unistd.h>
#include <stdio.h>

extern int optind;
int *p = &optind;
int *q = &optind + 3;     /* nonzero addend; dyld VANILLA externs ADD */

int main(void) {
    int errs = 0;
    unsigned long want_q = (unsigned long)&optind + 3 * sizeof(int);

    optind = 7;
    printf("&optind     = %p\n", (void *)&optind);
    printf("p           = %p\n", (void *)p);
    printf("q           = %p\n", (void *)q);
    printf("want q      = 0x%lx\n", want_q);
    printf("optind = 7;\n");
    printf("*p          = %d (want 7)\n", *p);

    if (p != &optind)               { puts("FAIL: p != &optind"); errs++; }
    if (*p != 7)                    { puts("FAIL: *p != 7");      errs++; }
    if ((unsigned long)q != want_q) { puts("FAIL: q wrong");      errs++; }

    if (errs == 0) {
        puts("PASS");
        return 0;
    }
    return errs;
}
