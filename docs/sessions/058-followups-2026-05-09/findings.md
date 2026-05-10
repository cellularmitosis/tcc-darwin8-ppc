# Session 058 findings

## seed 2187 (csmith B-complex) is a gcc-4.0 bug, not a tcc bug

### Triage

Campaign B (`--max-funcs 12 --max-block-depth 5`, seeds 2000-2300)
flagged one real divergence: seed 2187 — `gcc=FAD400D4`,
`tcc=31266415`. Final value of global `g_59` (uint16_t) was
`gcc=1, tcc=3` (initial). Drilled in via `print_hash_value=1`
trace + control-flow printfs:

* Both compilers enter `func_9(p_10=1, p_11=-5)` with identical
  args.
* Both evaluate the outer `for (g_172 = 0; g_172 <= 0; g_172++)`
  identically.
* They diverge at the very next `if (...)` statement: gcc takes
  the true branch (which contains the chain of `for (g_176)` /
  `for (g_183)` / `for (g_59)` writes); tcc takes the false branch.

### Root cause: gcc-4.0 constant-folds an unsigned/signed
### comparison incorrectly

The if-condition (after the side-effecting LHS of a comma) reduces
to `65528UL >= p_11` with `p_11` being `int8_t = -5`.

Per C semantics:
1. Integer-promote `p_11` (signed char) to `int` → -5.
2. Usual conversion vs `unsigned long`: convert `int` to
   `unsigned long`. (unsigned long)(-5) = 0xFFFFFFFB on PPC32.
3. Compare: 65528 >= 4294967291 → **false**.

Minimal repro (in `seed-2187-min/cmp.c` of this session dir):

```c
#include <stdio.h>
int main(int argc, char **argv) {
    signed char p_11 = -5;
    int side = argc;
    int j = ((side++, 65528UL) >= p_11);
    int k = ((side, 65528UL) >= p_11);
    int l = ((side = 1, 65528UL) >= p_11);
    int m = ((argc, 65528UL) >= p_11);
    printf("j=%d k=%d l=%d m=%d\n", j, k, l, m);
    return 0;
}
```

| Compiler | Output |
|---|---|
| gcc-4.0 (Tiger PPC), -O0..-O3 | `j=1 k=1 l=1 m=1` (**wrong**) |
| Apple clang 17 (uranium) | `j=0 k=0 l=0 m=0` (correct) |
| tcc (this tree) | `j=0 k=0 l=0 m=0` (correct) |

Asm inspection of gcc-4.0's `cmp.s` shows it stores `1` as a
compile-time constant for j/k/l/m. So the bug is in gcc-4.0's
constant folder: when the LHS of `>=` is a constant
`unsigned long` reached through a comma operator, and the RHS is
a `signed char` variable, gcc-4.0 picks signed comparison
instead of the unsigned-conversion path the standard requires.

This matches a class of gcc-4.0-era constant-folding bugs around
"unsigned vs narrow-signed" comparisons; csmith's preference for
deeply-nested comma+arithmetic expressions is what surfaced it.

### Why earlier campaigns missed it

Campaign A (default opts, --max-funcs 6 --max-block-depth 3)
generates shallower expressions; the comma+const+narrow-signed
shape needs deeper nesting to land. Campaign B's
`--max-funcs 12 --max-block-depth 5` increases both the
expression complexity and the count of `int8_t` parameters
threaded through nested calls, which is what csmith needs to
generate the trigger.

### Decision

Treat seed 2187 as a known gcc-4.0 reference-compiler bug.
Continue using gcc-4.0 as the differential reference for future
campaigns. If we get more output-diffs of this shape, the recipe
to confirm "gcc bug, not tcc" is:

1. Reproduce the divergence with `print_hash_value=1`.
2. Find the first divergent global; instrument writes; localize
   to one C expression.
3. Cross-check the expression on uranium with clang. If clang
   matches tcc, gcc-4.0 is the outlier — file under "gcc-4.0 fold
   bug" and move on.

The csmith test harness on imacg3 has no notion of this — it
naively compares gcc against tcc. We could add a third reference
(gcc-10.3 if we get its assembler issues fixed, or a
cross-built clang) but the false-alarm rate is currently very low
(1 in 800+ seeds across campaigns A and B post-v0.2.46), so it's
not worth the engineering churn yet.

## Campaign A re-validation (post-v0.2.46)

Same option set as session 056's pre-fix campaign that found
v0.2.45 (BE bitfield) and v0.2.46 (float-int const-fold).

* Seeds 1000-1500 (501 seeds), default csmith opts.
* PASS=432, FAIL=0, SKIP=69 (all gcc-timeouts on infinite-loop
  programs — the existing skip arm correctly handles them).
* No tcc regressions. v0.2.46 is stable on the option set that
  found it.

## Campaign B (post-v0.2.46, larger programs)

* Seeds 2000-2300 (301 seeds), `--max-funcs 12 --max-block-depth 5`.
* PASS=268, FAIL=1 (seed 2187, the gcc-4.0 fold bug above),
  SKIP=32.
* No tcc regressions.

## Campaign C (post-v0.2.46, --float, no --builtins)

* Seeds 3000-3300 (301 seeds), `--float --max-funcs 6 --max-block-depth 3`.
* PASS=262, FAIL=3 (seeds 3228, 3259, 3265), SKIP=36.
* Triage:
  * **3228**: real tcc bug — `gfunc_call` FP-arg shadow clobber.
    Fixed in v0.2.47 (`679f137`). See below.
  * **3265**: same bug as 3228; agrees with gcc-4.0 after the fix.
  * **3259**: implementation-defined float-to-uint32 conversion.
    `g_231 = (-0x1.2p+1)` (i.e. `(uint32_t)(-2.25)` via static
    initializer). gcc-4.0 → 0. tcc → 0xFFFFFFFE = `(uint32_t)(int32_t)-2`,
    matching the runtime `fctiwz` semantics we standardized on in
    v0.2.46. C99 calls this case UB; both compilers are conformant.
    Not a tcc regression — see "Open work item #3" in the session
    058 HANDOFF for whether to chase it.

