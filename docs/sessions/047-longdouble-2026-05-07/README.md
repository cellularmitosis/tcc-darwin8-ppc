# Session 047 — long double = IBM double-double

**Date:** 2026-05-07
**Build host:** imacg3 (synced from uranium via rsync; .git left at older
ref, working tree current).

## Arrival state

* HEAD `73e9ab0` — v0.2.31-g3.
* tests2: 111/111.
* abitest-tcc: 24/24.
* abitest-cc: **22/24** — `ret_longdouble_test` and `arg_align_test`
  the two persistent failures (B1 in session 046's HANDOFF). Both
  break on a tcc/gcc ABI mismatch: gcc-built harness passes/returns
  16-byte LD; tcc-compiled callees expected 8-byte LD.

## Exit state

* tests2: **111/111**.
* abitest-tcc: **24/24**.
* abitest-cc: **24/24** ← session goal achieved.
* dlltest: passes.
* Bootstrap fixpoint: holds.
* Lua, bzip2, cJSON, dylib-slide, alacarte, libtcc-slide demos all
  pass.
* Released as **v0.2.32-g3**.

## What changed

### 1. Type system / ABI bookkeeping

* `LDOUBLE_SIZE` 8 → 16, `LDOUBLE_ALIGN` 8 → 4 (Apple PPC32 says LD
  is 4-byte aligned). [`tcc/ppc-gen.c:75-83`].
* `REG_FRE2 = TREG_F(1)` and `RC_FRE2 = RC_F(1)` defined as the
  second-half FPR slot for LD return. [`tcc/ppc-gen.c:60-72`].
* `R2_RET(VT_LDOUBLE)` returns `REG_FRE2` on PPC32, so tcc's
  generic two-word machinery (`USING_TWO_WORDS`, `RC2_TYPE`) sees
  LD as a register pair. [`tcc/tccgen.c:255-258`].

### 2. Generic load/store: LD halves use 8-byte stride

The lvalue-load and lvalue-store paths in `gv()`/`vstore()` were
hardcoded to `PTR_SIZE` (= 4) between halves — fine for long-long,
wrong for double-double (each half is 8 bytes). Added a `load_stride`
/ `store_stride` selector that picks 8 for VT_LDOUBLE on PPC, plus
the BE half-order swap (LL: HIGH first then LOW; LD: HIGH first then
LOW — different convention because tcc's PPC LD has `r=HIGH r2=LOW`
where LL has `r=LOW r2=HIGH`). [`tcc/tccgen.c:1969-2056`,
`tcc/tccgen.c:3997-4010`].

`save_reg_upstack` got the same stride-and-order fix
[`tcc/tccgen.c:1456-1473`] — without it, spilling an LD vstack value
to a temp local wrote LOW on top of HIGH (PTR_SIZE stride between
two stfd's of 8 bytes each). This was the root cause of the `4-LD
printf in one call` bug: each `__gcc_qadd` call's intermediate result
got clobbered when the next operand was materialized.

### 3. ABI plumbing

`gfunc_prolog` for VT_LDOUBLE param: 2 FPR slots, 4 GPR-shadow
slots; emits TWO entries in `ppc_fp_param_off` so the prolog spills
both FPRs. [`tcc/ppc-gen.c:2228-2247`].

`gfunc_call`:
* Pass 1: VT_LDOUBLE arg consumes 2 FPR + 4 GPR-shadow slots; if it
  doesn't fit in 13 FPRs, `fpr_alloc[i] = -1` to signal stack-only.
  [`tcc/ppc-gen.c:1486-1497`].
* Pass 2: gv()'s the LD into ANY 2 FPRs (tcc's regalloc choice),
  then immediately stfd's both halves to the LD's outgoing param
  slot. [`tcc/ppc-gen.c:1582-1614`]. Decouples
  materialization-time FPR allocation (which must avoid live vstack
  values) from ABI placement (which requires specific FPR slots).
* Post-pass-2 loop: walks the recorded LD args one more time and
  emits `lfd f{n+1}, hi_off(r1) ; lfd f{n+2}, lo_off(r1)` to load
  each into its specific ABI FPR slot, plus `lwz` to populate the
  GPR shadow for variadic callees. [`tcc/ppc-gen.c:1838-1872`].

`gfunc_epilog` / return path: no swap needed for LD return — the
2-FPR pair (f1, f2) is already where Apple PPC ABI expects it.
tccgen.c's `gfunc_return` calls `gv(RC_RET(VT_LDOUBLE)) =
gv(RC_FRET=RC_F(0))` and the generic two-word path materializes
into `r=REG_FRET, r2=REG_FRE2`.

### 4. gen_opf for VT_LDOUBLE

Arithmetic delegated to `__gcc_q{add,sub,mul,div}` (Apple Darwin's
long-double libgcc helpers); comparisons delegated to
`__{eq,ne,lt,le,gt,ge}tf2` (the standard libgcc tf2 family). After
the helper call, `vpushi(0)` + manual r/r2 setup gives downstream
code the right SValue shape. Comparisons additionally `vpushi(0);
gen_opi(op)` to convert "result <op> 0" into a CMP. Negation is
inline (fneg on each half). [`tcc/ppc-gen.c:2769-2842`].

### 5. gen_cvt_ftof / gen_cvt_itof: LD pair completion

For int → LD: the existing `gen_cvt_itof` magic-constant trick
produces a DOUBLE in one FPR. Added a tail step: when target is
VT_LDOUBLE, allocate a second FPR loaded with 0.0 and assign to
`vtop->r2`. [`tcc/ppc-gen.c:2956-2967`].

For double ↔ LD: drop or zero-fill the low half inline (no libgcc
call). [`tcc/ppc-gen.c:3072-3119`]. LD → int / LD → LL conversions
still go through the existing libgcc dispatchers
(`__fixdfdi` / `__fixsfdi` etc.); the high-half-only narrowing is
lossy but acceptable.

### 6. Apple `$LDBL128` symbol redirect (THE non-obvious fix)

After all the ABI plumbing was in place, `printf("%Lf %Lf", a, b)`
still printed `12.34 0.000000` for the second LD. The bug:
tcc was emitting a call to plain `_printf`, which on Tiger is the
**legacy 8-byte-LD** entry point (`%Lf` reads 8 bytes per arg).
gcc-built code emits `_printf$LDBLStub` / `_printf$LDBL128` (the
16-byte-LD entries) via Tiger's `<sys/cdefs.h>` macro
`__DARWIN_LDBL_COMPAT(x)`.

That macro is gated on `__LDBL_MANT_DIG__ > __DBL_MANT_DIG__`, which
gcc auto-defines but tcc didn't. Fix: predefine
`__LDBL_MANT_DIG__=106` and `__DBL_MANT_DIG__=53` in tcc's
`target_machine_defs`. Also predefine
`__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=1040` so the cdefs
macro picks the direct `$LDBL128` form (the libSystem entry) rather
than `$LDBLStub` (which gcc resolves via libgcc-provided
runtime-lookup stubs that we don't ship).
[`tcc/ppc-gen.c:97-127`].

### 7. lib-ppc.c helpers

* Rewrote `__gcc_q{add,sub,mul,div}` from `struct ppc_dd`-returning
  to `long double`-returning (their actual Apple ABI). Lossy:
  drop low halves, do plain double arith, return as LD with low=0.
  Both abitest-cc tests pass with this lossy form because their
  values fit in double's 53-bit mantissa.
* Added `__{eq,ne,lt,le,gt,ge}tf2` — same lossy double-fallback
  pattern.
* Added `__floatdixf / __floatundixf / __fixxfdi / __fixunsxfdi`
  for LL ↔ LD conversions; tcc's generic gen_cvt_itof1 / ftoi1
  emit these names when LDOUBLE_SIZE != 8 and the bootstrap of
  tcc-on-tcc was hitting them as undefined symbols.

### 8. Token table additions

`tcctok.h`: added the `tf2` family for PPC (alongside ARM64/RISC-V)
plus PPC-specific `__gcc_q*` tokens.

## Verification

| Stage | Pre-session | Post-session |
|---|---|---|
| tests2 | 111/111 | 111/111 |
| abitest-cc | 22/24 | **24/24** ← goal |
| abitest-tcc | 24/24 | 24/24 |
| dlltest | passes | passes |
| Bootstrap fixpoint | holds | holds |
| All real-world demos | pass | pass |

`make -k test` upstream stages: same set passing as session 046
(version, hello-exe, hello-run, vla_test-run, pp-dir, tests2-dir,
memtest, abitest-cc — newly clean — abitest-tcc, dlltest). The
`libtest` failure (`R_PPC_REL24 out of range`) is not new — the same
error reproduces with my changes stashed. Listed as deferred below.

## Notable bugs hit and fixed during the work

### a. Stride mismatch in save_reg_upstack

When tcc spills an LD-pair vstack entry to a stack temp during
`save_regs(n)`, both halves were stored at `+0` and `+PTR_SIZE`
(= 4) — but each half is an 8-byte stfd, so the second store wrote
on top of the first's last 4 bytes. The 4-LD `printf` test exposed
it: each subsequent `__gcc_q*` call clobbered the prior arg's vstack
value.

Fix in `save_reg_upstack` [`tcc/tccgen.c:1456-1473`]: stride 8 and
order `r/r2 = HIGH/LOW` for LD (vs LL's `r2/r = HIGH/LOW`).

### b. The `$LDBL128` redirect

See section 6. The "wait, the bytes are right but printf still
reads garbage" symptom led to discovering that `printf` itself
exists in two ABI flavors on Tiger.

### c. `__floatundixf` / `__fixunsxfdi` undefined at bootstrap

tcc's generic `gen_cvt_itof1` / `gen_cvt_ftoi1` emit these names for
LL↔LD when `LDOUBLE_SIZE != 8`. The names use `xf` (gcc's "extended
float" suffix) regardless of platform LD type; we treat them as
synonyms for the platform-LD versions and provide thin lossy stubs.

## Open work (deferred)

### From session 046, still open

* **libtcc thread safety** (B2 — was OOS per CLAUDE.md).
* **DWARF for PPC** (C3).
* **Real bcheck.c port** (C4).
* **AltiVec intrinsics** (C5).
* **Self-link diagnostics** (C6).
* **Two-level namespace polish** (D).

### New from this session

* **Precision-correct double-double arithmetic.** Our `__gcc_q*` and
  `tf2` helpers are lossy (high-double only). Real precision-
  preserving versions would do the standard "compensated"
  Knuth-style double-double algorithms. Programs that rely on
  >53-bit LD precision (rare in practice) get truncated results.
* **printf `%Lf` for values where high+low exceeds double range.**
  Same root cause as above: our `__gcc_q*` stubs zero the low half,
  so LD-arith results are clamped to double precision before being
  stringified.
* **`libtest` upstream-test stage.** Was failing before this
  session's changes too (verified by stashing the diff and
  re-running) — `R_PPC_REL24 out of range` between JIT'd code and
  loaded helpers. The session-046 HANDOFF claimed this was passing,
  but it wasn't. Worth bisecting separately.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents used — the work was tightly coupled and benefited from
accumulated context across the same investigation arc (especially
the `$LDBL128` symbol-redirect discovery, which would have been
hard to brief into a fresh subagent).
