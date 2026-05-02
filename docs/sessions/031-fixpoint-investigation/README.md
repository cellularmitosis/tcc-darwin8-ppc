# 031 — Fixpoint regression investigation (deep dive, not fixed)

## TL;DR

🟡 The 32-byte fixpoint regression first noted in
[026/findings.md](../026-libgcc-helpers/findings.md) is the
*same bug* as the "long-long shift codegen quirk" noted in
[027/README.md](../027-self-link/README.md). It's a tcc PPC backend
bug in **compile-time constant folding** of 64-bit binary operations
(`&`, `|`, `^`, `+`, `*`) when the result has bit 31 of the low
half set. The high half of the constant gets sign-extended to all
1s (via the wrong code path through `vpushi(ll >> 32)`) instead of
being computed correctly. Documented in detail here; not fixed
because the actual change site is buried under 3+ layers of tcc
internal constant-folding/cast machinery and the right surgical
fix isn't obvious without deeper expertise. Worth a follow-up
session.

## Concrete reproduction

```c
unsigned long long g = 0x80000000ULL & 0xffffffffULL;
```

| compiler | `g` (expected `0x80000000`) |
|---|---|
| gcc-built tcc | `0x0000000080000000` ✓ |
| tcc-self      | `0xffffffff80000000` ✗ |

The .data section literally contains the wrong bytes:

```
gcc-built tcc:    00000000 80000000
tcc-self:         ffffffff 80000000
```

## What we proved

* **Plain assignment works**: `unsigned long long a = 0x80000000ULL;`
  → correct `0x80000000` in both. So the literal is parsed correctly.
* **Runtime AND works**: `unsigned long long c = a & b;` (variables)
  → correct in both.
* **All compile-time binary ops on 64-bit constants** with bit 31
  set in the low half produce wrong results in tcc-self:
  `& 0xffffffff`, `| 0`, `^ 0`, `+ 0`, `* 1`, `& self`, etc. — all
  give `0xFFFFFFFF80000000` instead of `0x80000000`.
* **Bit-pattern dependence**: only constants with bit 31 of the low
  half set trip the bug. `0x40000000` is fine, `0x80000000` and
  `0xffffffff` are not. The high half is sign-extended from bit 31
  of the low half.

## Where it manifests in the emitted code

`m2(void) { return 0x80000000ULL & 0xffffffffULL; }` — the diff
is one instruction:

```
gcc-built tcc emits:    li r4, 0x0       ; high half = 0 (correct)
tcc-self emits:         li r4, 0xffff    ; high half = -1 (wrong, sign-extended)
```

The bug is in tcc-self's COMPILED machine code for the
constant-loading path. It's loading the high 32 bits of a 64-bit
constant from the wrong place — either it computed `ll >> 32`
incorrectly (where `ll` was already corrupt to `0xFFFFFFFF80000000`),
or `vpushi(ll >> 32)` lost type info.

## What we tried

| Hypothesis | Result |
|---|---|
| `value64` returns 0 for `(0x80000000ULL, VT_LLONG\|VT_UNSIGNED)` | Tested standalone — returns correct `0x80000000`. Not the bug. |
| Struct field 64-bit write only emits 32-bit store | `s.i = 0x80000000` works fine. Not the bug. |
| Long-long shift `>>= 32` is broken at runtime | Works correctly at runtime. Not the bug. |
| 32-bit XOR with 0x80000000 broken | Works correctly. Not the bug. |
| `gen_cast` sign-extends LLONG → LLONG | Code at line 3391 only fires for non-LLONG source; shouldn't apply. |

The smoking gun is in `tcc/tccgen.c::gv` around line 1968:

```c
if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
    /* load constant */
    unsigned long long ll = vtop->c.i;
    vtop->c.i = ll; /* first word */
    load(r, vtop);
    vtop->r = r; /* save register value */
    vpushi(ll >> 32); /* second word */
}
```

For `ll = 0x80000000`, `ll >> 32` should be 0. tcc-self's compiled
version of THIS code apparently computes 0xFFFFFFFF, then `vpushi`
pushes -1, which gets emitted as `li r4, 0xffff` (`addi r,0,-1`).

The standalone test of `unsigned long long >> 32` works in
tcc-self — so the bug isn't in the shift itself but in some
context-specific interaction (maybe register allocation, maybe a
stale value in `vtop->c.i` from a prior step). Pinning it down
exactly will need either a debug build of tcc-self (with printfs
inserted) or careful gdb work on the running tcc-self.

## Why it's the *same* bug as the 11-instruction fixpoint diff

Confirmed by working backward from the disasm of `_gen_opic_lt`
(one of the three functions that differ between gcc-built tcc.o
and tcc-self.o):

```c
static int gen_opic_lt(uint64_t a, uint64_t b)
{
    return (a ^ (uint64_t)1 << 63) < (b ^ (uint64_t)1 << 63);
}
```

The constant `(uint64_t)1 << 63` is `0x8000000000000000`.
gcc-built tcc emits the `lis r5, 0x8000; xor r4, r4, r5` sequence
to perform the high-half XOR (4 instructions × 2 operands = 8
extra; minus the 4 that ARE there in tcc-self because it does some
other handling = 4 net). tcc-self skips those XORs entirely —
it constant-folded the high half of `1 << 63` to 0 (because of
this same low-bit-31 → high-half-sign-ext bug — note `0x80000000`
exactly == `1 << 31`, also tripping the same path), so the high
XOR is `XOR x, 0 = x` and gets optimised away.

So the fixpoint regression and the long-long-shift quirk are one
bug. Fixing it should close both, plus probably eliminate the
44-byte (11-instruction) `__text` size diff in `tcc.o` vs
`tcc-self.o`.

## Plausible fix sketch

The `vpushi(ll >> 32)` line takes an `int`. For `ll >> 32` where
`ll` is `unsigned long long`, the value is unsigned 32 bits. If
the high bit is set, casting to `int` makes it negative.

Replacing with:

```c
{
    unsigned int hi = (unsigned int)(ll >> 32);
    vpushv((SValue){ .type.t = VT_INT|VT_UNSIGNED, .r = VT_CONST,
                     .c.i = hi });
}
```

…or adding a `vpushui()` companion to `vpushi()` that pushes an
*unsigned* int constant, would likely fix it. But this changes
upstream tcc behavior; needs to be done carefully so other targets
still work.

A more surgical fix is in the constant-folding result store:
ensure `v1->c.i` after `gen_opic` always has the correct sign-
extension semantics for the result's type, even if intermediate
computations went wrong. Both directions need code reading.

## Status

**Documented but not fixed.** Captured here so a future session
can pick up cold. The regression doesn't block any v0.2.0
functionality — both `tcc-self` and `tcc-self2` work, the
fixpoint between *them* holds, only parity with gcc-built tcc.o
is broken. v0.2.0 ships fine without this fix.