## seed 3228 — real tcc bug — FP-arg GPR-shadow clobbers earlier int arg

### Triage

`g_48` (uint8_t) was the first divergent global: gcc=244, tcc=2.
Drilled in via per-global hash trace, then ENTER printfs in
`func_2`/`func_5`/`func_28`. Both compilers entered the three
functions with identical arg values, so the bug wasn't in the
call ordering. The divergence was in `func_2`'s `if (p_3)`
branch decision: gcc had `p_3 = 0` (took the `else` branch with
the `for (g_48 = (-12); ...)` loop), tcc had `p_3 = 1084227584`
(took the `if` branch — no g_48 mod).

`1084227584 = 0x40A00000` is the float bit pattern for `5.0f`,
which is the value of `func_2`'s **second** arg (p_4). So tcc
was getting the float arg's bit pattern into r3 (where the
uint32_t arg p_3 should be). Wrapping the call in a temp arg
extraction made the bug disappear. **Argument passing bug.**

### Minimal repro

```c
#include <stdio.h>
unsigned x = 7;
unsigned helper(int a, int b, int c, int d) { return 0; }
void f(unsigned a, float b) {
    printf("a=%u b=%f\n", a, (double)b);
}
int main(void) {
    unsigned char c = 5;
    f((x &= helper(1, 2, 3, 4)), c);
    return 0;
}
```

Pre-fix output (tcc): `a=1084227584 b=5.000000`
Post-fix (and gcc-4.0): `a=0 b=5.000000`

Probing variants narrowed the trigger to:

* arg1 must contain a function call (so r3 gets clobbered with
  return value).
* arg1 must be a compound assignment (`&=`, `|=`, `^=`, `+=`,
  `-=`); plain `=` doesn't trigger because the result lands in
  r3 directly. Compound ops first read x, then call helper which
  clobbers r3, then `&` etc. — leaving the result in r4 (since
  r3 is "in use" by helper's return).
* arg2 must be a float / double / int-promoted-to-float (so the
  variadic GPR shadow code runs).

### Root cause

tcc's `gfunc_call` (ppc-gen.c:1473) uses a two-pass model: pass 1
assigns each arg a GPR / FPR slot per the Apple PPC32 ABI; pass 2
walks args from `vtop` downward, materializing each into its
slot. For a `(uint32_t, float)` signature:

* arg1 goes in r3 (gslot=0).
* arg2 goes in f1 (fslot=0); for variadic compatibility the FP
  arg is also "shadowed" into r4 (gslot=1) — so a variadic callee
  walking GPRs sees the float bits.

Pass 2 processes vtop first, so **arg2 is materialized before
arg1**. The FP-arg shadow code emits:

```
stfs f1, 16(r1)       ; spill float to scratch slot
lwz  r{shadow}, 16(r1)  ; reload as int into shadow GPR
```

That `lwz` is hardcoded to a specific GPR (r4 here) without
consulting tcc's regalloc state. arg1's value happens to live
in r4 because the `&=` left it there — and the `lwz` overwrites
it.

Pass 2 then processes arg1 with `gv(RC_R(0))`, which sees the
SValue's `r=4` (= r4) and emits `mr r3, r4` to move arg1 into
its target slot. But r4 now holds the float bit pattern, not
arg1's value.

### Fix

Add `save_reg(gslot)` before each shadow `lwz` in `gfunc_call`'s
FP-arg branch. `save_reg(idx)` walks the vstack and spills any
entry currently using GPR `idx` to a stack local — exactly the
pattern the LL straddle and LL-both-in-GPR paths already use
(see ppc-gen.c:1789, 1857). For VT_DOUBLE both-in-GPR, save
both `gslot` and `gslot+1`.

The single-line additions:

```c
/* float, gslot < 8 */
save_reg(gslot);

/* double, gslot+1 < 8 (both halves in GPR) */
save_reg(gslot);
save_reg(gslot + 1);

/* double, gslot < 8 (straddling r10/stack) */
save_reg(gslot);
```

The other paths (float gslot >= 8, double gslot >= 8 — both
halves on stack) don't need it; they don't write any GPR.

### Verification

* Minimal repro `f(uint32, float)`, plus 8 probe variants, plus
  VT_DOUBLE and `f(uint32, float, float)` shapes — all PASS.
* csmith seed-3228 + seed-3265 outputs now match gcc-4.0.
* tests2: 111/111.
* abitest-cc / abitest-tcc: success across the board (no FAILs).
* Bootstrap fixpoint: holds.
* All 9 release demos pass.
* csmith default-opts spot check seeds 1000-1050: 42 PASS, 0 FAIL.

### Class of bug

This is the **third** "arg shuffle / clobber" bug in `gfunc_call`:

* v0.2.16: LL-arg shuffle clobber when re-targeting r3:r4 pair.
* v0.2.28: `save_regs(nb_args)` (was `nb_args+1`) so the func-ptr
  LVAL's volatile-GPR address survives arg setup.
* v0.2.47: this — FP-arg shadow `lwz` overwrites a GPR holding
  an earlier-computed arg value.

The pattern is consistent: pass 2 of arg materialization writes
to a hardcoded register without consulting tcc's vstack. We're
catching them one at a time as csmith shapes get more complex.
A factoring pass in `gfunc_call` to centralize the
"spill-target-before-clobber" logic would make future cases
easier to find — but not urgent; the existing fixes are local
and tested.
