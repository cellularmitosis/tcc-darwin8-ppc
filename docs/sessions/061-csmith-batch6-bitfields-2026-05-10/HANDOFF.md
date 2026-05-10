# Handoff — end of session 061 (2026-05-10)

## TL;DR

Ran two csmith campaigns in parallel across two G3 hosts:

* **Batch 6** on imacg3 — `--bitfields` default-depth, seeds 5000–5300:
  PASS=253, FAIL=0, SKIP=48. Validates v0.2.45 BE-bitfield fix.
* **Batch 7** on ibookg38 (newly online) — `--bitfields --max-funcs 12
  --max-block-depth 5`, seeds 6000–6300: PASS=245, FAIL=1, SKIP=55.
  The single FAIL (seed 6240) is **csmith-generated unspecified-eval-order
  UB**, not a tcc bug — gcc-4.0 -O0 vs -O2 also disagree. See
  [`findings.md`](findings.md).

Net 600 seeds, **0 real tcc bugs**. Three consecutive clean campaigns
post-v0.2.47 (059, 060, 061) plus the bitfield re-sweep validates that
the current input shapes are mined out.

Bonus: ibookg38 bootstrapped from bare-metal mid-session — a second G3
test host (~50% faster than imacg3) is now available for parallel
campaigns. The G4-vs-G3 cctools quirk it has is captured in memory
(`host_ibookg38.md`).

