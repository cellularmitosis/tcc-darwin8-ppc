# Handoff — end of session 071 (2026-05-13)

## TL;DR

Two-part session: (a) a small uranium-side docs cleanup closing a
loose end from session 070's `git mv` of the csmith harness — three
dangling markdown links in [session 064](../064-bswap-shim-prototypes-2026-05-10/)
repointed at the canonical `scripts/csmith/` location, forward-pointer
banners added to mirror the pattern session 070 used on sessions 062
and 069; and (b) a first **self-contained generator-host validation**
of ibookg37, the second working G3 in the fleet — fresh 200-seed
default-opts corpus generated on ibookg37 itself (no uranium
dependency) and run through the differential campaign.

* **HEAD at session start:** `015e6c9` (end of session 070).
* **HEAD at session end:** (this session's docs commit).
* **No tcc source change. No version bump. No new demo.**
* **csmith re-sweep:** seeds 9000-9199 (200 seeds), default opts,
  generated *and* run on ibookg37 with its native
  `/Users/macuser/csmith-2.3.0/bin/csmith` and post-v0.2.49 tcc.
  Result: **PASS=176 FAIL=0 SKIP=24** (all skips `gcc-timeout`).
  Comparable baseline from session 066 on ibookg37 with the
  uranium-generated 1000-seed default corpus was PASS=873 / FAIL=0
  / SKIP=127 (≈87.3% pass / 12.7% skip, also 100% gcc-timeout).
  Aggregate distribution within 0.7 pp.

## What landed

### Files edited

* [`docs/sessions/064-bswap-shim-prototypes-2026-05-10/README.md`](../064-bswap-shim-prototypes-2026-05-10/README.md)
  — forward-pointer "Note (session 071)" banner at top + two
  link-target redirects (`csmith_campaign.sh`, `bswap_compat.c`)
  pointing into `scripts/csmith/`.
* [`docs/sessions/064-bswap-shim-prototypes-2026-05-10/HANDOFF.md`](../064-bswap-shim-prototypes-2026-05-10/HANDOFF.md)
  — same banner pattern + `bswap_compat.c` link repointed.

### Files added

* [`docs/sessions/071-ibookg37-csmith-resweep-2026-05-13/README.md`](README.md)
  — session narrative.
* This `HANDOFF.md`.

### Host state (not in git)

* ibookg37: `/Users/macuser/tmp/bswap_compat.{c,h}` +
  `csmith_campaign.sh` refreshed from canonical `scripts/csmith/`
  copies (was May-11 deployment pre-session-069). Also generated
  `/Users/macuser/tmp/csmith-default-9000/seed-9000..9199.c` (200
  files, 27MB) and `/Users/macuser/tmp/csmith-out-071-default/`
  (SUMMARY.txt with PASS/FAIL/SKIP per seed). Generation took
  44m42s; campaign took 21m26s.

## Status of session 070's open items

| # | Item | Status |
|---|---|---|
| (070 #1) | **Default-opts csmith re-sweep on ibookg37** | **landed (this session)** |
| (070 #2) | OSO STAB / self-link / AltiVec / bcheck multi-session arcs | unchanged |
| (070 #3) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (latent 070 followup) | Fix 3 dangling links in session 064 from 070's `git mv` | **landed (this session)** |

## Open work for next session

### 1. (carried) OSO STAB / self-link / AltiVec / bcheck

Carried since session 067:

* (068 #2) Extend self-link diagnostics to `macho_output_dylib`
* (068 #3) Post-write linter (otool/nm sanity over emitted bytes)
* (068 #4) `-vv` diagnostic for reader's silent dropped relocs
* (067 #3) Sibling r11 watch (no surface yet)
* (carried) OSO STAB / AltiVec / bcheck

Each is its own multi-session arc; pick one with the user.

### 2. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

The pre-069 imacg3 tree backup. Inherited from session 069. Safe
to drop after a few more confirmed rebuild cycles on imacg3.

### 3. (optional) ibookg37 `--builtins+bitfields` generator-host validation

This session's run was default-opts only. A parallel `--builtins
--bitfields` sweep on ibookg37 would round out the generator-host
coverage. Lower value — the builtins sweep mostly exercises the
bswap shim, which session 069 already exercised on imacg3.

## How to pick up

### Verify cleanup-side: smoke-check the 064 link repoint

```sh
grep -rn "\](.*bswap_compat\|\](.*csmith_campaign" docs/ | grep -v convos/
```

Expected: all hrefs into either `scripts/csmith/` (canonical) or
`062-bswap-shim-builtins-2026-05-10/` (only for the retired
`builtin_compat.h` which stayed). One match on
`070/README.md:76` is a backtick-quoted code sample, not a live
link.

### Verify campaign-side: re-read the SUMMARY

```sh
ssh ibookg37 'tail -3 /Users/macuser/tmp/csmith-out-071-default/SUMMARY.txt'
```

Expected: `PASS=176 FAIL=0 SKIP=24`.

### Re-run the campaign (if you want post-fact confirmation)

The fresh corpus is preserved on ibookg37:

```sh
ssh ibookg37 '
  cd /Users/macuser/tmp &&
  bash csmith_campaign.sh 9000 9199 \
    /Users/macuser/tmp/csmith-default-9000 \
    /Users/macuser/tmp/csmith-out-071-default-rerun
'
```

Should be byte-identical seed-by-seed to the original run since
csmith is deterministic per seed and neither gcc-4.0 nor the
post-v0.2.49 tcc are non-deterministic.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

Two-axis session: low-stakes docs polish (the 064 dangling-link
cleanup) + a clean validation result (ibookg37 produces and runs
csmith corpus standalone). The interesting non-obvious findings:

* **csmith on a G3 is slower than expected**: ~13.4s/seed average
  for default-opts generation on ibookg37's 700-MHz G3. 200 seeds
  took 44m. Worth remembering when scoping future generator-host
  campaigns.
* **The campaign's skip-reason distribution is dominated entirely
  by gcc-4.0 -O0 execution timeouts**: both this session (24/24
  skips = 100% gcc-timeout) and session 066's baseline (127/127 =
  100% gcc-timeout) had every single skip attributed to the 15s
  `RUN_TIMEOUT` cap. Neither host nor compiler-side compile
  failures contribute. This is useful to know if we ever want to
  *reduce* the skip rate — bumping the timeout (or pre-screening
  csmith seeds for likely infinite loops) is the lever, not
  improving compile-side coverage.
* The session 070 smoke-check grep (`grep -rn '\](.*bswap_compat
  \|\](.*csmith_campaign' docs/ | grep -v convos/`) **does** catch
  the dangling 064 links the 070 verification missed — the
  difference is just that 070's verification was scoped narrowly
  to 062/069/roadmap and never run as a tree-wide sweep. Worth
  preferring the broader incantation on future `git mv` cleanups.

Three of session 070's open items remain (the carried multi-session
arcs and the imacg3 backup deletion); next session picks one of
those with the user.

Next session: [docs/sessions/071-ibookg37-csmith-resweep-2026-05-13/HANDOFF.md](docs/sessions/071-ibookg37-csmith-resweep-2026-05-13/HANDOFF.md)
