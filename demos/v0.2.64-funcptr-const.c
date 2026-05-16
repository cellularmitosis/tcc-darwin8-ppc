/* Demo for v0.2.64-g3 — function-pointer tables in __TEXT,__const
 * resolve correctly via stub allocation across a .o roundtrip.
 *
 * Pre-v0.2.64, `static const struct { name, fn } table[]` with extern
 * function pointers SIGSEGV'd when called through, because:
 *   1. .o write drops STT_FUNC from the undef nlist (Mach-O nlist
 *      has no FUNC bit).
 *   2. .o read reconstructs the extern sym as STT_NOTYPE.
 *   3. collect_extern_stubs's ADDR32+STT_FUNC clause misses → no
 *      stub allocated.
 *   4. v0.2.63's writable-section gate falls back to the slot-address
 *      path for __TEXT,__const (dyld can't bind there).
 *   5. The table entry holds &__nl_symbol_ptr[fn] (a data slot) — not
 *      a function-call-able VA. Calling through it jumps to data.
 *
 * v0.2.64-g3 fixes this with two heuristics in collect_extern_stubs:
 *   (a) Any undef sym referenced via R_PPC_REL24 is a function.
 *   (b) Any undef sym referenced via R_PPC_ADDR32 from .rodata is a
 *       function (vs. the much rarer `const T *const p = &extern_data`
 *       pattern). This is what catches sed's prednames[] on Tiger
 *       where <ctype.h> macros mean no direct REL24 to isalpha exists
 *       anywhere in the link.
 */
#include <ctype.h>
#include <stdio.h>

#undef isalpha
#undef isdigit
#undef isspace
extern int isalpha(int);
extern int isdigit(int);
extern int isspace(int);

typedef int (*pred_t)(int);

static const struct {
    const char *name;
    pred_t      fn;
} prednames[] = {
    { "alpha", isalpha },
    { "digit", isdigit },
    { "space", isspace },
};

int main(void)
{
    int i;
    char  inputs[3]      = { 'A',     '7',    ' '    };
    int   expected[3]    = {  1,       1,      1     };
    int ok = 1;

    for (i = 0; i < 3; i++) {
        int r = prednames[i].fn((unsigned char)inputs[i]);
        printf("prednames[%d=%s](%c) = %d (expected %d)\n",
               i, prednames[i].name, inputs[i], r, expected[i]);
        if (r != expected[i]) ok = 0;
    }
    return ok ? 0 : 1;
}
