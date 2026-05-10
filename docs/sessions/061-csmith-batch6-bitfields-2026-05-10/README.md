# Session 061 — csmith batches 6 + 7 (bitfields re-sweep, two-host parallel)

**Date:** 2026-05-10
**Author:** Opus 4.7 (1M context)

## Arrival state

* HEAD: `8a80deb` ("docs/sessions/060: csmith batch 5 (deeper FP shape)") on
  `main`, in sync with `origin/main`.
* Last codegen change: v0.2.47 (FP-arg GPR-shadow clobber fix, session 058).
* Two consecutive clean csmith campaigns post-v0.2.47 (sessions 059 and
  060) — the union of `--float` × deeper-funcs/blocks is mined out.
* Per session 060's HANDOFF, the recommended next axis is the
  **`--bitfields` re-sweep** — well-defined, no harness changes,
  validates the v0.2.45 BE-bitfield fix against the current binary.
* csmith's default is no bitfields, so all five prior campaigns
  effectively turned that surface off. Re-enabling it is the
  natural regression check for v0.2.45.
* **New mid-session:** the user brought **ibookg38** (iBook G3
  900 MHz, Tiger 10.4.11 PowerPC) online. Second G3 in the test
  fleet — opens up parallel-host campaigns. This session bootstraps
  it and runs a complementary shape there in parallel with the
  imacg3 run.

## Plan

Two campaigns, run on different hosts in parallel:

* **Batch 6 — imacg3 — bitfields, default depth.** Validates v0.2.45
  against a broad seed range; the 1-of-5 prior campaigns axis we
  haven't re-swept since the BE-bitfield fix.

  ```
  --no-volatiles --no-packed-struct --bitfields \
  --max-funcs 6 --max-block-depth 3 \
  --max-array-dim 2 --max-array-len-per-dim 5
  ```
  Seeds 5000–5300, output `csmith-out-batch6`.

* **Batch 7 — ibookg38 — bitfields, deeper.** Stresses the
  bitfield-codegen path with deeper / larger programs that have
  more chances to surface struct-with-bitfield-passed-by-value
  or returned-by-value interactions with the v0.2.47 GPR-shadow
  fix.

  ```
  --no-volatiles --no-packed-struct --bitfields \
  --max-funcs 12 --max-block-depth 5 \
  --max-array-dim 2 --max-array-len-per-dim 5
  ```
  Seeds 6000–6300, output `csmith-out-batch7`.

Wall time expected ~25 min on imacg3 (default depth) and ~30–35 min
on ibookg38 (deeper, but ibookg38 is ~50% faster than imacg3 raw —
roughly cancels out).

After both runs, triage every FAIL:
* Reproduce on uranium if possible.
* Determine if it's a new tcc bug, a known UB shape, or a
  csmith/gcc-4.0 quirk.
* For any new tcc bugs: minimize, file in `findings.md`, decide
  if it warrants a v0.2.48 fix this session.

## What happened

### ibookg38 bring-up

Generated 301 batch-6 seeds locally on uranium with the default-depth
bitfield shape, rsync'd to imacg3, kicked the existing harness in
nohup. While that ran, set up ibookg38 from scratch:

1. `tiger.sh make-4.3` — installed GNU make 4.3 into `/opt/make-4.3`
   (the tcc Makefile uses GNU `$(or)` which the system make 3.80
   lacks). Note: the package name is `make-4.3`, not
   `install make-4.3` (different from some other tiger.sh wrappers).
2. `tiger-rsync.sh -a --delete --exclude='.git/' --exclude='demos/'
   --exclude='docs/' uranium:tcc-darwin8-ppc/ ibookg38:tcc-darwin8-ppc/`
   — copied the source tree (no need for git history on a test host).
3. Tarred `csmith_campaign.sh` + `csmith-runtime/` headers from
   imacg3 to ibookg38 over an ssh→ssh pipe.
4. `make clean`, `rm -f c2str.exe` to purge uranium's x86_64 build
   artifacts that the rsync brought along; `configure` + `make`
   to build the v0.2.47 PowerPC binary natively.

Build initially failed with `dyld: incompatible cpu-subtype` on the
`ar` invocation. Diagnosis: ibookg38 reports
`hw.cpusubtype 9` (CPU_SUBTYPE_POWERPC_750, G3), but
`/opt/cctools-667.3/bin/ar` was built with `cpusubtype 11`
(CPU_SUBTYPE_POWERPC_7450, G4e with AltiVec). All `/opt/cctools-*`
binaries on ibookg38 are similarly G4-only — even `otool` fails to
load, so we used `/usr/bin/otool` to inspect the broken ones.
Workaround: prepend `/usr/bin` to PATH for the build —
Apple's `/usr/bin/{ar,ranlib,nm,ld,strip,libtool}` are universal
binaries with a working ppc slice that runs on G3.

