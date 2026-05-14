/*
 * v0.2.56-n-oso-dsymutil.c — main TU
 *
 * Showcase for roadmap #7.5 Phase 2 item 1b: N_OSO debug-map records
 * + dsymutil-on-Tiger consumption.  Linked with v0.2.56-n-oso-dsymutil-helpers.c.
 *
 * Each TU contributes one function.  Under -gstabs, the linked exe's
 * LC_SYMTAB carries the classic file:line stab chain plus the new
 * N_OSO record after each TU's filename-N_SO (gcc-4.0's exact
 * format).  Under -gdwarf-2, the same chain is synthesized at link
 * time with N_BNSYM/N_FUN/N_FUN(size)/N_ENSYM markers per function so
 * dsymutil can map exe-resident function addresses back to the .o's
 * DWARF subprograms when consolidating a .dSYM bundle.
 */

#include <stdio.h>

extern int helper_add(int a, int b);
extern int helper_mul(int a, int b);

int main(int argc, char **argv) {
    int s = helper_add(3, 4);
    int p = helper_mul(s, 5);
    printf("sum=%d prod=%d argc=%d\n", s, p, argc);
    return 0;
}
