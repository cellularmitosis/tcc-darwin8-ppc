# Session 049 — precision-correct double-double arithmetic

**Date:** 2026-05-07 (third release of the evening)
**Build host:** imacg3

## Arrival state

* HEAD `9827a4d` — v0.2.33-g3.
* All previously-passing tests/demos still pass.
* Open work item B1 (precision-correct dd) was the headline gap.

## Motivation

v0.2.32 made `long double` a real 16-byte IBM-double-double on
tcc-Apple-PPC. But the runtime helpers in `lib-ppc.c`
(`__gcc_q{add,sub,mul,div}` and the tf2 comparison family) were
LOSSY: they took the high doubles only, did plain double arith,
and zeroed the low half of the result. abitest values fit in
double's 53-bit mantissa so this isn't visible there, but any
program that relies on dd's full ~104-bit mantissa silently lost
precision:

```c
long double x = 9007199254740992.0L;  /* 2^53 */
long double r = (x + 1.0L) - x;       /* lossy: 0; expected: 1 */
```

## Implementation

Standard Knuth Two-Sum + Veltkamp split + Dekker product, no fma.

```c
static void dd_two_sum(double a, double b, double *s, double *e)
{
    double bb;
    *s = a + b;
    bb = *s - a;
    *e = (a - (*s - bb)) + (b - bb);
}

static void dd_split(double a, double *hi, double *lo)
{
    double t = a * 134217729.0;        /* 2^27 + 1 */
    double t1 = t - a;
    *hi = t - t1;
    *lo = a - *hi;
}

static void dd_two_prod(double a, double b, double *p, double *e)
{
    double ahi, alo, bhi, blo;
    *p = a * b;
    dd_split(a, &ahi, &alo);
    dd_split(b, &bhi, &blo);
    *e = ((ahi * bhi - *p) + ahi * blo + alo * bhi) + alo * blo;
}
```

The four arithmetic ops:

* **add**: TwoSum on the highs; add the lows; renormalize via
  QuickTwoSum.
* **sub**: same, with negated `b`.
* **mul**: TwoProd on the highs; add the cross terms `xhi*ylo +
  xlo*yhi` (drop `xlo*ylo` — below ~2^−104); renormalize.
* **div**: long division: `q1 = xhi/yhi`; compute residual
  `x - q1*y` as a dd value via `__gcc_qmul + __gcc_qsub`; second
  digit `q2 = (residual.hi)/yhi`; combine via QuickTwoSum.

Comparisons:

* For canonical dd values (hi is the rounded-double; lo is the
  remainder satisfying `|lo| ≤ ulp(hi)/2`), comparison is
  lexicographic on `(hi, lo)`. All four ordering ops share one
  implementation; libgcc tf2 semantics translate naturally
  ("`result <op> 0` matches `a <op> b`").
* eq/ne use bitwise equality on both halves.

## Why no fma

PPC has hardware `fmadd` which would simplify `dd_two_prod` to two
flops (`p = a*b; e = fma(a, b, -p)`). But:

1. tcc-on-PPC doesn't emit `fmadd` from C — it uses regular
   `fmul`/`fadd`. So we'd have to call `fma()` from libm.
2. Pulling libm into libtcc1.a's dependency surface is undesirable.
3. Veltkamp+Dekker is only ~6 extra flops per product; on a G3 in
   2026 nobody is benchmarking dd performance.

If we ever grow tcc-on-PPC `fmadd` codegen (or expose it as a
builtin), this can be revisited.

## Verification

| Check | Pre | Post |
|---|---|---|
| `(2^53 + 1) - 2^53 == 1` | ❌ (returns 0) | ✅ |
| `(1e16 + 1) - 1e16 == 1` | ❌ | ✅ |
| `1e20+1 != 1e20+2` | ❌ (compares equal) | ✅ |
| `pi*pi - (double(pi))^2 != 0` | ❌ | ✅ |
| tests2 | 111/111 | 111/111 |
| abitest-cc | 24/24 | 24/24 |
| abitest-tcc | 24/24 | 24/24 |
| libtest, dlltest | pass | pass |
| Bootstrap fixpoint | holds | holds |
| All real-world demos | pass | pass |

Released as **v0.2.34-g3**.

## Side observations

* `(double)(long double)x` round-trips correctly under the new
  arithmetic — for any double `x`, the dd value `(hi=x, lo=0)`
  flows through arithmetic with the low half tracking the actual
  rounding error.
* test3's known content diffs didn't change — they were never
  about LD precision; they're alignment-quirk and signed-vs-unsigned
  promotion artifacts.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents — straightforward port of textbook algorithms. The
hardest part was the tcc-specific union-cast for packing/unpacking
LD = `(hi, lo)`, which the existing `vstore` for VT_LDOUBLE handles
cleanly because we're storing/loading via the canonical 16-byte
memory layout.
