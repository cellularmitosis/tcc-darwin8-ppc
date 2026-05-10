# Handoff — end of session 060 (2026-05-10)

## TL;DR

Ran csmith campaign #5: `--float --max-funcs 12 --max-block-depth 5`,
seeds 4000–4300, against the v0.2.47 binary on imacg3.
**PASS=258, FAIL=0, SKIP=43** — the cleanest campaign yet, two in a
row post-v0.2.47. The union shape of batch4 and session 058's
campaign B is now exhausted: deeper-with-FP doesn't find anything
new on top of what each axis already covered. To progress the
bug-hunt arc the next campaign needs a genuinely new axis (see
below). No source changes this session.

* HEAD at session start: `68f3fca` ("pulling in convos"), already
  on `origin/main`. **No prior unpushed work to push.**
* Tag: `v0.2.47-g3` already on origin.
* Net unpushed since session start: **one** docs-only commit
  (this session's session-060 dir).

## What landed

| Tag | Commit | Headline |
|---|---|---|
| (no tag) | (this session's docs commit) | **docs/sessions/060: csmith batch 5 (deeper FP shape).** README + commits.md + SUMMARY.txt + this HANDOFF. No source changes; one campaign run on imacg3 (~32 min wall) with shape `--float --max-funcs 12 --max-block-depth 5`, seeds 4000–4300. Result: 258 PASS / 0 FAIL / 43 SKIP. Two consecutive clean campaigns post-v0.2.47. |

## Open work for next session

### 1. Continue csmith bug-hunting — pick a genuinely new axis

Two consecutive clean campaigns (059 batch4 re-run + 060 deeper
union) signals the current axis is mined out. The next campaign
should vary something the prior five didn't:

* **`--bitfields`** (csmith default is no bitfields). The v0.2.45
  fix was a BE-bitfield bug; turning bitfields back on with the
  current binary is the natural regression sweep for that fix.
  Expect potential interaction with the FP-arg path (struct-with-
  bitfield-passed-by-value).
* **Variadic-heavy programs.** csmith doesn't generate variadic
  callers by default, but `--no-checksum --no-runtime`
  combinations + manual seed-driver edits are an option. Lower
  yield — variadic surface area is small in tcc's PPC backend
  and well-tested via abitest.
* **Re-run default opts past seed 1500** (session 059 HANDOFF
  item). Quiet arm; small sample (501 seeds in 058). Cheap to
  extend, low expected yield.
* **`--enable-builtin-kinds`** (still gated on `bswap_compat.h`
  shim — see session 056 HANDOFF item #3). Highest potential
  yield because it enables `__builtin_bswap{16,32,64}`,
  `__builtin_popcountl`, etc. which exercise paths gcc-4.0 would
  reject. Unblocking this is the higher-leverage move than
  another stock-options campaign.

**Recommendation:** start with `--bitfields` re-sweep (well-defined,
no harness changes, validates v0.2.45) and in parallel decide whether
to invest in the bswap-shim work to open the
`--enable-builtin-kinds` axis.

Recipe for a `--bitfields` campaign — run csmith on uranium, rsync
to imacg3, then invoke the existing harness:

```sh
# uranium
mkdir -p /tmp/csmith-batch6
for seed in $(seq 5000 5300); do
    csmith --no-volatiles --no-packed-struct \
           --bitfields \
           --max-funcs 6 --max-block-depth 3 \
           --max-array-dim 2 --max-array-len-per-dim 5 \
           --seed $seed -o /tmp/csmith-batch6/seed-$seed.c
done
rsync -a /tmp/csmith-batch6/ imacg3:/Users/macuser/tmp/csmith-batch6/

# imacg3
ssh imacg3
mkdir -p /Users/macuser/tmp/campaigns-061
nohup /opt/tigersh-deps-0.1/bin/bash \
    /Users/macuser/tmp/csmith_campaign.sh 5000 5300 \
    /Users/macuser/tmp/csmith-batch6 \
    /Users/macuser/tmp/csmith-out-batch6 \
    > /Users/macuser/tmp/campaigns-061/batch6.log 2>&1 &
```

Expect ~25 min wall (default depth, like sessions 055/058 A).

**Note for future-Claude:** csmith is on uranium (homebrew
`/opt/homebrew/bin/csmith`, version 2.3.0), **not on imacg3**.
Generate seeds locally and rsync them. The handoff in 059
implied the seed-loop ran on imacg3 — that's wrong; this
session re-discovered the actual workflow.

### 2. (Maintenance) factor `gfunc_call`'s clobber-protection

Carry-forward from 059 HANDOFF #4. Same recommendation: not urgent
— do it the moment a fourth bug of this shape lands. Sketch lives
in 059 HANDOFF.

### 3. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

### 4. Closed in 059, no action needed

* fctiwz / float-to-uint32 design call. See
  [`docs/sessions/059-csmith-batch4/findings.md`](../059-csmith-batch4/findings.md).

## How to pick up

### Sanity baseline (unchanged from 059)

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh   # fixpoint should hold
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.47-fp-arg-shadow.sh              # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
```

(Same imacg3 stale-history caveat as 059: HEAD on imacg3 is
`62511c4*`, but the working tree md5-matches uranium HEAD
byte-for-byte. Either accept or rsync from uranium with
`tiger-rsync.sh --delete`. The on-disk binary is v0.2.47.)

### Pick up the bug-hunt

The arc is healthy. Two-consecutive-clean is **expected** after
three real fixes — it means the existing input shapes are mined
out, not that the harness has lost its teeth. The next move is
shape selection (above), not harness changes.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. One in-session author (Opus 4.7).

## Closing notes

Two consecutive quiet campaigns close out the post-v0.2.47
validation question. The next forward step in the bug-hunt arc
is the bitfield re-sweep (cheap, well-defined) and/or
unblocking `--enable-builtin-kinds` (higher yield, requires
the bswap-shim work).

Next session: [docs/sessions/060-csmith-batch5-2026-05-10/HANDOFF.md](docs/sessions/060-csmith-batch5-2026-05-10/HANDOFF.md)
