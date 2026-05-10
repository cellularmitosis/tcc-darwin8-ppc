# Findings — session 061

## seed 6240 — csmith-generated UB (not a tcc bug)

The single FAIL of session 061's two-host campaign (batch 7 on
ibookg38, seed 6240, output-diff) reproduces on imacg3 too — so it
is not a host-specific quirk. But it is **not a tcc bug**: the
program triggers unspecified evaluation-order behavior that even
different versions / opt-levels of gcc-4.0 disagree on.

### Compiler matrix on seed 6240

| Compiler | Opts | `checksum =` |
|---|---|---|
| gcc-4.0 (Apple, build 5370) | -O0 | `12EBF78E` |
| gcc-4.0 | -O2 | `1D3D9DD` |
| gcc-4.0 | -O3 | `1D3D9DD` |
| gcc-4.9.4 (tigerbrew /opt) | -O0 | `12EBF78E` |
| gcc-4.9.4 | -O2 | `12EBF78E` |
| tcc v0.2.47 (PowerPC) | (default) | `C53DC3FC` |

Three of these results are pairwise distinct. When **gcc-4.0 -O0
and gcc-4.0 -O2 disagree**, the only valid conclusion is that the
program is exercising unspecified or undefined behavior — the
compiler is allowed to interpret it any way. tcc landing on a
fourth answer (`C53DC3FC` happens to coincide with no other
compiler we tried) is therefore consistent with tcc making a
different-but-still-legal choice, not a codegen bug.

### Why the harness flagged it

The csmith harness at `/Users/macuser/tmp/csmith_campaign.sh`
runs differential testing **only** between gcc-4.0 (the reference)
and tcc (the SUT). It does not cross-check gcc-4.0 against itself
at multiple opt levels, nor against gcc-4.9.4. It cannot
distinguish "tcc disagrees with gcc-4.0" from "the program has UB
and any disagreement is meaningful." This is the same
limitation that surfaced for session 058's seed 3259
(`(uint32_t)(-2.25)` UB).

For now we accept the false-positive cost; adding a
"three-compiler oracle" pre-filter is recipe-tractable but not yet
worth the wall-time cost it would add to every campaign (cross-
compiling each PASS twice through gcc-4.0 -O0 and -O2 and
verifying agreement before declaring tcc the bug). If FAILs become
common in future campaigns we should reconsider.

### What in the program causes the UB

The first divergent global is `g_21` (an `int8_t`). The full diff
shows divergence cascading from there onward — every subsequent
global derives from the same execution path. The decisive
assignment is in `func_1`, line 142 of the seed:

```c
(*l_2) = ((+(**l_4)) <= (&g_3 != ((*g_115) =
  (l_22 = func_6(((*g_1535) = func_12(g_18, g_18.f1, (**l_4), &l_2,
    (g_18.f0 != ((*l_22) = ((((*l_2) , ((g_21 =
      (safe_rshift_func_int16_t_s_u(g_18.f2, 12)))
      && g_21)) , (**l_4)) && 0x52L))))),
  g_268[2][3].f1, g_369.f0, l_1869, (*l_2))))));
```

Two unspecified-order issues are bundled here:
1. `g_21` is written by **arg5's side effect**
   (`g_21 = safe_rshift(g_18.f2, 12)`) AND by **func_12's body**
   (per the read/write annotation on line 195: func_12 writes
   `g_21`). Argument-evaluation order is unspecified in C99/C11,
   so different compilers can produce either order. The final
   value of `g_21` therefore depends on implementation choice.
2. `(*l_22) = ...` and `(l_22 = ...)` both happen in the same
   full expression. That's the textbook "unsequenced
   modify-and-access" pattern.

csmith's design generally tries to avoid UB via its `safe_*`
wrappers, but those only cover individual operators (overflow,
shift-by-too-much). They don't catch unspecified-order issues
across function-argument lists or pointer-aliasing in compound
assignments. With `--bitfields --max-funcs 12 --max-block-depth 5`
csmith generates expressions deep enough that these slip in.

### Action items

* **No source change.** The bug is in the test program, not in
  tcc.
* **Add seed 6240 to the documented known-UB list** (this file).
  Future campaigns that include seed 6240 in their range can
  expect a FAIL on this seed and ignore it.
* The pattern (`--bitfields --max-funcs 12 --max-block-depth 5`)
  remains a valuable shape — it surfaces real UB-prone idioms
  that future tcc work needs to be robust against. Don't drop
  the shape just because it produces false positives.

### Known-UB seeds (cumulative)

| Seed | Shape | Session | UB pattern |
|---|---|---|---|
| 3259 | `--float` | 058 | `(uint32_t)(-2.25)` — float-to-uint32 with negative source, implementation-defined per C99 |
| 6240 | `--bitfields --max-funcs 12 --max-block-depth 5` | 061 | unspecified eval-order: `g_21` written via both arg5 side-effect and inside func_12 body in same call expression |

If a third UB seed lands in a future campaign, consider whether
the harness should grow a "known-UB exclusion" file, or whether
the cost of a three-compiler oracle pre-filter has finally become
worth it.
