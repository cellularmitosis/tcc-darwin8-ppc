/* v0.2.51-g3 — OSO STAB emission for gdb-on-Tiger (roadmap #7.5).
 *
 * Small program exercising the kinds of source-level entities a user
 * debugging with `gdb` on Tiger expects to see: globals, statics,
 * a non-trivial helper function called from main, multiple source
 * lines per function. The shell wrapper builds this with
 * `tcc -gstabs`, runs `nm -ap` to dump the embedded STAB chain, then
 * scripts gdb to set breakpoints by file:line, run, and inspect the
 * backtrace.
 *
 * The program itself just exits with status 0 — the interesting
 * artifacts are the STAB entries in the linked exe and gdb's
 * behavior when stepping through them, both validated by the
 * companion .sh.
 */
#include <stdio.h>

static int g_static = 42;
int g_global = 7;

int helper(int x)
{
    int sum = x + g_static;
    return sum;
}

int main(int argc, char **argv)
{
    int local = helper(argc);
    printf("hello %d %d\n", local, g_global);
    (void)argv;
    return 0;
}