```
PATH=/usr/bin:/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/usr/bin:/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
```

After that: `tcc -v` reports `0.9.28rc (PowerPC Darwin)`, and the
self-hosting fixpoint holds (`tcc-self2.o == tcc-self3.o`, 635370
bytes — byte-identical to imacg3's binary).

This quirk is captured in memory under `host_ibookg38.md` so future
sessions don't re-discover it.

(Why imacg3 doesn't hit this: imacg3's `/opt/cctools-667.3/bin/ar`
*is* cpusubtype 9 — somehow the same tigerbrew-derived path
contains different arch builds on the two machines. Possibly due
to when each was installed.)

### Campaigns

#### Batch 6 — imacg3 (default depth + bitfields)

```
PASS=253  FAIL=0  SKIP=48
```

* **0 FAILs.** v0.2.45's BE-bitfield fix holds across the full
  300-seed sweep with bitfields re-enabled.
* All 48 SKIPs are `gcc-timeout` — none are
  `gcc-compile-fail`, `tcc-timeout`, or `tcc-compile-fail`.
* Wall: ~23 min on the G3, in line with the 25-min prediction.
* SKIP rate 16% — slightly higher than the ~12–14% from prior
  default-depth campaigns. Bitfield programs may exercise a few
  more pathologically-slow gcc paths; the rate is still well
  inside expected variance.

#### Batch 7 — ibookg38 (deeper + bitfields)

```
PASS=245  FAIL=1  SKIP=55
```

* **1 FAIL — seed 6240 — verified as csmith-generated UB**, not a
  tcc bug. See [`findings.md`](findings.md) for the full triage.
  The compiler matrix shows even gcc-4.0 -O0 vs -O2 disagree on
  this program; tcc landing on a third answer is consistent with
  unspecified-eval-order rather than a codegen miscompile. The
  cause is the canonical csmith pitfall: a deeply-nested call
  expression where the same global (`g_21`) is written by both an
  argument's side-effect and the called function's body, with
  unspecified evaluation order.
* All 55 SKIPs are `gcc-timeout`. None are `tcc-timeout`,
  `tcc-compile-fail`, or `gcc-compile-fail`.
* Wall: ~50 min on ibookg38 (started 02:01, finished ~02:51).
  Notably slower per-seed than batch 6's default-depth shape, even
  though ibookg38 is 50% faster than imacg3 raw — the deeper
  programs more than absorb the speed advantage.

### Combined result

```
total seeds: 600 (300 default-depth + 300 deep, all with bitfields)
PASS: 498   FAIL: 1 (csmith UB)   SKIP: 103 (all gcc-timeout)
real tcc bugs surfaced: 0
```

The bug-hunt arc remains healthy. Three real fixes (v0.2.45, v0.2.46,
v0.2.47) were followed by three consecutive clean campaigns — sessions
059 (`--float` re-run), 060 (FP×deep union), and 061 (bitfields, two
shapes, two hosts). The harness is still working; the input space we've
explored is genuinely empty of low-hanging tcc-codegen bugs at these
shapes.

## Exit state

* HEAD unchanged at session start (`8a80deb`); this session adds one
  docs-only commit (the session-061 dir, including the seed-6240 UB
  artifact).
* No source changes. No demos. No release.
* **ibookg38** is now a fully-functional second test host: tcc v0.2.47
  built natively, fixpoint holds, harness installed. It will be the
  default host for future deeper / longer campaigns; imacg3 stays as
  the canonical "first-pass" host. The G3-vs-G4 cctools quirk is
  captured in memory (`host_ibookg38.md`).
* imacg3 state preserved: batch-6 SUMMARY + 48 SKIP-seed artifacts in
  `/Users/macuser/tmp/csmith-out-batch6/`; campaign log at
  `/Users/macuser/tmp/campaigns-061/batch6.log`.
* ibookg38 state preserved: batch-7 SUMMARY + 55 SKIP-seed and 1 FAIL-seed
  artifacts in `/Users/macuser/tmp/csmith-out-batch7/`; campaign log
  at `/Users/macuser/tmp/campaigns-061/batch7.log`. Seed inputs at
  `/Users/macuser/tmp/csmith-batch7/`.
* Local copies of both SUMMARY files plus the seed-6240 source live in
  this session dir for offline triage.

## Files in this session dir

* `README.md` — this narrative.
* `findings.md` — seed-6240 UB triage and the running known-UB-seeds list.
* `SUMMARY-batch6.txt` — verbatim batch 6 (imacg3) campaign summary.
* `SUMMARY-batch7.txt` — verbatim batch 7 (ibookg38) campaign summary.
* `seed-6240.c` — the csmith program flagged as a FAIL but verified UB.
* `commits.md` — commits landed.
* `HANDOFF.md` — pickup notes for the next session.
