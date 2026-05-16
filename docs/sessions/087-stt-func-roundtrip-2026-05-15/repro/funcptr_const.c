/* Reproducer: function-pointer table in __const referencing extern
 * functions, where the .o is read back at link time.
 *
 * Compile:
 *   tcc -B$T -L$T -I$T/include -c -o table.o funcptr_const.c
 *   tcc -B$T -L$T -I$T/include    -o app    funcptr_const_main.o table.o
 *
 * The const table forces __TEXT,__const. The single TU also calls
 * isalpha() directly, so a REL24 to _isalpha exists in this TU's
 * .text — the STT_FUNC reconstruction should pick it up.
 *
 * Expected after fix: prints "all match" and exits 0.
 * Pre-fix: SIGBUS or wrong pointer printed.
 */
#include <ctype.h>
#include <stdio.h>

/* Tiger's <ctype.h> defines isalpha etc. as macros that inline to
 * __istype, so a normal `isalpha(c)` call generates `bl ___istype`
 * not `bl _isalpha`. Undef the macros so direct calls show up as
 * REL24 to _isalpha / _isdigit / _isspace — that's what the
 * STT_FUNC reconstruction heuristic looks for. Sed's real source
 * has direct calls to isalpha scattered throughout the link, so
 * it doesn't need this dance; this repro is more minimal. */
#undef isalpha
#undef isdigit
#undef isspace
extern int isalpha(int);
extern int isdigit(int);
extern int isspace(int);

typedef int (*pred_t)(int);

/* The table that lives in __TEXT,__const because of the `const`. */
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
    int direct_alpha = isalpha('A');   /* REL24 → _isalpha */
    int direct_digit = isdigit('7');   /* REL24 → _isdigit */
    int direct_space = isspace(' ');   /* REL24 → _isspace */

    if (!direct_alpha || !direct_digit || !direct_space) {
        fprintf(stderr, "direct calls broken\n");
        return 1;
    }

    /* Calling through the table must work. The table contains
     * function pointers in __TEXT,__const — pre-fix, tcc baked
     * __nl_symbol_ptr slot addresses into those entries, and
     * calling through them jumped into data memory and crashed.
     * Post-fix, the entries hold stub VAs that load via
     * __nl_symbol_ptr and branch to the dyld-resolved function. */
    {
        char  inputs[3]      = { 'A',     '7',    ' '    };
        int   expected[3]    = {  1,       1,      1     };
        const char *names[3] = { "alpha", "digit", "space" };
        int ok = 1;
        for (i = 0; i < 3; i++) {
            int r = prednames[i].fn((unsigned char)inputs[i]);
            printf("prednames[%d=%s](%c) = %d (expected %d)\n",
                   i, names[i], inputs[i], r, expected[i]);
            if (r != expected[i]) ok = 0;
        }
        if (!ok) return 2;
    }

    /* Note: tcc on Tiger places this `static const` table in
     * __TEXT,__const (gcc-4.0 puts it in __DATA,__const so dyld can
     * bind extrels there and pointer equality holds). With tcc's
     * placement, the table stores a stub VA — not the function's
     * libSystem VA — so `prednames[i].fn == &foo` does NOT hold.
     * Functional behavior (calling through the pointer) does. */
    printf("all match\n");
    return 0;
}
