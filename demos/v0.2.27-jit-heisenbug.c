/* v0.2.27-jit-heisenbug.c — proves the long-deferred JIT
 * heisenbug is fixed.
 *
 * Pre-v0.2.27, this loop would crash with SIGILL / SIGBUS /
 * SIGSEGV (or return wrong values) at a random iteration —
 * see session 044's HANDOFF for the multi-session debugging
 * trail. Best-of-15 runs would get 19 abitest-tcc tests
 * passing; worst case 5.
 *
 * The bug: `__clear_cache(begin, end)` in ppc-macho.c was a
 * no-op stub when tcc compiled itself (`__TINYC__` set,
 * inline-asm not available), so `protect_pages(... 0/3)`
 * never actually flushed the icache. The JIT page at the same
 * address (e.g. 0xa7000) gets reused across iterations, but
 * the CPU keeps fetching stale instructions from icache.
 *
 * The fix: delegate to libSystem's `sys_icache_invalidate`,
 * which performs the dcbst/sync/icbi/sync/isync sequence
 * inside its own implementation.
 *
 * Compiled with tcc, this demo loops 30 times: each iteration
 * creates a new TCCState, JIT-compiles a struct-returning
 * function, calls it, and tcc_deletes. With the fix, all 30
 * iterations always succeed.
 */

#include <stdio.h>
#include "libtcc.h"

typedef struct { float x, y; } two_float;

static int run_one(int i) {
    TCCState *s = tcc_new();
    tcc_set_lib_path(s, "tcc");
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s,
        "typedef struct { float x, y; } two_float;\n"
        "two_float f(two_float a) { two_float r = {a.x*5, a.y*3}; return r; }\n") < 0)
        return -2;
    if (tcc_relocate(s) < 0) return -3;
    void *p = tcc_get_symbol(s, "f");
    if (!p) return -4;
    two_float (*f)(two_float) = (two_float (*)(two_float))p;
    two_float a = {10, 35};
    two_float r = f(a);
    tcc_delete(s);
    return (r.x == 50 && r.y == 105) ? 0 : -5;
}

int main(void) {
    int i;
    for (i = 0; i < 30; i++) {
        int rc = run_one(i);
        fprintf(stderr, "%d:%d ", i, rc);
        if (rc != 0) {
            fprintf(stderr, "\nFAIL at i=%d\n", i);
            return 1;
        }
    }
    fprintf(stderr, "\n30 iterations OK -- JIT heisenbug fixed\n");
    return 0;
}
