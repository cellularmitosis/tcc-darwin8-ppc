# Session 051 — `__builtin_fma` / `__builtin_fmaf` codegen

**Date:** 2026-05-07 (fifth release of the evening)
**Build host:** imacg3

## Arrival state

* HEAD `0fed2a7` — v0.2.35-g3.
* All previously-passing tests/demos still pass.

## Motivation

Tcc-on-PPC didn't recognize `__builtin_fma` or `__builtin_fmaf`.
Calls to them compiled (tcc treated the names as ordinary external
functions) but failed at runtime with `dyld: Symbol not found:
___builtin_fma`. There's no library-side stub, and even if we
provided one (libm has fma()), the cost would be a runtime function
call.

PPC has hardware `fmadd FRT, FRA, FRC, FRB` (and `fmadds` for
single precision) that compute `FRA*FRC + FRB` in one rounding
step. Exposing it as a tcc builtin gives both:
1. correctness for any program that uses `__builtin_fma()`
2. a small perf win for `lib-ppc.c`'s `dd_two_prod` (Knuth/Dekker
   double-double product), which collapses from ~10 flops to 2.

## Implementation

Three pieces:

### Tokens (`tcctok.h`)

```c
#if defined TCC_TARGET_PPC
    DEF(TOK_builtin_fma,  "__builtin_fma")
    DEF(TOK_builtin_fmaf, "__builtin_fmaf")
#endif
```

### Helper (`ppc-gen.c`)

```c
void gen_ppc_fmadd(int dst_slot, int a_slot, int b_slot,
                   int c_slot, int dbl)
{
    int frt = TREG_TO_FPR(dst_slot);
    int fra = TREG_TO_FPR(a_slot);
    int frc = TREG_TO_FPR(b_slot);   /* FRC = b */
    int frb = TREG_TO_FPR(c_slot);   /* FRB = c */
    /* Double: opcode 63, XO 29, Rc=0  -> 0xfc00003a
     * Single: opcode 59, XO 29, Rc=0  -> 0xec00003a
     * Note FRC is at bits 6..10 (not the usual FRB position at
     * 11..15) — fmul* and fmadd* take 4 register operands. */
    uint32_t base = dbl ? 0xfc00003a : 0xec00003a;
    o(base | (frt << 21) | (fra << 16) | (frb << 11) | (frc << 6));
}
```

Plain `void` (no `ST_FUNC = static`) so the tccgen.c-side `extern`
declaration doesn't trigger "static storage ignored" warnings under
ONE_SOURCE compilation.

### Dispatcher (`tccgen.c`)

In the expression-parser switch on `tok`, gated by
`#ifdef TCC_TARGET_PPC`:

```c
case TOK_builtin_fma:
case TOK_builtin_fmaf: {
    extern void gen_ppc_fmadd(int, int, int, int, int);
    int dbl = (tok == TOK_builtin_fma);
    int a_slot, b_slot, c_slot, dst_slot;
    parse_builtin_params(0, "eee");          /* vstack: [a, b, c] */
    type.t = dbl ? VT_DOUBLE : VT_FLOAT;
    type.ref = NULL;
    /* Cast each. After parse, vtop = c. Rotate-cast to hit b, then a. */
    gen_cast(&type);
    vrott(3); gen_cast(&type);
    vrott(3); gen_cast(&type);
    /* vstack now [b, c, a], vtop = a. Materialize each into an FPR
     * via gv-then-rotate. tcc's regalloc avoids vstack-occupied FPRs
     * so the three gv()s land in three distinct registers. */
    gv(RC_FLOAT);              /* a in FPR */
    vrott(3);                  /* [a, b, c], vtop=c */
    gv(RC_FLOAT);              /* c in FPR */
    vrott(3);                  /* [c, a, b], vtop=b */
    gv(RC_FLOAT);              /* b in FPR */
    /* End: vstack [c, a, b]. vtop[0]=b, vtop[-1]=a, vtop[-2]=c. */
    b_slot = vtop[0].r & VT_VALMASK;
    a_slot = vtop[-1].r & VT_VALMASK;
    c_slot = vtop[-2].r & VT_VALMASK;
    dst_slot = get_reg(RC_FLOAT);
    gen_ppc_fmadd(dst_slot, a_slot, b_slot, c_slot, dbl);
    vtop -= 3;
    vpushi(0);
    vtop->type.t = type.t;
    vtop->r = dst_slot;
    break;
}
```

### dd_two_prod simplification (`lib-ppc.c`)

```c
static void dd_two_prod(double a, double b, double *p, double *e)
{
    *p = a * b;
    *e = __builtin_fma(a, b, -*p);     /* exact thanks to single-rounding fmadd */
}
```

The Veltkamp split + 4-product fallback is no longer needed when
fma is available.

## Verification

| Check | Pre | Post |
|---|---|---|
| `__builtin_fma()` compiles | ✅ | ✅ |
| `__builtin_fma()` runs | ❌ (Symbol not found) | ✅ |
| Disasm contains `fmadd` (not `bl _fma`) | n/a | ✅ |
| `libtcc1.a` `dd_two_prod` uses fmadd | n/a | ✅ |
| dd-precision invariants (5/5) | ✅ | ✅ |
| tests2 | 111/111 | 111/111 |
| abitest-cc | 24/24 | 24/24 |
| abitest-tcc | 24/24 | 24/24 |
| libtest, dlltest | pass | pass |
| Bootstrap fixpoint | holds | holds |

Released as **v0.2.36-g3**.

## Bug hit during the work

First attempt computed `a*c + b` instead of `a*b + c`. Cause: my
gv-rotate-gv-rotate-gv pattern left vstack in `[c, a, b]`
(NOT `[b, c, a]` as I'd assumed when reading vtop[0]/vtop[-1]/
vtop[-2]). Fixed by re-tracing the rotate states and reassigning
the slots correctly. Always trace these out with a state diagram —
the indices are off-by-three from "natural order" because vstack
top is the LAST entry, not the first.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents — this was small enough to keep in head.

## Closing note

Five releases in one evening. The fmadd work is the smallest of
them by impact (no test stages newly passing) but the most
satisfying because the v0.2.34 dd_two_prod immediately benefits
from it, and the implementation is short enough to fit in one
screen of code per file. A clean cap to the LD-related work that
started in v0.2.32.
