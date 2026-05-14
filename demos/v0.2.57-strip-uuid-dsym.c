/* v0.2.57-strip-uuid-dsym.c — main TU for the strip-and-debug demo.
 *
 * Built two-step with -gdwarf-2 alongside its helpers TU; the demo
 * strips the linked exe with `strip -S`, runs it, then asks Tiger
 * gdb to find the companion `.dSYM` via LC_UUID and resolve symbols.
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
