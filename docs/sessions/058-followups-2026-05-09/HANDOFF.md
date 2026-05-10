# Handoff — end of session 058 (2026-05-09)

## TL;DR

Triaged the 4 csmith FAILs from the post-v0.2.46 campaigns
(A, B, C running on imacg3 from session 056). One was a
**real tcc bug** — `gfunc_call`'s FP-arg GPR-shadow code
clobbered a previously computed int arg in r4. Shipped
**v0.2.47-g3** with a 3-line `save_reg` fix (~15 lines with
comments). Other three FAILs classified: one is a gcc-4.0
constant-fold bug, two were the same v0.2.47 bug (different
seeds), and the remaining one (3259) is a known
implementation-defined float-to-uint32 conversion difference
consistent with our v0.2.46 fctiwz-semantics design choice.

* HEAD: `a8a6dfb` — local; **NOT pushed**.
* Tag: `v0.2.47-g3` at `a8a6dfb` — local; **NOT pushed**.
* Net unpushed since the last push (origin/main = `2aabd35`):
  * `679f137` — ppc-gen: FP-arg GPR-shadow save_reg fix
  * `a8a6dfb` — demos + roadmap: v0.2.47
  * tag `v0.2.47-g3`

(Note: session 057's HANDOFF claimed 6 unpushed commits + tag
v0.2.46-g3 at session start. **That was stale.** `git status`
this session showed local main matching origin/main; v0.2.46-g3
was already on origin too. So the cumulative unpushed set is
just this session's two commits + new tag.)

## What landed

| Tag | Commit | Headline |
|---|---|---|
| `v0.2.47-g3` | `679f137` (1) | **`gfunc_call` FP-arg GPR-shadow no longer clobbers previously computed int arg.** Pre-fix, `f((x &= helper(...)), 5.0f)` with `f` having signature `(uint32_t, float)` lost the int arg's value: arg2's variadic GPR-shadow `lwz r4, 16(r1)` overwrote r4 (which held the `&=` result), and pass 2's later `mr r3, r4` then put the float bit pattern in r3 where the int should have been. Fix calls `save_reg(gslot)` before the shadow `lwz` (and `gslot+1` for VT_DOUBLE both-in-GPR), matching what the LL straddle / LL-both-in-GPR paths already do. Verified: tests2 111/111, abitest clean, bootstrap fixpoint holds, 9 release demos pass, csmith C-float seeds 3228 + 3265 now match gcc-4.0, csmith default-opts spot check 42 PASS / 0 FAIL on seeds 1000-1050. |
| (no tag) | `a8a6dfb` (1) | **demos + roadmap for v0.2.47.** `demos/v0.2.47-fp-arg-shadow.{c,sh}` covers 8 call shapes; roadmap `Where we are` bumped to v0.2.47-g3. |

## Open work for next session

### 1. Push commits + tag (needs user OK)

Two commits + one tag this session. If parity with prior sessions
is desired:

```sh
git push origin main
git push origin v0.2.47-g3
```

### 2. Continue csmith bug-hunting (high-yield)

Three real codegen bugs in three campaigns since v0.2.45 (BE
bitfield, float-int const-fold, FP-arg shadow clobber). Each
was a small, well-localized fix with a strong minimal repro.
Worth running another batch.

Reasonable next pass on imacg3:

```sh
# Shapes to hit:
# - More C (--float) since v0.2.47 closed two of three FAILs
# - --max-funcs 16 --max-block-depth 6 to push deeper than
#   campaign B's --max-funcs 12 --max-block-depth 5
# - Combined --float --max-funcs 12 to union B's and C's triggers

ssh imacg3
mkdir -p /Users/macuser/tmp/campaigns-058

# Generate seeds
for seed in $(seq 3300 3500); do
    csmith --no-volatiles --no-packed-struct --float \
           --max-funcs 6 --max-block-depth 3 \
           --max-array-dim 2 --max-array-len-per-dim 5 \
           --seed $seed -o /Users/macuser/tmp/csmith-batch4/seed-$seed.c
done

# Run campaign
/opt/tigersh-deps-0.1/bin/bash /Users/macuser/tmp/csmith_campaign.sh \
    3300 3500 /Users/macuser/tmp/csmith-batch4 \
    /Users/macuser/tmp/csmith-out > /Users/macuser/tmp/campaigns-058/C2-float.txt
```

### 3. seed 3259-style float-to-uint32 divergence — design call

tcc's compile-time `(uint32_t)(-2.25)` gives `0xFFFFFFFE` (= int32(-2)
reinterpreted) while gcc-4.0 gives 0. C99 calls it UB so neither is
wrong, but gcc-4.0's choice produces fewer csmith false alarms.

**Recommendation: keep current behavior** (consistent with runtime
`fctiwz`, documented in v0.2.46 findings). Only revisit if csmith
floods us with this shape — current rate is about one per
campaign-of-300, fine to triage manually.

### 4. Csmith with `--enable-builtin-kinds` (still gated)

Still blocked on the `bswap_compat.h` shim alternative (gcc-4.0
doesn't have `__builtin_bswap32`/`64`). See session 056 HANDOFF
item #3 for full notes.

### 5. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

### 6. (Maintenance) factor `gfunc_call`'s clobber-protection

Three "arg shuffle / clobber" bugs in `gfunc_call` over recent
months (v0.2.16 LL-shuffle, v0.2.28 func-ptr-LVAL, v0.2.47 FP-shadow).
Each followed the same pattern: pass 2 writes to a hardcoded
register without consulting tcc's vstack. A small refactor to
centralize the "spill-target-before-clobber" idiom would make
future cases easier to spot. Not urgent — the existing fixes are
local and tested — but worth a future session.

## How to pick up

### Sanity baseline

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

The csmith differential testing arc is paying for itself.
Three small, local fixes from three campaigns in three sessions —
v0.2.45, v0.2.46, v0.2.47 — each landing one real codegen bug
that no other test caught. The harness on imacg3 is in good
shape; we should keep queuing campaigns at the cadence of "a few
hundred seeds per work block, triage in the next."

The arg-passing class of bugs in `gfunc_call` is now the
recurring shape; the maintenance item to factor it is open.

Next session: [docs/sessions/058-followups-2026-05-09/HANDOFF.md](docs/sessions/058-followups-2026-05-09/HANDOFF.md)
