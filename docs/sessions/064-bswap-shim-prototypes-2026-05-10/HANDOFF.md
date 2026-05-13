# Handoff — end of session 064 (2026-05-10)

> **Note (session 071):** the `bswap_compat.c` and `csmith_campaign.sh`
> referenced below moved to [`scripts/csmith/`](../../../scripts/csmith/)
> in session 070; markdown link targets in this file have been
> redirected to the new location. The narrative is unchanged from
> session-064's exit state.

## TL;DR

The remaining csmith bug from session 062/063 (**seed-8255**) was
**not a tcc codegen bug** — it was a UB issue in the test harness's
[`bswap_compat.c`](../../../scripts/csmith/bswap_compat.c)
shim, exposed because [`builtin_compat.h`](../062-bswap-shim-builtins-2026-05-10/builtin_compat.h)
was missing prototypes for the bswap/crc32 functions. K&R implicit
declaration was mis-typing the call as `int(int)`, which is
ABI-incompatible with the actual `unsigned long long(unsigned long
long)` signature on PPC32 (64-bit args use the `r3:r4` pair, 32-bit
uses `r3` only). gcc and tcc both compiled to the same wrong ABI;
they only diverged when surrounding codegen left different junk in
`r4` at the call site.

**Fix**: add `extern` prototypes for the three shim functions to
`builtin_compat.h`. No tcc source changes. All 7 prior builtins
seeds (including 8255) now produce identical gcc/tcc checksums.

* HEAD at session start: `fc263ba` (session 063 ppc-gen fix).
* HEAD at session end: this docs commit + the shim-prototype edit.
* Bug count from sessions 062: **5 surfaced, 5 closed.**
* No tcc rebuild needed; binary on both hosts is still the post-063
  build.

## Status of the 5 bugs (final)

| Seed | Source | Status | How closed |
|---|---|---|---|
| 1536 | default-opts | ✅ FIXED | session 063 (ppc-gen save_reg) |
| 8271 | builtins+bitfields | ✅ FIXED | session 063 (ppc-gen save_reg) |
| 8389 | builtins+bitfields | ✅ FIXED | session 063 (ppc-gen save_reg) |
| 1705 | default-opts | ✅ FIXED | session 063 (ppc-gen save_reg) |
| 8255 | builtins+bitfields | ✅ FIXED | session 064 (shim prototypes) |

## What landed

Two changes:

* `docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h`
  — added 3 `extern` prototypes (bswap32/bswap64/ia32_crc32qi) plus
  a comment paragraph explaining the K&R implicit-decl ABI gotcha.
* `docs/sessions/064-bswap-shim-prototypes-2026-05-10/` —
  this session directory with README and HANDOFF.

