/*
 * Session 012 milestone — varargs (stdarg.h, va_start/va_arg/va_end).
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC):
 *
 *     ./tcc -B. -run demos/s012-varargs.c
 *     echo $?      # → 42
 *
 * Why this matters: tcc's own source uses varargs in tcc_warning,
 * tcc_error, cstr_printf, and helpers throughout. Without working
 * varargs, no self-host. Plus, varargs is the prerequisite for
 * `printf` (the first user-visible payoff).
 *
 * What landed in 012: refactored gfunc_prolog to spill all 8 GPR
 * arg registers (r3..r10) to the caller's parameter save area at
 * r31+24..+52. This is unconditional — even no-arg / non-variadic
 * functions pay 32 bytes / 8 instructions per call, in exchange for
 * a constant prolog size and the ability for the standard C
 * `va_list = char *` machinery (from tcc/include/tccdefs.h's i386
 * branch, which we fall through to) to walk forward from the last
 * fixed argument:
 *
 *     #define __builtin_va_start(ap, last) \
 *         (ap = ((char *)&(last)) + ((sizeof(last) + 3) & ~3))
 *
 * &last gets the address of the spill slot for the last named
 * argument; adding sizeof(last) reaches the next slot, which is the
 * first variadic argument.
 *
 * Also reorganized: params now live at POSITIVE offsets from FP
 * (r31 + 24 + i*4), not negative as in earlier sessions. Locals
 * still live at negative offsets (r31 - 4 for FP save, r31 - 8 down
 * for user locals). Two distinct addressing regions sharing the
 * same FP register.
 */

#include <stdarg.h>

int sum_n(int n, ...)
{
    va_list ap;
    int s = 0;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void)
{
    /* sum(7) = 1+2+...+7 = 28; 28 + 14 = 42. */
    int s = sum_n(7, 1, 2, 3, 4, 5, 6, 7);
    return s + 14;
}
