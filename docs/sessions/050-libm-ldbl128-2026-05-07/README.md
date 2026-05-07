# Session 050 — libm LDBL128 redirect for math functions

**Date:** 2026-05-07 (fourth release of the evening)
**Build host:** imacg3

## Arrival state

* HEAD `9683fd6` — v0.2.34-g3.
* All previously-passing tests/demos still pass.
* `make test3` had 45 lines of content-diffs vs gcc reference,
  documented as "known structural differences" but never deeply
  examined.

## Investigation

Looking at the test3 diff this evening, most of it turned out to
be NOT structural — the 30+ lines of diverging "subnormal" output
were a real bug. tcctest's `0x0.88p-1022` literal (a tiny
subnormal double) was being parsed as 136 by tcc. Direct
`tcc -run tcctest.c` produced the correct value; only the iterated
`tcc -run tcc.c -run tcctest.c` (i.e., when tcc runs JIT'd-tcc)
showed the bug.

The path: `parse_number` in tccpp.c handles hex floats by
accumulating the mantissa in `bn[]`, computing
`d = (long double)bn[3] * 2^96 + ...`, then scaling with
`d = ldexpl(d, exp_val - frac_bits)`. For `0x0.88p-1022`:
* mantissa = `bn[0] = 0x88 = 136`, others 0.
* d = 136L (in dd form: hi=136, lo=0).
* d = ldexpl(136, -1022 - 8) = ldexpl(136, -1030).

ldexpl should return `136 * 2^-1030 ≈ 1.18e-308` (a subnormal
double). Instead it was returning 136 unchanged.

Symbol inspection:
* gcc-4.0-built program references `_ldexpl$LDBL128` (the 16-byte-LD
  Tiger libm entry).
* tcc-built program references plain `_ldexpl` (the LEGACY 8-byte-LD
  entry, which on Tiger treats LD as a double — and apparently
  doesn't even handle the int-exponent path correctly for our
  16-byte input layout).

Tiger's `<architecture/ppc/math.h>` has the redirect macro
`__LIBMLDBL_COMPAT(sym) __asm("_" #sym "$LDBL128")`, gated on
`__WANT_LONG_DOUBLE_FORMAT__ == 128`. That format selector
evaluates to 128 only when `__APPLE_CC__ && __LONG_DOUBLE_128__`
are both defined. tcc already defines `__APPLE_CC__ = 1` (in
tccdefs.h, for TargetConditionals.h compatibility) but never
defined `__LONG_DOUBLE_128__`.

This is the math.h analog of v0.2.32's `<sys/cdefs.h>` redirect
work — which I'd only known to fix because I happened to chase a
printf failure end-to-end. The math.h side wasn't on my radar
until this test3 investigation.

## Fix

One line in ppc-gen.c's `target_machine_defs`:

```diff
+    "__LONG_DOUBLE_128__ 1\0"
```

`__APPLE_CC__` was already defined to 1; that was sufficient
input to the math.h `&&` — only `__LONG_DOUBLE_128__` was missing.

## Verification

| Check | Pre | Post |
|---|---|---|
| tcc symbol for `ldexpl` call | `_ldexpl` (legacy) | `_ldexpl$LDBL128` ✓ |
| `ldexpl(136, -1030)` from tcc | 136 (wrong) | ≈ 1.18e-308 ✓ |
| tcctest's `da subnormal` line | `0x1.1p+7` (= 136, wrong) | `0x1.1p-1023` ✓ |
| `make test3` diff lines | 45 | 13 (all structural) |
| tests2 | 111/111 | 111/111 |
| abitest-cc | 24/24 | 24/24 |
| abitest-tcc | 24/24 | 24/24 |
| libtest, dlltest | pass | pass |
| Bootstrap fixpoint | holds | holds |
| All real-world demos | pass | pass |

Released as **v0.2.35-g3**.

## What's left in test3 (the 13 remaining diff lines)

* `aligntest3 sizeof=16 alignof=4` (vs gcc's `alignof=8`):
  Apple PPC32 ABI says double aligns to 4 in structs. gcc-4.0 on
  Tiger uses 8 for compatibility with older PowerOpen ABI. Our
  alignof=4 matches Apple's documented ABI. Won't change.
* `promote char/short funcret 137 -1`:
  tcctest casts via `csf` (char-subword to int via signed char).
  Both tcc and gcc define this implementation-defined; they pick
  different definitions. Both legal.
* `sizeof(_Bool) = 1`:
  C99 says `_Bool` has size 1 (≥ 1 byte for any non-bitfield
  scalar). gcc-4.0 used 4 — predates C99 settling. tcc matches
  the standard.

These are documented in the session 047 HANDOFF. Not bugs.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents — this was a fast diagnose-and-fix that benefited
from session 047's earlier work on the analogous stdio.h
LDBL128 redirect.

## Closing note

This is the kind of follow-up bug that tends to lurk: the LD ABI
work in v0.2.32 fixed printf/scanf/etc., the precision-correct dd
in v0.2.34 fixed the arithmetic, but the math.h redirect was
missing because the bug only showed up via an indirect path
(parse_number's internal ldexpl call). Worth re-scanning the
header tree for any other `__LIBM*COMPAT` / `__DARWIN*COMPAT`
macros gated on conditions tcc isn't satisfying — none likely
remain (the two we know about are sys/cdefs.h and
architecture/ppc/math.h), but it's the kind of thing that's
better grep'd than discovered by accident.