* HEAD at session start: `8a80deb` ("docs/sessions/060: csmith batch 5
  (deeper FP shape)") on `main`. **One prior unpushed commit** —
  session 060's docs-only commit.
* Tag: `v0.2.47-g3` already on origin.
* Net unpushed at end of this session: **two** docs-only commits
  (session 060's pre-existing one + this session's session-061 dir).
  Both are docs-only; safe to push together when convenient.

## What landed

| Tag | Commit | Headline |
|---|---|---|
| (no tag) | (this session's docs commit) | **docs/sessions/061: csmith batches 6+7 (bitfields, two-host parallel).** Two campaigns, two hosts, 600 seeds, 0 real tcc bugs. ibookg38 brought online and validated. |

## Open work for next session

### 1. Pick the next genuinely new axis

After three consecutive clean campaigns we should now treat the
**existing csmith axis space as well-mined**. The remaining
high-value axes from session 060's HANDOFF:

* **`--enable-builtin-kinds`** (highest expected yield). Still gated on
  the `bswap_compat.h` shim work (session 056 HANDOFF item #3).
  Unblocking this is the higher-leverage move than another stock-options
  campaign — it enables `__builtin_bswap{16,32,64}`, `__builtin_popcountl`,
  etc. which exercise paths gcc-4.0 would reject.
* **Variadic-heavy programs.** csmith doesn't generate variadic callers
  by default. Lower yield (variadic surface area is small in tcc's PPC
  backend and well-tested via abitest). Skip unless other axes are
  exhausted.
* **Re-run default opts past seed 1500** (carry-forward from 059).
  Quiet arm, low expected yield. Cheap and easy parallel work for
  ibookg38 if we want a "background sweep" running.

**Recommendation:** the bswap-shim work is the right next investment.
The bug-hunt arc has reached a saturation point — every remaining
csmith shape costs wall-time without surfacing real bugs. Investing
in `--enable-builtin-kinds` opens a **new bug class** rather than
re-sweeping the same one. Once unblocked, run a campaign with
`--enable-builtin-kinds` × bitfields × default-depth as the first
sweep there.

If we don't want to commit to the shim work yet, run the
"default-opts past seed 1500" sweep on ibookg38 in the background as
a **0-effort parallel arm** — it doesn't compete with foreground
investigations and may surprise us.

### 2. Use ibookg38 by default for csmith from now on

ibookg38 is faster and now fully set up. Future campaigns should
default to it; imacg3 stays as the canonical "first build" /
sanity-baseline host. For really long campaigns (1000+ seeds, deep
shapes), split the seed range across both hosts.

The campaign recipe is identical to before, just point at ibookg38:

```sh
# uranium
mkdir -p /tmp/csmith-batch8
for seed in $(seq 7000 7300); do
    csmith --no-volatiles --no-packed-struct --bitfields \
           --max-funcs 6 --max-block-depth 3 \
           --max-array-dim 2 --max-array-len-per-dim 5 \
           --seed $seed -o /tmp/csmith-batch8/seed-$seed.c
done
~/bin/tiger-rsync.sh -a /tmp/csmith-batch8/ ibookg38:/Users/macuser/tmp/csmith-batch8/

# ibookg38
ssh ibookg38
mkdir -p /Users/macuser/tmp/campaigns-NNN
nohup /opt/tigersh-deps-0.1/bin/bash \
    /Users/macuser/tmp/csmith_campaign.sh 7000 7300 \
    /Users/macuser/tmp/csmith-batch8 \
    /Users/macuser/tmp/csmith-out-batch8 \
    > /Users/macuser/tmp/campaigns-NNN/batch8.log 2>&1 &
```

**Important — the ibookg38 build needs `PATH=/usr/bin:` prefixed**
because `/opt/cctools-667.3/bin/` is built for G4 (subtype 11), but
ibookg38 is G3 (subtype 9). Apple's `/usr/bin/{ar,ranlib,nm,ld,...}`
are universal binaries that include a working ppc slice. The harness
itself is unaffected (it just calls gcc-4.0 and tcc directly), but
any `make` invocation needs the prefix:

```sh
PATH=/usr/bin:/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/usr/bin:/opt/make-4.3/bin:$PATH FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
```

This quirk is captured in the `host_ibookg38.md` memory.

### 3. (Maintenance) factor `gfunc_call`'s clobber-protection

Carry-forward from 059/060 HANDOFF. Same recommendation: not urgent —
do it the moment a fourth bug of this shape lands. Sketch lives in
059 HANDOFF.

### 4. Consider three-compiler oracle for the harness

After two known-UB false-positive seeds (3259 in session 058, 6240
in this session), the harness's "tcc disagrees with gcc-4.0" oracle
is starting to show its limits. A pre-filter that requires both
gcc-4.0 -O0 and gcc-4.0 -O2 to agree before declaring tcc the bug
would catch unspecified-eval-order UB shapes for free. **Not yet
worth the wall-time** — adding two extra gcc compiles per seed
roughly doubles the campaign duration. Reconsider if a third UB
seed lands.

### 5. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

## How to pick up

### Sanity baseline

On either host (both have the v0.2.47 binary built and validated):

```sh
ssh imacg3   # or ssh ibookg38 (with PATH=/usr/bin: prefix on builds)
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.47-fp-arg-shadow.sh
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
```

(Both hosts validated this baseline at session start.)

### Pick up the bug-hunt

The arc has saturated for the existing axes. Three consecutive clean
campaigns post-v0.2.47 (across seven distinct input shapes)
plus this session's parallel two-host run says: **the cheap remaining
moves are exhausted**. The next move is to either invest in the
bswap-shim work (opens `--enable-builtin-kinds`) or pivot to one of
the other roadmap items (OSO STAB, self-link diagnostics, AltiVec).

If you do start the bswap-shim work, also kick off a "default-opts
past seed 1500" sweep on ibookg38 in the background — zero overhead,
catches anything we missed in the original session-058 pass.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. One in-session author (Opus 4.7).

## Closing notes

This session executed cleanly on plan **and** unlocked a new piece of
infrastructure (ibookg38 as a second G3 host). The bug-hunt result is
"more confirmation, no new bugs" — which is the expected and correct
state after three real fixes and three consecutive validation passes.
Future investment should shift from re-sweeping known shapes toward
opening new ones (`--enable-builtin-kinds`) or toward roadmap items
that don't depend on csmith.

Next session: [docs/sessions/061-csmith-batch6-bitfields-2026-05-10/HANDOFF.md](docs/sessions/061-csmith-batch6-bitfields-2026-05-10/HANDOFF.md)
