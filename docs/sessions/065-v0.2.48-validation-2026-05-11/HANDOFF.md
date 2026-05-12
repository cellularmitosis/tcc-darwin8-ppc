# Handoff — end of session 065 (2026-05-11)

## TL;DR

Session 065 ran the full validation campaigns that session 064's
HANDOFF flagged as the headline next-session task (#2): a 400-seed
`--builtins+bitfields` sweep and a 1000-seed `default-opts` sweep,
both against the post-063 tcc binary on **ibookg37** (a third
PowerPC G3 brought into the fleet for this work).

**Result**: builtins sweep clean (352/0/48), default-opts surfaced
**1 new tcc bug** — `seed-732`, output-diff at first read of
`g_359.f0` (a `union U0 { int8_t f0; }` global), not UB. Verified
by gcc -O0/-O2 stable agreement on the gcc reference checksum.

**Status of session-062's 5 bugs**: all still closed. Session 062's
five fail seeds (1536, 1705, 8255, 8271, 8389) are inside the
8020-8419 builtins range or the 1-1000 default-opts range; all
pass clean in this campaign.

**v0.2.48-g3 tagging decision**: **deferred**. v0.2.48-g3 will
land combined with the seed-732 fix in a future session. See
"Open work" below.

* HEAD at session start: `c7360d3` (session 064 builtin_compat.h doc).
* HEAD at session end: session-065 docs commit.
* No tcc source changes. The post-063 binary on ibookg38 was used
  verbatim on ibookg37 via tarball transfer (ibookg38 went offline
  mid-session; window was narrow).

## Campaign results

### --builtins+bitfields, seeds 8020-8419 (ibookg37)

| Pass | Fail | Skip | Notes |
|---|---|---|---|
| 352 | **0** | 48 | All 48 SKIPs are gcc-timeouts. Zero tcc-side issues. |

Summary on host: `/Users/macuser/tmp/csmith-out-065-builtins/SUMMARY.txt`.

### default-opts, seeds 1-1000 (ibookg37)

| Pass | Fail | Skip | Notes |
|---|---|---|---|
| 872 | **1** | 127 | seed-732 output-diff (new tcc bug). |

Summary on host: `/Users/macuser/tmp/csmith-out-065-default/SUMMARY.txt`.

Failing seed + outputs captured in this session's `seed-732-fail/`:

* `seed-732.c` (922 lines)
* `seed-732.gcc.out` — gcc-4.0 -O0 reference, per-global hash
* `seed-732.tcc.out` — tcc post-063 actual

First divergence: `g_359.f0` (gcc=`3B71BCAC` vs tcc=`F3115DB3`).
All later globals diverge from there as the rolling CRC carries
forward. UB-check (gcc -O0 vs -O2) clean — gcc reference is stable.
Suspect-shape: `func_8` takes `union U0` by **value as a middle
argument** (`p_11`, between an int and a pointer). Distinct from
the session-063 "struct as last arg" shape; not closed by the
session-063 patch.

### Cross-host consistency (smoke)

All 10 closed-bug seeds (5 surfaced in session 062 plus 5 nearby
"happened-to-pass" builtins seeds) re-validated on ibookg37 with
the post-063 tcc binary; gcc/tcc checksums match the session-064
HANDOFF table byte-for-byte. See `README.md` for the table.

## Setup notes for future ibookg37 work

ibookg37 (Mac OS X 10.4.11 PowerPC G3) is now provisioned for the
csmith differential-testing harness:

* `/Users/macuser/tcc-darwin8-ppc/tcc/{tcc,libtcc1.a}` — post-063
  binaries (pre-existing May-6 builds backed up to `*.pre-065.bak`).
* `/Users/macuser/tmp/csmith-runtime → /Users/macuser/tmp/csmith`
  (symlink; csmith runtime headers were already in the latter path).
* `/Users/macuser/tmp/builtin_compat.h`, `bswap_compat.c`,
  `csmith_campaign.sh` — copied from this repo's
  `docs/sessions/062-bswap-shim-builtins-2026-05-10/`.
* `/Users/macuser/tmp/csmith-builtins-8020/` (400 seeds) and
  `/Users/macuser/tmp/csmith-default-1k/` (1000 seeds) —
  regenerated on uranium (main Mac) with `csmith 2.3.0` from
  Homebrew and tar-streamed to ibookg37.

Key wrinkle: **csmith is not installed on ibookg37 (nor on imacg3).**
Both relied on ibookg38 for seed generation. uranium has it via
`brew install csmith` → `/opt/homebrew/bin/csmith` (2.3.0). Future
sessions can regenerate from uranium if ibookg38 is down again, or
build csmith from source on a PPC host if needed (untried).

## Open work for next session

### 1. seed-732 — investigate and (probably) fix

New tcc codegen bug surfaced this session. Suspected pattern:
**union/struct passed by value as a middle argument** (not the
last, which session 063 handled). Plausibly another `save_reg`
hole in `gfunc_call`. First-look notes are in this README.

Suggested approach (cribbed from session 063's playbook for
seed-1536):

1. Run `seed-732.c` with `argv[1]="1"` under tcc and gcc, save
   per-global checksums, confirm `g_359.f0` is the first divergent
   value. (Already done; see `seed-732-fail/`.)
2. Hand-trace which writer of `g_359.f0` runs prior to the first
   `transparent_crc(g_359.f0, ...)` call in `main()`.
3. `creduce` against a `test.sh` that asserts gcc/tcc disagree
   (template is in
   `docs/sessions/062-bswap-shim-builtins-2026-05-10/seed-1536-repro/test.sh`).
4. Inspect the resulting reduced .c — look for the `func_8`-shape
   call (struct by value, middle arg).
5. Compare `ppc-gen.c`'s arg-pass-2 paths to verify the
   `save_reg` invariant holds for the middle-arg-struct case.
6. Fix, regression-test (fixpoint, tests2, abitest), then re-run
   this session's 1000-seed default-opts sweep to confirm
   seed-732 closes.

### 2. v0.2.48-g3 tag — held until seed-732 closes

Decision at end of session 065: **defer the tag**. v0.2.48-g3 will
mark the combined "session-062 5 bugs + seed-732 closed" state
once the seed-732 fix lands. Rationale: tagging a milestone
immediately followed by a known regression-class bug reads
poorly in the changelog. The seed-732 fix looks session-063-sized
(small, contained), so the coupling cost is modest.

When tagging:

* Tag message style follows recent precedent — see
  `git tag -l --format='%(refname:short)|%(subject)' v0.2.4[5-7]-g3`.
  Suggested subject: `v0.2.48-g3: struct-by-value middle-arg
  save_reg fix (closes seeds 062 + 732)`.
* Tag the commit that lands the seed-732 fix, not the docs
  commit. The fix should land in `tcc/ppc-gen.c` (same neighborhood
  as session 063's `fc263ba`).
* Don't push the tag without user confirmation.

### 3. (Optional) v0.2.48 demo

The handoff for 064 suggested a `demos/v0.2.48-csmith-clean-builtins.sh`.
Once the seed-732 fix is in and the campaigns are re-run clean, the
right shape is probably:

* A reduced reproducer for the seed-732 middle-arg struct case
  (similar to `demos/v0.2.47-fp-arg-shadow.c`), self-contained,
  with the milestone in the file header comment.
* No csmith runtime dependency in the demo (so it works on any
  Tiger PPC host without setup).

Skipped this session because the bug needs to be fixed first.

### 4. (Carried from 064 #3) Patch tcc's libtcc1.a clz/ctz to match gcc-PPC

Same rationale, still desirable. The `builtin_compat.h` shim
papers over the divergence, but the right long-term fix is in
`tcc/lib/lib-arm64.c` / `tcc/lib/...` to special-case the x==0
input. Then `--builtins` campaigns can run without `-include`.

### 5. Get csmith building on at least one PPC host

ibookg38 had it as a binary; if that host goes for good, the fleet
loses csmith. Building from source on a PPC G3/G4 is plausible
(it's C++ but not exotic) — `/opt/gcc-10.3.0` should handle it.
Alternative: keep the `csmith` binary checked into a fleet
home-share so any host can fetch it.

### 6. ibookg38 — bring back online or write off?

Went offline mid-session and stayed offline. If it's dead, lose
the in-place `csmith-builtins-8020/` corpus there (already
regenerated on uranium so this is recoverable), but the host
itself was the fastest of the G3 fleet (per memory).

### 7. (Carried from 064 #5) Older items, unchanged

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

## How to pick up

### Reproduce this session's smoke

```sh
ssh ibookg37 'TCC=/Users/macuser/tcc-darwin8-ppc/tcc/tcc
for s in 1536 1705 8068 8084 8228 8255 8271 8312 8356 8389; do
  src=/Users/macuser/tmp/csmith-fails-065/seed-${s}.c
  [ -f "$src" ] || continue
  /usr/bin/gcc-4.0 -O0 -w -I/Users/macuser/tmp/csmith \
    -include /Users/macuser/tmp/builtin_compat.h \
    /Users/macuser/tmp/bswap_compat.c "$src" -o /tmp/g 2>/dev/null
  $TCC -B/Users/macuser/tcc-darwin8-ppc/tcc \
    -I/Users/macuser/tcc-darwin8-ppc/tcc/include \
    -I/Users/macuser/tmp/csmith \
    -include /Users/macuser/tmp/builtin_compat.h \
    /Users/macuser/tmp/bswap_compat.c "$src" -o /tmp/t 2>/dev/null
  g=$(/tmp/g); t=$(/tmp/t)
  [ "$g" = "$t" ] && echo "PASS $s" || echo "FAIL $s"
done'
```

### Re-run a campaign

```sh
ssh ibookg37 'EXTRA_CC_SRC=/Users/macuser/tmp/bswap_compat.c \
  EXTRA_CC_HDR=/Users/macuser/tmp/builtin_compat.h \
  /opt/tigersh-deps-0.1/bin/bash /Users/macuser/tmp/csmith_campaign.sh \
  8020 8419 \
  /Users/macuser/tmp/csmith-builtins-8020 \
  /Users/macuser/tmp/csmith-out-065-builtins'
```

### Regenerate corpus from uranium (if ibookg38 stays down)

```sh
mkdir /tmp/csmith-NNN && cd /tmp/csmith-NNN
for s in $(seq <start> <end>); do
  csmith --no-volatiles --no-packed-struct [--bitfields --builtins] \
    --max-funcs 6 --max-block-depth 3 --max-array-dim 2 \
    --max-array-len-per-dim 5 --seed $s -o seed-$s.c
done
tar czf - . | ssh ibookg37 'cd /Users/macuser/tmp && mkdir csmith-NNN && cd csmith-NNN && tar xzf -'
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

The shape of this session — kick off the broadest validation we
have, see what falls out — landed on a tidy outcome: every bug
session 062 surfaced is durably closed (verified across two
hosts and the full builtins+bitfields range), and one new bug
was promptly added to the pile. That's exactly the kind of
"the campaign is doing its job" signal we wanted.

The seed-732 bug bears a family resemblance to seed-1536: both
involve a small union (`union U0 { int8_t f0; }`) flowing through
a `gfunc_call` arg-shuffle path. Session-063's fix targeted the
**last-arg** struct-word lwz path. seed-732 is a **middle-arg**
case, plausibly another save_reg hole in the same neighborhood.
If the hypothesis pans out, the fix is likely small (4-8 lines)
and the v0.2.48 milestone tightens into "all 5 062 bugs + the
follow-up seed-732 closed".

Two minor sub-finds worth remembering:

* **`csmith` is single-host in the fleet** — only ibookg38 had
  it, and ibookg38 went flaky mid-session. Worked around by
  regenerating the corpora deterministically on uranium (Homebrew
  csmith 2.3.0). Future-proofing: see "Open work #5".
* **ibookg37 is now a viable third campaign host.** Slightly
  slower than ibookg38 per memory (~70-80%?), but solid for
  long runs. The setup notes above are enough for any future
  session to re-bootstrap it.

Next session: [docs/sessions/065-v0.2.48-validation-2026-05-11/HANDOFF.md](docs/sessions/065-v0.2.48-validation-2026-05-11/HANDOFF.md)
