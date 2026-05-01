/*
 * Session 013 milestone — IEEE 754 single + double floating point.
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC G3):
 *
 *     ./tcc -B. -run demos/s013-floating-point.c
 *     echo $?      # → 27  (= polynomial 1*4^2 + 2*4 + 3)
 *
 * Why this matters: tcc parses FP literals in tccpp.c and propagates
 * them through gen_op. Beyond bootstrap, FP unlocks scientific code,
 * games, audio code — everything that wasn't pure integer logic.
 *
 * What landed in 013 (in `ppc-gen.c`):
 *   - FP load/store: lfs/lfd/stfs/stfd for VT_LOCAL, pointer-deref,
 *     and rodata-symbol forms.
 *   - FP constants placed in rodata, addressed via lis + lfs/lfd
 *     using R_PPC_ADDR16_HA / R_PPC_ADDR16_LO relocation pairs.
 *   - gen_opf: + - * / for both single (`fadds`/`fsubs`/`fmuls`/
 *     `fdivs`) and double (`fadd`/`fsub`/`fmul`/`fdiv`).
 *     fmul has the C-field encoding quirk handled.
 *   - Unary `TOK_NEG` via `fneg`.
 *   - FP comparisons via `fcmpu cr0, fA, fB` plus the existing
 *     gjmp_cond machinery.
 *   - VT_CMP → int register materialization (li/bc/li sequence)
 *     so `int x = (a < b)` works.
 *   - gen_cvt_itof: classic "magic double" trick (write 0x43300000
 *     high half + xor'd low half to a scratch slot, lfd, fsub the
 *     magic). Signed and unsigned 32-bit ints supported.
 *   - gen_cvt_ftoi: fctiwz to a temp FPR, stfd to scratch, lwz the
 *     low word.
 *   - gen_cvt_ftof: frsp for double→float; nop for float→double
 *     (PPC FPRs are always 64-bit internally).
 *   - FP argument passing per Apple PPC ABI: f1..f8 for arg slots,
 *     each FP arg also "shadows" 1 (float) or 2 (double) GPR slots.
 *   - FP parameter unpacking in gfunc_prolog spills f1..f8 alongside
 *     r3..r10 so body code reads params via `lfs/lfd offset(r31)`.
 *
 * Plus a critical bug fix in `tccgen.c`: `init_putv` was using
 * `write32le`/`write64le` for FP literals in rodata, reading from
 * `vtop->c.i` (the union's uint64 alias). On a big-endian host the
 * low 32 bits of `c.i` aren't the same bytes as `c.f` — the
 * literal would land bit-reversed and `lfd` saw garbage. Fixed by
 * a TCC_TARGET_PPC branch that reads `c.f`/`c.d` directly via a
 * typed union and writes BE-ordered bytes.
 *
 * What's deferred:
 *   - long long ↔ FP conversions.
 *   - long double (Apple double-double 128-bit; we treat it as
 *     an alias for double for now).
 *   - More than 8 FP args.
 */

double poly(double a, double b, double c, double x)
{
    return a*x*x + b*x + c;
}

int main(void)
{
    /* p(4) for the polynomial 1*x^2 + 2*x + 3 = 16 + 8 + 3 = 27. */
    return (int)poly(1.0, 2.0, 3.0, 4.0);
}
