/* v0.2.52-g3 — N_PSYM / N_LSYM stack offsets for gdb-on-Tiger
 * (roadmap #7.5 Phase 2, item 1a).
 *
 * Companion to v0.2.51's interactive-debug capability: now that
 * file:line breakpoints and stack walking work, this demo exercises
 * the part that v0.2.51's "Phase 1" couldn't — gdb's `print` for
 * parameters and local variables, including stepping past their
 * assignments and seeing the values change.
 *
 * Each function below takes several params and computes a few
 * locals from them. The .sh wrapper scripts gdb to break in each
 * function and `print` every name, asserting the values match
 * what would be computed for the actual call site in `main`.
 *
 * Multiple call sites in main exercise different argument values
 * so that "broken-but-coincidentally-zero" wouldn't pass.
 */
#include <stdio.h>

static int g_static = 100;
int g_global = 200;

int add3(int a, int b, int c)
{
    int s1 = a + b;
    int s2 = s1 + c;
    return s2;
}

int with_local_and_global(int x)
{
    int doubled = x * 2;
    int with_g = doubled + g_global;
    int with_s = with_g + g_static;
    return with_s;
}

int many_locals(int p, int q)
{
    int a = p + 1;
    int b = q + 2;
    int c = a + b;
    int d = a * b;
    int e = c - d;
    return e;
}

int main(int argc, char **argv)
{
    int r1 = add3(10, 20, 30);
    int r2 = with_local_and_global(5);
    int r3 = many_locals(7, 11);
    printf("r1=%d r2=%d r3=%d\n", r1, r2, r3);
    (void)argc;
    (void)argv;
    return 0;
}
