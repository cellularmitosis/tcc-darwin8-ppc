/* v0.2.58-dsym-source-lines.c — main TU for the source-line-via-.dSYM
 * demo.  Built two-step with -gdwarf-2 alongside its helpers TU; the
 * demo strips the linked exe, runs it, then asks Tiger gdb to do
 * source-line operations (list, break file:LINE) using only the
 * companion `.dSYM`.
 */

#include <stdio.h>

extern int helper_add(int a, int b);
extern int helper_mul(int a, int b);

int main(int argc, char **argv) {
    int s = helper_add(3, 4);
    int p = helper_mul(5, 7);
    printf("sum=%d prod=%d argc=%d\n", s, p, argc);
    return 0;
}
