/* Minimal repro for the extern *data* reference static-init bug
 * deferred from session 044.
 *
 * On Tiger `errno` is `(*__error())` (a function), so the roadmap's
 * `int *p = &errno;` shorthand can't actually be compiled — but
 * `_optind` from <unistd.h> is a real `int` data symbol in
 * libSystem (`a000056c D _optind`), so it makes a clean test.
 *
 * Pre-fix expected behavior with tcc: `p` ends up holding the
 * address of `__nl_symbol_ptr[optind]` (a slot in this exe's
 * pointer table), not `&optind` (the libSystem variable). So:
 *
 *   - `p != &optind`
 *   - `*p` reads the dyld-filled slot value (which IS `&optind`)
 *     interpreted as an int — looks like a high VA
 *   - assigning optind = 7; does NOT make `*p` read back 7.
 *
 * Post-fix expected:
 *   - `p == &optind`
 *   - `*p == 7` after `optind = 7`.
 *
 * gcc-4.0 prints PASS for the same source. tcc currently prints FAIL.
 */
#include <unistd.h>
#include <stdio.h>

extern int optind;
int *p = &optind;

int main(void) {
    optind = 7;
    printf("&optind = %p\n", (void *)&optind);
    printf("p       = %p\n", (void *)p);
    printf("optind  = %d\n", optind);
    printf("*p      = %d\n", *p);
    if (p == &optind && *p == 7) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
