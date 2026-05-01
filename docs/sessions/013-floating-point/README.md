# 013 — IEEE 754 single + double floating point

PPC G3 has full IEEE 754 single and double FP hardware (FPU,
registers f0..f31). This session enables it.

## What was added (in `ppc-gen.c`, ~360 lines)

- **FP load/store** — `lfs`/`lfd`/`stfs`/`stfd` for VT_LOCAL,
  pointer-deref, and rodata-symbol forms.
- **FP constants** — placed in rodata, addressed via `lis` + `lfs/lfd`
  using `R_PPC_ADDR16_HA` + `R_PPC_ADDR16_LO` relocation pairs.
- **`gen_opf`** — `+ - * /` for both single (`fadds`, etc.) and
  double (`fadd`, etc.). Unary `TOK_NEG` via `fneg`. `fmul`/`fmuls`
  C-field encoding quirk handled.
- **FP comparisons** — `fcmpu cr0, fA, fB` plus `vset_VT_CMP`. Reuses
  the existing `gjmp_cond` machinery.
- **VT_CMP → int register** — `li/bc/li` sequence so `int x = (a < b)`
  works.
- **`gen_cvt_itof`** — classic "magic double" trick: write
  `0x43300000` high half + xor'd low half to a scratch slot, `lfd`,
  `fsub` the magic. Both signed and unsigned 32-bit ints.
- **`gen_cvt_ftoi`** — `fctiwz fD, fB` (round-toward-zero), `stfd`
  to scratch, `lwz` the low word.
- **`gen_cvt_ftof`** — `frsp` for double→float; nop for float→double
  (PPC FPRs are always 64-bit internally).
- **FP argument passing per Apple PPC ABI** — f1..f8 for arg slots.
  Each FP arg "shadows" 1 GPR slot (float) or 2 (double), per Apple
  rules.
- **FP parameter unpacking in `gfunc_prolog`** — spills f1..f8
  alongside r3..r10 so body code reads FP params via
  `lfs/lfd offset(r31)`.

## Critical bug fix in `tccgen.c`

`init_putv` was using `write32le` / `write64le` for FP literals in
rodata, reading from `vtop->c.i` (the union's uint64 alias). On a
big-endian host the low 32 bits of `c.i` aren't the same bytes as
`c.f` — the literal would land bit-reversed and `lfd` saw garbage.
Fixed with a `TCC_TARGET_PPC` branch that reads `c.f` / `c.d`
directly via a typed union and writes BE-ordered bytes.

## What's deferred

- `long long` ↔ FP conversions (errors with a clear message).
- Long double (Apple double-double 128-bit; aliased to double).
- More than 8 FP args.

## Test record

```
poly(1.0, 2.0, 3.0, 4.0)  → 27   (1*16 + 2*4 + 3)
pi * 100                  → 314 (low byte 58)
double a/b/c arithmetic   → all correct
mix(int, double, int)     → ABI-compliant
all prior int/long-long tests still pass
```

## Demo

[`demos/s013-floating-point.c`](../../../demos/s013-floating-point.c) —
quadratic polynomial evaluation, returns 27.

## Exit state

Floating point works for the surface tcc itself uses. Combined with
the previous sessions, the surface now covers nearly every C
construct tcc's source uses internally:
- Locals, arithmetic, control flow.
- Function calls, recursion.
- Pointers, arrays, modulo.
- Long long, structs (by pointer), varargs.
- Floating point single + double.
- Real PPC Mach-O .o output.

The remaining blockers for actual self-host bootstrap are mostly
about EXTERNAL function calls — calling memcpy/printf/malloc which
live in libSystem.dylib. That requires PLT for external symbols,
which is the next big chunk.

## Next session

014 — PLT for external symbols (the path to printf), or attempt the
self-host bootstrap and document what fails first.
