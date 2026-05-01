/*
 * Session 007 milestone — function calls per the Apple PPC ABI.
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC):
 *
 *     ./tcc -B. -run demos/s007-fibonacci.c
 *     echo $?      # → 55  (= fib(10))
 *
 * Why this matters: this is the first program tcc-darwin8-ppc compiles
 * with *more than one function*. Recursive Fibonacci stresses the
 * call mechanism in every direction at once — caller-side arg setup
 * (r3 = first arg), callee-side parameter unpacking (spill r3 to a
 * stack slot, then address it as a local), the bl instruction's
 * R_PPC_REL24 relocation patched at JIT time, deeply nested calls
 * preserving frame pointers across recursion, and r3 carrying the
 * return value back up the stack.
 *
 * fib(10) = 55, which fits in a Unix exit code.
 *
 * Codegen surface added in 007:
 *   - gfunc_call: gv() each arg into RC_R(i) for r3..r10, then either
 *     `bl <symbol>` (direct call, R_PPC_REL24 reloc) or
 *     `mtctr ; bctrl` (indirect via CTR).
 *   - gfunc_prolog: walk the parameter Sym chain, spill each incoming
 *     register arg to a fresh local slot at function entry, and call
 *     gfunc_set_param so the body addresses params as locals.
 *   - ppc-link.c: real implementations for R_PPC_REL24, R_PPC_ADDR32,
 *     R_PPC_ADDR16_HA / _HI / _LO. The relocation handler is what
 *     actually patches the `bl 0` placeholder bytes with the real
 *     branch displacement at relocation time.
 *   - ppc_frame_size now reserves 32 bytes of outgoing parameter area
 *     above the linkage area, so any callee can spill our args back.
 */

int fib(int n)
{
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void)
{
    return fib(10);  /* 0,1,1,2,3,5,8,13,21,34,55 */
}
