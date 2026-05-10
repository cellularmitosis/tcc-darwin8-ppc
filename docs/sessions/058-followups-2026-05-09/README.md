# Session 058 — followups (2026-05-09)

## Arrival state

Picking up from session 057's HANDOFF. Three actionable items:

* Push 6 unpushed commits + tag `v0.2.46-g3` (needs user OK).
* Triage the 3 csmith campaigns that were running on imacg3 from
  session 056. A and B were already done at session start; C
  (`--float`) was still running.
* Csmith with `--enable-builtin-kinds` (still gated by gcc-4.0
  missing builtins).

Also still open from prior sessions: OSO STAB (#7.5), self-link
diagnostics (#7), AltiVec (#9), real bcheck.c port (#11).

## Plan

1. Pull A and B campaign results, triage any FAILs.
2. Wait for C to finish, triage its FAILs.
3. Write up findings; ask user about pushing.

## What landed

| Tag | Commit | Headline |
|---|---|---|
| `v0.2.47-g3` | `679f137` | **`gfunc_call`'s FP-arg GPR-shadow code now `save_reg`'s the target register before the shadow `lwz`.** Pre-fix, `f((x &= helper(...)), 5.0f)` lost the int arg's value when the float arg's variadic GPR shadow `lwz r4, 16(r1)` clobbered r4 (which held the `&=` result). Pass 2's later `mr r3, r4` then put float bit pattern in r3 instead of the int. Mirrors the existing LL-straddle / LL-both-in-GPR `save_reg` pattern. Surfaced via csmith --float seed 3228; minimal repro is 8 lines. tests2 111/111, abitest clean, bootstrap fixpoint holds, all 9 release demos pass; csmith C-float seeds 3228 + 3265 now match gcc-4.0. |
| (no tag) | `a8a6dfb` | **demos + roadmap for v0.2.47.** `demos/v0.2.47-fp-arg-shadow.{c,sh}` covers 8 call shapes (compound assign + helper call int arg × float / double / two-float arg2). Roadmap `Where we are` bumped to v0.2.47. |

## Live notes

### Campaign A (default opts, seeds 1000-1500)

PASS=432, FAIL=0, SKIP=69 (gcc-timeouts). No tcc regressions.

### Campaign B (--max-funcs 12 --max-block-depth 5, seeds 2000-2300)

PASS=268, FAIL=1 (seed 2187, output-diff), SKIP=32.

Triage of seed 2187: dropped `print_hash_value=1` traces, found
the divergent global is `g_59`, narrowed to `func_9`, then to a
specific if-condition `(side_effect_expr, 65528UL) >= p_11` where
`p_11` is `int8_t = -5`. Per C semantics, this should evaluate
to FALSE (unsigned promotion: `65528 >= 4294967291` is false).
**tcc says false (correct); gcc-4.0 says true (constant-fold bug).**
Apple clang 17 on uranium also says false, confirming tcc's
behavior is right.

Determination: gcc-4.0 reference-compiler bug, not a tcc bug.
Minimal repro at [`seed-2187-min/cmp.c`](seed-2187-min/cmp.c).
Recipe for future false-alarm triage in [findings.md](findings.md).

### Campaign C (--float, seeds 3000-3300)

PASS=262, FAIL=3 (seeds 3228, 3259, 3265), SKIP=36.

Triage:

* **seed 3228**: real tcc bug — FP-arg GPR-shadow clobber.
  Drilled in via per-global hash trace + control-flow printfs +
  arg-printing wrapper around the call site. Localized to a tcc
  miscompile of `f(complex_int_expr_with_helper_call, float_val)`:
  arg2's variadic GPR-shadow `lwz r4` clobbered arg1's value.
  Fixed in `679f137`.
* **seed 3265**: same bug as 3228; matches gcc-4.0 after the fix.
* **seed 3259**: implementation-defined float-to-uint32 conversion
  difference. Source has `g_231 = (-0x1.2p+1)` (double-to-uint32
  of -2.25). gcc-4.0 returns 0; tcc returns `0xFFFFFFFE` (= int32(-2)
  reinterpreted as uint32, per our v0.2.46 fctiwz-semantics design
  choice). C99 calls this case undefined behavior. **Not a tcc
  regression.**

## Outcome

* New tag: **v0.2.47-g3**.
* csmith differential testing on C-float dropped from 3 FAILs to
  1 FAIL (and that 1 is a known-divergent UB construct).
* Recipe for future "is this a real tcc bug or a gcc-4.0 ref bug"
  triage documented for next session.

## Open work for next session

### 1. Push status (already up-to-date, was a stale handoff claim)

`git status` showed local main matched `origin/main` at session
start — the "6 unpushed commits + tag v0.2.46-g3" item from
session 057's HANDOFF was actually already done. This session's
commits (`679f137`, `a8a6dfb`) and the new tag (`v0.2.47-g3`)
are unpushed; if parity with prior sessions is desired:

```sh
git push origin main
git push origin v0.2.47-g3
```

### 2. Continue csmith bug-hunting

A reasonable next pass:

* **More C (`--float`)**: seeds 3300-3600 with the same option
  set, since v0.2.47 closed two of three FAILs. Likely surfaces
  the next layer of FP-related codegen issues.
* **`--max-funcs 16 --max-block-depth 6`** to push deeper than
  campaign B. Larger programs surface more arg-passing /
  register-pressure issues like the one v0.2.47 fixed.
* **Combined `--float --max-funcs 12`**: union of B and C's
  triggers. May expose more arg-shuffle bugs.

`/Users/macuser/tmp/csmith_campaign.sh first last [src_dir]
 [out_dir]` is the existing harness; the campaign-wrapper that
queues multiple option sets is in `/Users/macuser/tmp/run_more_campaigns.sh`
(though that's for the option sets we've already run — write a
new one for the next batch).

### 3. seed-3259-style float-to-unsigned divergence — design call

Right now tcc's compile-time conversion of out-of-range floats
to unsigned int gives `0xFFFFFFFE` (signed int32 reinterpreted)
while gcc-4.0 gives 0 for the same input. C99 calls it UB so
neither is wrong. But it inflates csmith's false-alarm rate.

Two reasonable choices:

* **Match gcc-4.0**: fold `(uint32_t)negative_double` to 0
  unconditionally. Easy. Cuts csmith false alarms but is
  arbitrary.
* **Stay consistent with runtime**: keep the current behavior
  (which matches runtime fctiwz on PPC). Documented and
  intentional per v0.2.46's findings; csmith false alarms
  are tolerable at <1%.

Recommend keeping current behavior unless csmith floods us with
this shape; revisit if more than one campaign in three has
3259-style FAILs.

### 4. From earlier sessions, still open

* `--enable-builtin-kinds` csmith campaigns (gated on a
  `bswap_compat.h` shim — see s056 HANDOFF #3).
* OSO STAB emission for gdb-on-Tiger (roadmap #7.5).
* Self-link diagnostics (roadmap #7).
* AltiVec intrinsics (roadmap #9).
* Real bcheck.c port (roadmap #11).

## How to pick up

Sanity baseline — same as session 057's HANDOFF:

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
git fetch /Users/cell/claude/tcc-darwin8-ppc main   # or origin if pushed
git reset --hard FETCH_HEAD
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh   # FIXPOINT HOLDS
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.32-longdouble.sh                 # PASS
./demos/v0.2.33-libtcc-callback.sh            # PASS
./demos/v0.2.40-sed.sh                        # PASS
./demos/v0.2.41-gzip.sh                       # PASS
./demos/v0.2.42-python.sh                     # PASS
./demos/v0.2.43-gdebug-extern.sh              # PASS
./demos/v0.2.44-gdebug-link.sh                # PASS
./demos/v0.2.45-be-bitfield-abi.sh            # PASS
./demos/v0.2.46-float-int-const-fold.sh       # PASS
./demos/v0.2.47-fp-arg-shadow.sh              # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. One in-session author (Opus 4.7).

## Closing notes

The csmith bug-hunt arc is paying for itself. Three "real
divergence" campaigns since session 056:

* v0.2.45 (BE bitfield, session 055) — 1 real bug from 200 default-
  opt seeds.
* v0.2.46 (float-int const-fold, session 056) — 1 real bug from
  `--float --builtins`.
* v0.2.47 (this session, FP-arg shadow clobber) — 1 real bug from
  `--float --max-funcs 6`.

Plus 1 gcc-4.0 reference-compiler bug surfaced and classified
out of scope.

Each was a small, well-localized codegen fix with a strong
minimal repro. The harness on imacg3 (csmith → gcc/tcc compile
→ run → checksum diff → triage with `print_hash_value=1`) is the
right shape for this work; we should keep running campaigns at
the cadence of "queue a few hundred seeds at end of each work
block, triage in the next."

The arg-passing class of bugs (v0.2.16 LL-shuffle clobber,
v0.2.28 `save_regs(nb_args)` for func-ptr LVAL, this session's
FP-shadow clobber) keeps recurring because `gfunc_call` is the
busiest interaction point between tcc's regalloc and the PPC
ABI's fixed register requirements. Worth a future pass to factor
the "spill-target-before-clobber" pattern into one helper instead
of repeating it in each branch — but not urgent.

Next session: [docs/sessions/058-followups-2026-05-09/HANDOFF.md](docs/sessions/058-followups-2026-05-09/HANDOFF.md)
