# Session 059 findings

## Design call: keep tcc's compile-time `(unsigned)(negative_float)` as `fctiwz` semantics

### Context

Session 058's csmith campaign C surfaced **seed 3259** as a tcc-vs-gcc-4.0
output divergence. Triage (058 findings.md) attributed it to an
implementation-defined float-to-uint32 conversion: a static
initializer of the form `g_231 = (-0x1.2p+1)` (i.e. the
init-value `(uint32_t)(-2.25)`) folds differently in the two
compilers:

| Compiler | Result for `(uint32_t)(-2.25)` | Mechanism |
|---|---|---|
| tcc (this tree) | `0xFFFFFFFE` | `fctiwz` semantics: round-toward-zero to int32 = -2, reinterpret bits as uint32 |
| gcc-4.0 (Tiger) | `0x00000000` | clamp-to-zero on negative-to-unsigned compile-time fold |

The 058 HANDOFF flagged this as a design call rather than a bug
("recommend keep current behavior"). This note is the durable
record of that decision.

### Where tcc's choice lives

`tcc/tccgen.c` lines 3535–3567, inside `gen_cast`'s constant-fold
arm (added in v0.2.46, commit `0aa4690`):

```c
#if defined(TCC_TARGET_PPC) && !defined(TCC_TARGET_PPC64)
    /* PPC fctiwz saturates float-to-int32 to INT_MAX/INT_MIN
     * and produces 0x80000000 for NaN. ... */
    if (dbt_bt != VT_LLONG) {
        long double ld = vtop->c.ld;
        if (ld != ld)                       /* NaN */
            vtop->c.i = (int64_t)(int32_t)0x80000000;
        else if (ld >= 2147483648.0L)       /* +overflow */
            vtop->c.i = 0x7FFFFFFF;
        else if (ld < -2147483648.0L)       /* -overflow */
            vtop->c.i = (int64_t)(int32_t)0x80000000;
        else                                /* in-range */
            vtop->c.i = (int64_t)ld;
    } else
#endif
    ...
```

For an in-range negative float like `-2.25`, the fold lands at
`vtop->c.i = (int64_t)(-2.25L)` = `-2`. The downstream byte mask
at line 3583 (`vtop->c.i &= 0xffffffff`) then yields `0xFFFFFFFE`
when the destination is `uint32_t`. That is the same bit pattern
the runtime `fctiwz` instruction produces for `(int)(-2.25)` —
the v0.2.46 fix exists precisely to keep both paths in lockstep.

### Why C says neither is wrong

Per **C99 6.3.1.4p1**:

> When a finite value of real floating type is converted to an
> integer type other than `_Bool`, the fractional part is
> discarded (i.e., the value is truncated toward zero). If the
> value of the integral part cannot be represented by the integer
> type, the behavior is undefined.

`-2.25` truncates to `-2`, which has no representation in
`uint32_t`. The standard says the result is **undefined**. Both
gcc-4.0's `0` and tcc's `0xFFFFFFFE` are conformant.

### Why "keep current"

1. **Internal consistency.** v0.2.46 deliberately aligned
   compile-time and runtime float-to-int paths on PPC so that
   `int x = (int)1e10f;` folds identically to what runtime
   `fctiwz` would produce. Special-casing the negative-to-unsigned
   sub-shape would re-introduce the divergence v0.2.46 closed —
   for code that's already UB.
2. **Cost vs. payoff.** Across two `--float` campaigns (601 seeds
   total: 058's C with 301 seeds, plus the 050s spot-check), this
   is the only csmith FAIL traceable to the choice — a
   false-alarm rate of roughly 1 in 600. Triaging by hand once
   per campaign is cheaper than designing and testing a
   compile-time-only special case.
3. **`gen_cvt_ftoi` agrees.** ppc-gen.c:3157+ emits `fctiwz` for
   both signed and unsigned int targets (PPC has no native
   unsigned-fp-to-int instruction). Changing the const-fold to
   match gcc-4.0's clamp-to-zero on negatives would mean a
   negative float-literal cast to unsigned would produce one
   value at compile time and a different value at runtime —
   the exact bug class v0.2.46 was created to eliminate.

### Revisit criteria

Reopen this if **any** of the following occur:

* csmith `--float` campaigns flag this shape at >1% rate (i.e.
  3+ seeds in a 300-seed campaign).
* A real user-visible program is found to depend on the
  gcc-4.0 / clang clamp-to-zero behavior for a negative-float →
  unsigned constant.
* The runtime `gen_cvt_ftoi` path is ever changed to clamp
  negatives to zero (in which case the const-fold should follow
  to keep parity).

The first is the most realistic trigger; the others would
require deliberate code action to come up.

### Cross-references

* Original triage: [`058/findings.md` "seed 3228" / "Triage"](../058-followups-2026-05-09/findings.md)
  — see notes at the bottom of the file for the seed 3259 entry.
* v0.2.46 fix that introduced the fctiwz emulation:
  [`056/findings.md`](../056-csmith-2026-05-09/findings.md), commit
  `0aa4690`.
