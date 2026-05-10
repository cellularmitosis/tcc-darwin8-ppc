# Handoff — end of session 062 (2026-05-10)

## TL;DR

Built the bswap shim per session 061's HANDOFF and ran it. The
session finished with **5 distinct real tcc bugs** surfaced (the most
in any session since the v0.2.40-series unsupervised run), plus a
documented `clz/ctz(0)` UB false-positive class with a working
`builtin_compat.h` UB-guard.

* HEAD at session start: `2f7a7e6` ("docs/sessions/061: csmith
  batches 6+7"), `main`, clean tree, in sync with origin.
* No commits landed yet (this docs commit is the one to land).
* Tag: `v0.2.47-g3` already on origin.

### Real bugs (in priority order)

| Seed | Source | Symptom | Source lines | Reduction state |
|---|---|---|---|---|
| **1536** | default-opts | tcc SIGSEGV (gcc OK) | 615 | creduce on imacg3, 75 lines (~88% line-reduced, 9% byte-reduced); see `seed-1536-repro/test.c` |
| **8271** | builtins+bitfields | tcc SIGSEGV | 1261 | creduce on ibookg38, 105 lines (~92% line-reduced, 4% byte-reduced); see `seed-8271-repro/test.c` |
| **8389** | builtins+bitfields | tcc SIGSEGV | 1248 | not yet reduced |
| **1705** | default-opts | tcc=`6413675B`, gcc=`989E0C4E` | 419 | not yet reduced |
| **8255** | builtins+bitfields | tcc=`64984FA0`, gcc=`A2955E73` | 877 | not yet reduced; per-variable hash dump shows first divergence at `g_181[i]` |

3 of 5 are SIGSEGVs. They may share a wild-pointer codegen root cause
— if one reduction lands, retry the others against any in-tree fix
before reducing them independently.

### UB false-positive class (now neutralized)

The other 6 FAILs (8034, 8068, 8084, 8228, 8312, 8356) are all the
same shape: csmith `--builtins` emits `__builtin_clz(0)` /
`__builtin_ctz(0)` and tcc's libtcc1.a software impl returns a
different value than gcc-PPC's `cntlzw` for input 0. Both within
their rights per the gcc spec ("If x is 0, the result is undefined")
but our oracle treats it as a tcc bug because gcc-O0 == gcc-O2.

`builtin_compat.h` (in this session dir) UB-guards the calls and was
verified to fix all 6. From now on, any csmith `--builtins` campaign
on PPC should `-include` it.

## What landed (proposed commit)

| Tag | Commit | Headline |
|---|---|---|
| (no tag) | docs/sessions/062 | bswap shim + clz/ctz UB-guard for csmith `--builtins`; two parallel campaigns; 5 real bugs surfaced (3 SEGV, 2 output-diff). |

## Open work for next session

### 1. Continue / pick up the in-flight reductions

Both creduces are running in background on uranium, ssh-per-iteration.
At session close they were ~10% through their reduction passes. They
will keep grinding overnight.

```sh
# Check status:
tail /Users/cell/claude/tcc-darwin8-ppc/docs/sessions/062-bswap-shim-builtins-2026-05-10/seed-1536-repro/creduce.log
wc -l /Users/cell/claude/tcc-darwin8-ppc/docs/sessions/062-bswap-shim-builtins-2026-05-10/seed-1536-repro/test.c

tail /Users/cell/claude/tcc-darwin8-ppc/docs/sessions/062-bswap-shim-builtins-2026-05-10/seed-8271-repro/creduce.log
wc -l /Users/cell/claude/tcc-darwin8-ppc/docs/sessions/062-bswap-shim-builtins-2026-05-10/seed-8271-repro/test.c

# Any creduce processes still running?
pgrep -f creduce
```

If they've converged, the `test.c` in each dir is the reduced repro.
Open it, look at the bug pattern, audit the surrounding tcc codegen
path. Both predicates accept any signal kill (>=128) so the symptom
may have shifted from SIGSEGV to SIGBUS or SIGILL during reduction.

If creduce is still running but the user wants to move on, kill it
with `pkill -f creduce` and use whatever reduction it reached.

### 2. Triage the remaining real bugs

* **seed-8389** — builtins+bitfields SEGV, 1248 lines. Not yet
  reduced. If 1536 / 8271 reduce to a clear pattern, try compiling
  8389 against any in-tree fix before reducing independently — it
  may be the same bug.
* **seed-1705** — default-opts output-diff, 419 lines. Not yet
  reduced. Smallest output-diff bug; should be the easiest to
  manually narrow via the per-variable hash dump trick.
* **seed-8255** — builtins+bitfields output-diff, 877 lines. First
  divergent global is `g_181[i]`. Per-variable dump in
  `fails/seed-8255.{gcc,tcc}.out` (regenerated as needed).

For any of these, the standard manual narrowing flow is:

```sh
# Run with arg "1" to print per-variable CRC32 dump.
ssh ibookg38 './seed-8255.gcc 1 > /tmp/g; ./seed-8255.tcc 1 > /tmp/t; diff /tmp/g /tmp/t | head'

# First divergent line points at the first global tcc gets wrong.
# Find the writes to that global, narrow to a tiny program manually
# OR build a creduce predicate that targets that specific divergence.
```

### 3. Finish the default-opts campaign on imacg3

At session close it was at 268/1000 — much slower than projected
because the seed-1536 creduce was contending for the host. After the
creduce finishes, the campaign should pick back up speed.

```sh
ssh imacg3 'wc -l /Users/macuser/tmp/csmith-out-default-1500/SUMMARY.txt
            grep ^FAIL /Users/macuser/tmp/csmith-out-default-1500/SUMMARY.txt'
```

Any new default-opts FAILs are real bugs (no UB-shim issues in this
arm). Triage with the same gcc-O0/O2 sanity check.

### 4. (Maintenance) Ship the updated harness to both hosts

The local copy at `docs/sessions/062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh`
gained `EXTRA_CC_HDR` support. Hosts still have the older harness.
After their current campaigns finish, ship the new one:

```sh
scp -q docs/sessions/062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh \
       imacg3:/Users/macuser/tmp/csmith_campaign.sh
scp -q docs/sessions/062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh \
       ibookg38:/Users/macuser/tmp/csmith_campaign.sh
```

### 5. (Backlog) Patch tcc's libtcc1.a clz/ctz to match gcc-PPC

The shim approach in this session unblocks csmith without modifying
tcc — that was the right call for a session focused on opening the
arm. The cleaner long-term fix is a 10-line patch to
`tcc/lib/builtin.c` short-circuiting `x == 0` in CLZI / CLZL /
CTZI / CTZL macros to gcc-PPC's UB-result (clz: 32/64; ctz: -1/-1/31
for int/long/long long).