Off-tree, on each host:
* `/Users/macuser/tmp/builtin_compat.h` updated on imacg3 and ibookg38.
* `/Users/macuser/tmp/csmith_campaign.sh` updated on both (the
  `EXTRA_CC_HDR`-aware version, deferred from session 063 as
  open-work item #2).

## Open work for next session

### 1. Decide: tag v0.2.48-g3?

All 5 of session 062's surfaced bugs are now closed. The codegen
fix landed in session 063 (`fc263ba`); this session's fix is purely
test-harness. v0.2.48-g3 would mark the "all 5 csmith bugs closed"
state. Worth tagging. Optionally bundle a fresh demo (e.g.
`demos/v0.2.48-csmith-clean-builtins.sh`) showing the
`builtins+bitfields` campaign passing.

### 2. (Carried from 063 #4) Run a fresh full csmith campaign

Now that both the gfunc_call codegen bug AND the shim-UB bug are
fixed, a clean 1000-seed `default-opts` AND 1000-seed `--builtins`
campaign would build confidence in the cumulative fix.

This session ran a 100-seed `--builtins` validation campaign (seeds
8020-8119) on ibookg38 — see appendix below for results.

### 3. (Carried from 063 #3) Patch tcc's libtcc1.a clz/ctz to match gcc-PPC

Same rationale as before, now slightly less urgent because the shim
already covers it. Still worth doing — would let `--builtins` seeds
pass without `-include` and is the right long-term direction. Test
plan unchanged.

### 4. (Carried from 063 #5) From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

The OSO STAB item rises in priority after this session — manually
trapping the call-site register state for the bswap diagnosis took
hours; gdb on PPC would have made it minutes.

## How to pick up

### Sanity baseline

No tcc rebuild was performed in this session, so the post-063 binary
is still in place on both hosts. Standard sanity:

```sh
ssh imacg3   # or ssh ibookg38 with PATH=/usr/bin: prefix on builds
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
```

### Re-run the 7 builtins seeds individually (5-second smoke)

```sh
ssh ibookg38 '
cd /tmp && for s in 8034 8068 8084 8228 8255 8312 8356; do
  src=/Users/macuser/tmp/csmith-builtins-8020/seed-${s}.c
  [ -f "$src" ] || continue
  printf "seed-%s: " "$s"
  gcc -O0 -w -I/Users/macuser/tmp/csmith-runtime \
    -include /Users/macuser/tmp/builtin_compat.h \
    /Users/macuser/tmp/bswap_compat.c "$src" -o /tmp/g 2>/dev/null
  /Users/macuser/tcc-darwin8-ppc/tcc/tcc \
    -B/Users/macuser/tcc-darwin8-ppc/tcc \
    -I/Users/macuser/tcc-darwin8-ppc/tcc \
    -I/Users/macuser/tmp/csmith-runtime \
    -include /Users/macuser/tmp/builtin_compat.h \
    /Users/macuser/tmp/bswap_compat.c "$src" -o /tmp/t 2>/dev/null
  g=$(/tmp/g); t=$(/tmp/t)
  [ "$g" = "$t" ] && echo "PASS ($g)" || echo "FAIL g=$g t=$t"
done'
```

### Run the full validation campaign

```sh
ssh ibookg38 '
EXTRA_CC_SRC=/Users/macuser/tmp/bswap_compat.c \
EXTRA_CC_HDR=/Users/macuser/tmp/builtin_compat.h \
/opt/tigersh-deps-0.1/bin/bash /Users/macuser/tmp/csmith_campaign.sh \
  8020 8419 \
  /Users/macuser/tmp/csmith-builtins-8020 \
  /Users/macuser/tmp/csmith-out-064-full'
```

(All 400 builtins seeds. ~1-2 hours on ibookg38.)

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. One in-session author (Opus 4.7).

## Closing notes

A satisfying counterpoint to session 063's "single 4-line patch
closes 4 bugs" — this session's "single 3-line non-tcc patch closes
the last bug" continues the theme of high-leverage minimal changes.

The deeper lesson: when a differential-test campaign uses a hand-
written shim with extern symbols, **always declare prototypes for
those symbols** in the harness header, even if the compilers
"appear to work". The K&R implicit-decl path is so cheap to fall
into and so sneaky in its UB consequences that any 64-bit-using shim
function without a prototype is a latent bug detector — except it
detects nothing 99% of the time, then surfaces a "real bug" when
the UB happens to bite.

## Appendix — 100-seed validation campaign

A `--builtins`+`--bitfields` campaign on seeds 8020-8119 ran on
ibookg38. Results:

| Pass | Fail | Skip | Notes |
|---|---|---|---|
| 82 | 0 | 18 | All 18 SKIPs are gcc-timeouts (programs >15s); zero tcc-side failures. |

Output kept at `/Users/macuser/tmp/csmith-out-064/SUMMARY.txt` on
ibookg38 if needed for cross-reference.

Next session: [docs/sessions/064-bswap-shim-prototypes-2026-05-10/HANDOFF.md](docs/sessions/064-bswap-shim-prototypes-2026-05-10/HANDOFF.md)
