/* v0.2.17-g3 milestone — alloca() works correctly.
 *
 * Run on ibookg37 (or any Tiger 10.4.11 PowerPC):
 *
 *     tcc -B<tccdir> -o /tmp/demo demos/v0.2.17-alloca.c
 *     /tmp/demo
 *
 * Expected output (last 5 lines):
 *     names[0] = jack
 *     names[1] = jill
 *     names[2] = jane
 *     concatenated: jack/jill/jane
 *     done
 *
 * Why this matters: pre-v0.2.17, code that called alloca() and then
 * any subsequent function (e.g. printf, strcpy) would silently
 * corrupt the alloca'd memory and eventually segfault on epilog.
 * Two coupled bugs:
 *
 *   1. Function epilog used `addi r1, r1, frame_size` which assumes
 *      r1 hasn't moved during the function. alloca DROPS r1, so
 *      after the call this addi puts r1 in the wrong place. New
 *      epilog restores r1 from the back chain (lwz r1, 0(r1)) and
 *      saved regs via FP (r31), both of which survive alloca.
 *
 *   2. The PPC alloca leaf-function trick reserves "padding" between
 *      the new SP and the user's region so subsequent callees'
 *      linkage areas don't stomp on alloca'd data. Old: 32 bytes
 *      (just enough for linkage). New: 256 bytes (covers up to 8
 *      GPR arg slots × 4 bytes + linkage, comfortably). Without
 *      this, a printf-of-an-alloca'd-string call would overwrite
 *      its own argument data before printf could read it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>

/* Allocate three small strings on the stack via alloca, copy fixed
 * names into each, then concatenate them via more alloca. Exercises
 * the same problematic pattern that broke pre-v0.2.17. */
int main(void) {
    const char *src[] = { "jack", "jill", "jane" };
    char *names[3];
    int i;
    size_t total = 0;

    for (i = 0; i < 3; i++) {
        size_t n = strlen(src[i]) + 1;
        names[i] = (char *)alloca(n);
        strcpy(names[i], src[i]);
        printf("names[%d] = %s\n", i, names[i]);
        total += n;  /* +1 for separator below */
    }

    /* Concatenate with '/' separators. Total: room for all three +
     * separators + final NUL. */
    char *joined = (char *)alloca(total + 3);
    joined[0] = '\0';
    for (i = 0; i < 3; i++) {
        if (i) strcat(joined, "/");
        strcat(joined, names[i]);
    }
    printf("concatenated: %s\n", joined);
    printf("done\n");
    return 0;
}