Justification: tcc-on-PPC should match gcc-on-PPC's UB behavior for
binary compatibility — real-world PPC code may rely on `cntlzw`
semantics for `__builtin_clz(0)`. After the patch, the
`builtin_compat.h` shim becomes unnecessary.

Test plan: rebuild libtcc1.a, fixpoint check, tests2, abitest, then
re-run the 6 UB-FP seeds without the shim and confirm they pass.

### 6. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

## How to pick up

### Sanity baseline

Both hosts have v0.2.47 binaries and are validated.

```sh
ssh imacg3   # or ssh ibookg38 (with PATH=/usr/bin: prefix on builds)
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.47-fp-arg-shadow.sh
```

### Pick up the bug-hunt

The single highest-value next move is to land at least one of the
SEGV reductions and audit the corresponding tcc codegen path. Three
parallel SEGVs (1536, 8271, 8389) almost certainly share a root
cause. One fix likely closes all three.

If reductions have converged: read the reduced source, identify the
codegen pattern, find it in `tcc/ppc-gen.c` or `tcc/tccgen.c`, fix.
If not: let creduce keep running, work on seed-1705 manually in
parallel.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. One in-session author (Opus 4.7).

## Closing notes

Sessions 059/060/061 were "0 real bugs" validation sweeps after the
v0.2.47 cluster. Session 062 broke that pattern by opening the
`--builtins` axis (and the carry-forward default-opts arm) and finding
**5 distinct real bugs** plus a documented UB false-positive class.

The bug-hunt arc is alive again, with clear next moves: finish
reductions, audit codegen, fix the SEGV class. Don't pivot to roadmap
items (OSO STAB, AltiVec) yet — there's high-yield bug-fix work in
the queue.

Next session: [docs/sessions/062-bswap-shim-builtins-2026-05-10/HANDOFF.md](docs/sessions/062-bswap-shim-builtins-2026-05-10/HANDOFF.md)
