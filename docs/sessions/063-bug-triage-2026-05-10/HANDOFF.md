# Handoff — end of session 063 (2026-05-10)

## TL;DR

Single 4-line fix in `tcc/ppc-gen.c` (added `save_reg()` calls in
the struct-by-value path of `gfunc_call`) closed **4 of the 5**
real bugs that session 062 surfaced — including all three SEGVs
(1536, 8271, 8389) and one output-diff (1705).

Regression suite all green: fixpoint holds, tests2 111/111, abitest
every test passes including all `*struct*` and `*union*` cases.

* HEAD at session start: `5783198` ("docs/sessions/062: bswap shim
  + clz/ctz UB-guard for csmith --builtins"), `main`, in sync with
  origin.
* HEAD at session end: this docs commit + the `tcc/ppc-gen.c` fix.
* Tag: `v0.2.47-g3` already on origin. The fix is post-tag —
  consider tagging `v0.2.48-g3` once seed-8255 is also closed (or
  decide to ship a "4 of 5 closed" tag if seed-8255 takes a while).

## Status of the 5 bugs

| Seed | Source | Pre-fix symptom | Status |
|---|---|---|---|
| 1536 | default-opts | tcc SIGSEGV (gcc OK) | ✅ FIXED |
| 8271 | builtins+bitfields | tcc SIGSEGV | ✅ FIXED |
| 8389 | builtins+bitfields | tcc SIGSEGV | ✅ FIXED |
| 1705 | default-opts | tcc≠gcc checksum | ✅ FIXED |
| 8255 | builtins+bitfields | tcc≠gcc checksum | ❌ residual |

All 5 ORIGINAL csmith-generated programs were tested against the
fixed tcc; the first 4 now produce gcc-equivalent output, the 5th
still diverges at `g_181[i]`.

## What landed (proposed commit)

| Tag | Commit | Headline |
|---|---|---|
| (no tag) | session 063 | ppc-gen: gfunc_call save_reg before struct-word loads — closes 4 of 5 csmith bugs from session 062 |

Two changes:
- `tcc/ppc-gen.c` — added 4-line save_reg() loop in the struct
  case of gfunc_call, before `gv(RC_INT)` materializes the struct
  address.
- `docs/sessions/063-bug-triage-2026-05-10/` — this session
  directory with README, analysis, minimal repro, and HANDOFF.

The session-062 reductions in `docs/sessions/062-.../seed-{1536,8271}-repro/`
were left at their last creduce state. They drifted into different
bugs during reduction; the residual SIGBUS / r3=17 crash in the
147-line seed-1536 reduced testcase is a separate codepath — not a
priority since the original (the actual user-facing bug) is fixed.

## Open work for next session

### 1. seed-8255 — only remaining "real" bug

builtins+bitfields output-diff. First divergent global is
`g_181[i]`:

```
gcc: ...checksum after hashing g_181[i] : 7DF4B4D6
tcc: ...checksum after hashing g_181[i] : 111DC160
```

Source on ibookg38: `/Users/macuser/tmp/csmith-builtins-8020/seed-8255.c`.
Compile with `-include /Users/macuser/tmp/builtin_compat.h
/Users/macuser/tmp/bswap_compat.c -I/Users/macuser/tmp/csmith-runtime`.

Standard manual narrowing flow (find writes to `g_181[i]`, etc.)
or fresh creduce against an output-diff predicate targeting that
line. Different bug class from the just-fixed gfunc_call issue —
don't assume any particular structural shape.

### 2. (Maintenance, deferred from session 062 #4)
   Ship updated harness to both hosts

The local copy at
`docs/sessions/062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh`
gained `EXTRA_CC_HDR` support. Hosts still have the older harness.

```sh
scp -q docs/sessions/062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh \
       imacg3:/Users/macuser/tmp/csmith_campaign.sh
scp -q docs/sessions/062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh \
       ibookg38:/Users/macuser/tmp/csmith_campaign.sh
```

### 3. (Maintenance, deferred from session 062 #5)
   Patch tcc's libtcc1.a clz/ctz to match gcc-PPC

Still backlog. Becomes more attractive now that the codegen-bug
backlog is mostly drained:

After the patch, the `builtin_compat.h` shim becomes unnecessary
and the 6 UB-FP seeds (8034, 8068, 8084, 8228, 8312, 8356) pass
without `-include`. Test plan: rebuild libtcc1.a, fixpoint check,
tests2, abitest, then re-run those 6 seeds without the shim and
confirm.

### 4. Run a fresh csmith campaign post-fix

The default-opts campaign on imacg3 reached 783/1500 with both
fixed bugs as its only FAILs. The remaining 717 generation +
compile + run cycles are likely to be 0-FAIL since we're now past
the gfunc_call bug. But running a fresh campaign — say 1000 seeds,
default-opts AND --builtins — would build confidence in the fix
and possibly surface anything else that's been masked.

### 5. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
  — would have made today's investigation an order of magnitude
  faster (no manual prologue-pattern hunting). Worth doing.
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

## How to pick up

### Sanity baseline

Both hosts should have the new tcc binary (rebuilt during session
063); verify on each:

```sh
ssh imacg3   # or ssh ibookg38 with PATH=/usr/bin: prefix on builds
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.47-fp-arg-shadow.sh
```

### Pick up the bug-hunt

If pursuing seed-8255 directly, the entry point is the
per-variable hash diff above. If pursuing #4 (fresh campaign), use
the campaign harness (after #2 ships) at
`/Users/macuser/tmp/csmith_campaign.sh` on either host.

If neither sounds appealing, OSO STAB is the highest-leverage
infra investment — it pays for itself the first time you debug a
csmith SEGV.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. One in-session author (Opus 4.7).

## Closing notes

Session 062 surfaced 5 distinct real bugs; session 063 closed 4 of
them with a single 4-line patch. The pattern that surfaced the
biggest payoff was: pick the smallest concrete reproducer, run it
under gdb, follow the wild pointer back through the disassembly to
find what set it.

The remaining seed-8255 is a different bug class — likely worth
its own focused session rather than bolt-on triage.

Next session: [docs/sessions/063-bug-triage-2026-05-10/HANDOFF.md](docs/sessions/063-bug-triage-2026-05-10/HANDOFF.md)
