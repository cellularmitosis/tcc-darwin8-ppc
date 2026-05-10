# Handoff — end of session 059 (2026-05-09)

## TL;DR

Re-ran session 058's `--float` campaign shape (seeds 3000–3300)
against the v0.2.47 binary on imacg3: **PASS=264, FAIL=1, SKIP=36**.
The only FAIL is **seed 3259**, the previously-documented UB
fold for `(uint32_t)(-2.25)` (gcc-4.0 → 0; tcc → `0xFFFFFFFE`
matching runtime `fctiwz`). Seeds 3228 + 3265 — the FP-arg
GPR-shadow clobber repros that drove v0.2.47 — now PASS,
confirming the fix holds across the full input set. **No new
codegen bugs surfaced.** Wrote `findings.md` capturing the
fctiwz design call as a durable record so future sessions
don't re-litigate it.

* HEAD at session start: `ef191d4` (session 058 docs commit),
  already on `origin/main`. **No prior unpushed work to push.**
* Tag: `v0.2.47-g3` already on origin.
* Net unpushed since session start: **one** docs-only commit
  (this session's session-059 dir).

## What landed

| Tag | Commit | Headline |
|---|---|---|
| (no tag) | (this session's docs commit) | **docs/sessions/059: csmith batch 4 + fctiwz design call.** README + commits.md + findings.md + this HANDOFF. No source changes; one campaign run on imacg3 (~21 min wall) replicating session 058 batch4 input against the v0.2.47 binary. Validates the v0.2.47 fix (3228/3265 now PASS) and confirms seed 3259 is the documented UB-shape, not a new bug. |

## Open work for next session

### 1. Continue csmith bug-hunting

This session's campaign found 0 new bugs (1 expected FAIL = the
documented UB shape). That's a clean signal that the
shape-3000-3300 input set is now exhausted post-v0.2.47.
Productive next campaigns vary the shape:

* **Combined `--float --max-funcs 12 --max-block-depth 5`** to
  union session 058's campaign B shape (which found seed 2187,
  the gcc-4.0 fold bug) with batch4's float emphasis. Most
  likely to surface the next FP-related arg-passing bug class.
* **`--float --max-funcs 16 --max-block-depth 6`** to push
  expression depth past what batch4 covered. Mostly orthogonal
  to the above.
* **Re-run default opts** (no `--float`) past seed 1500 — that
  arm has been quiet but the sample was small (501 seeds in 058).

Recipe (uses the same harness scripts already on imacg3):

```sh
ssh imacg3
mkdir -p /Users/macuser/tmp/csmith-batch5
for seed in $(seq 4000 4300); do
    csmith --no-volatiles --no-packed-struct --float \
           --max-funcs 12 --max-block-depth 5 \
           --max-array-dim 2 --max-array-len-per-dim 5 \
           --seed $seed -o /Users/macuser/tmp/csmith-batch5/seed-$seed.c
done
nohup /opt/tigersh-deps-0.1/bin/bash \
    /Users/macuser/tmp/csmith_campaign.sh 4000 4300 \
    /Users/macuser/tmp/csmith-batch5 \
    /Users/macuser/tmp/csmith-out-batch5 \
    > /Users/macuser/tmp/campaigns-059/batch5.log 2>&1 &
```

Expect ~25–35 min wall on the G3 (deeper programs = more
gcc-4.0 timeouts, longer tail).

### 2. fctiwz / float-to-uint32 design call — closed

See [`findings.md`](findings.md). Decision: keep tcc's current
`fctiwz`-equivalent semantics. Revisit only if this shape's
false-alarm rate exceeds 1% in a campaign or a real program is
found to depend on the gcc-4.0 clamp-to-zero behavior.

### 3. Csmith with `--enable-builtin-kinds` (still gated)

Still blocked on the `bswap_compat.h` shim. See session 056
HANDOFF item #3 for full notes.

### 4. (Maintenance) factor `gfunc_call`'s clobber-protection

Three "arg shuffle / clobber" bugs in `gfunc_call` over recent
months (v0.2.16 LL-shuffle, v0.2.28 func-ptr-LVAL, v0.2.47
FP-shadow). Each followed the same pattern: pass 2 writes to a
hardcoded register without consulting tcc's vstack.

Concrete refactor sketch (looked at the code this session,
ppc-gen.c:1473–1912):

* **Small refactor** (~30 LoC): a helper
  `save_target_gprs(gpr_idx_lo, gpr_idx_hi)` that calls
  `save_reg(i)` for each idx in `[lo,hi]`. The 5 existing
  `save_reg(...)` callsites (LL straddle, LL-both-in-GPR,
  FP-shadow float, FP-shadow double-both, FP-shadow
  double-straddle) become single calls. **Yield: low** —
  same number of bugs, just shorter lines.
* **Bigger refactor** (~80 LoC): make pass 1 also build a
  per-arg "GPRs-this-arg-will-write" mask (currently only
  GPR slot starts are tracked). Then before pass 2, iterate
  vtop→vbottom and `save_reg` any vstack entry whose register
  is in the union-of-future-arg masks. **Yield: high** — the
  three historical bugs share the same root cause (forgetting
  to spill a not-yet-processed arg from a target reg), and a
  declarative pre-pass eliminates the class entirely.

Not urgent — existing fixes are local and tested. Worth doing
the moment a fourth bug of this shape lands.

### 5. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
# imacg3's git history is stale (HEAD=62511c4) but the
# working-tree tcc/* + demos/v0.2.4{6,7}* files md5-match
# uranium HEAD byte-for-byte. The on-disk tcc binary at
# tcc/tcc is built from v0.2.47 (May 9 20:18). Either:
#   (a) accept the stale-history state and just rebuild, or
#   (b) rsync from uranium to clean it up:
#         (uranium) ~/bin/tiger-rsync.sh --delete \
#             /Users/cell/claude/tcc-darwin8-ppc/ \
#             imacg3:tcc-darwin8-ppc/
#       — and accept that you lose imacg3-side .git state.
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

The csmith differential-testing arc has now produced **three
real codegen bug fixes in three campaigns** (v0.2.45 BE bitfield,
v0.2.46 float-int const-fold, v0.2.47 FP-arg shadow clobber)
followed by **one quiet validation campaign** (this session).
Quiet is good — it means the prior fixes covered the input set
and we're ready to widen the shape.

The fctiwz design call is now durable docs rather than a
conversation thread; future campaigns flagging seed 3259's shape
should be batch-dismissed without re-investigation.

Next session: [docs/sessions/059-csmith-batch4/HANDOFF.md](docs/sessions/059-csmith-batch4/HANDOFF.md)
