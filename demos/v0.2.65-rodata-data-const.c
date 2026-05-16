/* Demo for v0.2.65-g3 — `.rodata` with undef-ADDR32 relocs is routed
 * to `__DATA,__const` so dyld can bind the slot values at load time.
 * Pointer equality with `&extern_fn` then holds across all three
 * reference idioms — gcc-4.0 parity on Tiger.
 *
 * Three reference patterns to the same extern function:
 *   - prednames[].fn  : `static const fn_t arr[] = {extern_fn};` in
 *                       rodata, now placed in __DATA,__const, slot
 *                       bound by dyld via classic-format extrel.
 *   - global_q        : `static fn_t q = extern_fn;` in writable
 *                       __DATA, also extrel'd (v0.2.65 widened the
 *                       extrel path to include STT_FUNC undefs).
 *   - local_q         : `fn_t q = extern_fn;` in a function: tcc's
 *                       code-gen emits an indirect load through the
 *                       __nl_symbol_ptr slot (dyld-bound to the
 *                       libSystem function VA).
 *
 * All three end up with the same libSystem-bound VA at runtime, so
 * `prednames[0] == global_q == local_q` holds. Pre-v0.2.65 the table
 * slot held a stub VA (v0.2.64 stub trick) and the .data slot also
 * held a stub VA (v0.2.63's wants_extrel excluded STT_FUNC) — so
 * neither matched local_q's libSystem VA from the nl_ptr indirection.
 */
#include <ctype.h>
#include <stdio.h>

#undef isalpha
extern int isalpha(int);

typedef int (*fn_t)(int);

static const fn_t prednames[] = { isalpha };
static       fn_t global_q    = isalpha;

int main(void)
{
    fn_t local_q = isalpha;
    int ok = 1;

    printf("prednames[0] = %p\n", (void *)prednames[0]);
    printf("global_q     = %p\n", (void *)global_q);
    printf("local_q      = %p\n", (void *)local_q);

    /* Functional: each pointer must be callable and return 1 for 'A'. */
    if (prednames[0]('A') != 1) { printf("FAIL: prednames[0] call\n");  ok = 0; }
    if (global_q('A')     != 1) { printf("FAIL: global_q call\n");      ok = 0; }
    if (local_q('A')      != 1) { printf("FAIL: local_q call\n");       ok = 0; }

    /* Pointer equality: all three are the libSystem function VA. */
    if (prednames[0] != global_q) {
        printf("FAIL: prednames[0] != global_q (%p vs %p)\n",
               (void *)prednames[0], (void *)global_q);
        ok = 0;
    }
    if (prednames[0] != local_q) {
        printf("FAIL: prednames[0] != local_q (%p vs %p)\n",
               (void *)prednames[0], (void *)local_q);
        ok = 0;
    }

    return ok ? 0 : 1;
}
