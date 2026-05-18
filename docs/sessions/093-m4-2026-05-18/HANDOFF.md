# Handoff — end of session 093 (2026-05-18)

## TL;DR

**GNU m4 1.4.18 builds + runs correctly with tcc under ordinary
autoconf**, no tcc source change. Twelfth real-world program in the
demos tree (after lua / zlib / bzip2 / cJSON / sqlite / sed / gzip /
Python / awk / expat / bc) and the **first gnulib-heavy autoconf
project**. m4 is itself the macro processor autoconf runs every probe
through, so the cleanly-passing 236/236 manual-examples self-test is
the deepest single correctness signal we've produced for tcc's
codegen on autoconf-driven C to date. Cross-validation against
Tiger's `/usr/bin/m4` 1.4.2 is bit-identical across 26 lines of
macro expansions.

Two distinct findings surfaced (both documented in
[findings.md](findings.md)):

1. **The demos' canonical CC pattern is wrong for gnulib projects.**
   `CC="$TCC -B$TCCROOT -L$TCCROOT -I$TCCROOT/include"` includes a
   redundant `-I$TCCROOT/include` that flat-prepends tcc's own
   sysinclude to the search path. For projects that ship wrapper
   headers under `lib/` (gnulib's `#include_next` pattern), the
   Makefile's `-I.` lands AFTER `-I$TCCROOT/include` on the compile
   line, and tcc's `stddef.h` shadows gnulib's wrapper — causing
   `max_align_t` to be undeclared at parse time, breaking
   `lib/c-stack.c`. Fix: drop the redundant `-I$TCCROOT/include`;
   `-B` already provides sysinclude as a low-priority fallback.

2. **Modern gnulib's `lib/sigsegv.c` has a Tiger-PPC ucontext
   portability bug.** m4-1.4.19 (2021) bundles a gnulib whose
   PowerPC branch accesses `uc_mcontext->__ss.__r1` — modern macOS
   field naming. Tiger uses `ss.r1` (no underscores); gcc-4.0 would
   hit the same error. Not a tcc bug. Session pivoted to m4-1.4.18
   (2016) whose older gnulib snapshot doesn't bundle sigsegv.c.

- **HEAD at session start:** `b928d3c` (end of session 092).
- **HEAD at session end:** post this session's docs + demo commit.
- **Tag:** none — no tcc source change.
- **tests2:** unchanged (111 / 111).
- **abitest:** unchanged.
- **Bootstrap fixpoint:** unchanged.
- **No tcc source change.** Demo + docs only.

## What landed

### Source — none

Zero tcc files touched this session.

### Demos

* [`demos/v0.2.67-m4.sh`](../../../demos/v0.2.67-m4.sh) — new.
  Downloads GNU m4 1.4.18, configures with
  `CC="$TCC -B$TCCROOT -L$TCCROOT"` (no `-I$TCCROOT/include` — see
  findings.md §1), runs `make`, asserts `src/m4` links only
  `libSystem.B.dylib`, runs 6 smoke tests:
    1. Basic define + expand
    2. `eval(2+3*4)`
    3. Recursive factorial — `fact(10) = 3628800`
    4. String builtins — `len`, `substr`, `translit`, `index`
    5. `divert` / `undivert` output reordering
    6. **Cross-validation against /usr/bin/m4 1.4.2** on a 26-line
       corpus (define / eval / ifelse / len / substr / translit /
       index / nested macros / recursion / Fibonacci / divert)
  Then runs `make -C checks check` — m4's own 236-test
  manual-examples suite (transcribed verbatim from the GNU m4
  manual, every documented macro/builtin covered, plus a stack-
  overflow recovery test). All 236/236 pass.

  Also includes a config.h sanity block: 5 things that MUST be
  undef'd (`HAVE_DPRINTF`, `HAVE_ARC4RANDOM_BUF`,
  `HAVE_SECURE_GETENV`, `HAVE_MEMPCPY`, `HAVE_PTHREAD_SPIN_LOCK` —
  none in Tiger libSystem; pre-v0.2.67 would have falsely defined
  them), and 4 things that MUST be defined as controls
  (`HAVE_STRDUP`, `HAVE_GETPAGESIZE`, `HAVE_SNPRINTF`,
  `HAVE_VASPRINTF` — all genuinely present). Same pattern as bc's
  demo (findings.md §3 of session 092).

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — appended a "Companion
  demo (session 093)" mention of v0.2.67-m4.sh to v0.2.67-g3's
  milestone-table row.
* [`demos/README.md`](../../../demos/README.md) — new row for
  v0.2.67-m4.sh inserted above v0.2.67-bc.sh (newest-at-top
  convention).
* [`docs/sessions/093-m4-2026-05-18/README.md`](README.md) — full
  session narrative (arrival → goals → 14-step work log →
  exit state).
* [`docs/sessions/093-m4-2026-05-18/findings.md`](findings.md) —
  6 durable lessons:
  1. tcc's flat `-I` search; no `-isystem` demotion lane.
  2. Modern gnulib's `lib/sigsegv.c` has Tiger-PPC ucontext bug.
  3. m4's bundled 236-test self-test is the deepest correctness
     signal we've used for autoconf-driven C.
  4. Cross-validation against /usr/bin/m4 works the same way
     bc's cross-validation does (session 092 finding #1).
  5. Don't trust a follow-up `make` reporting "up to date" as
     evidence a prior `make` error was a transient.
  6. m4-1.4.19 is reachable with a Tiger-PPC ucontext patch to
     gnulib, but skipping it for 1.4.18 was the right call.
* This `HANDOFF.md`.

### Memory updates

None. The findings are documented in this session's findings.md
and the roadmap; memory would duplicate.

## Status of session 092's open items

| # | Item | Status |
|---|---|---|
| (089 #1, carried 086/087/088) | Dylib writer extrel emission | unchanged |
| (089 #2, carried 067 #3) | Sibling r11 watch | unchanged |
| (089 #3, carried) | **Try another real-world program** | ✅ **m4-1.4.18, CLEAN** (1.4.19 blocked on gnulib's Tiger-PPC sigsegv.c gap) |
| (089 #4, carried) | AltiVec intrinsics | unchanged |
| (089 #5, carried) | bcheck.c port | unchanged |
| (089 #6, optional, carried 069) | Delete `tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (089 #7, optional, carried) | rsync exclude-list polish | unchanged |
| (091 #6, optional) | `-undefined dynamic_lookup` opt-out | unchanged |
| (091 #7, cosmetic) | Drop expat demo's `-Werror` workaround | unchanged |

## Open work for next session

### 1. Add `-isystem` flag support to tcc (NEW, recommended)

This session's findings.md §1 surfaces a small but real
ergonomic gap: tcc doesn't support `-isystem`. With it, the
demos' CC pattern could remain
`CC="$TCC -B$TCCROOT -L$TCCROOT -isystem $TCCROOT/include"` and
gnulib-style wrapper headers would still take precedence. Without
it, gnulib-using demos must omit the explicit include path
entirely and rely on `-B`'s implicit fallback.

Estimated scope: an afternoon. tcc already has a sysinclude
priority list (populated by `-B`); `-isystem foo` is just one
additional argv flag that appends `foo` to that list rather than
to the user `-I` list. Search tcc.c for where `-isystem` would
parse (currently rejected as unknown flag) and where the sysinclude
list is appended; mirror the gcc behavior.

This would also make the existing demos' CC pattern future-proof
for any other gnulib-style project we add to the demos tree.

### 2. Patch gnulib's sigsegv.c for Tiger PPC (LOW priority)

Findings.md §2/§6. Replace `uc_mcontext->__ss.__r1` with
`uc_mcontext->ss.r1` on Tiger PPC, gated on a configure-time
detection of which convention the system uses. Skip unless we
specifically want m4-1.4.19 in the demos.

### 3. Try another real-world program (carried, 089 #3)

m4 is in. Remaining candidates from 092's list:

- **`libpng`** — image library, exercises integer + endian +
  table-driven code. First library demo (vs. executable); would
  test the .a-archive-of-tcc-built-objects path harder.
- **`vim`** — bigger; lots of terminal handling, file I/O,
  custom memory management. Highest-variance bet but slow to
  iterate on G3 build times.
- **`gawk`** — comparison point with one-true-awk (already
  shipped in v0.2.66-awk). gawk uses more glibc-isms that may
  surface different gaps.

Strongest follow-up after m4: **libpng**. It's a library (not
an executable), which would harden the
tcc-built-archive-of-objects path more than any current demo —
all twelve real-world demos produce single executables.

### 4. Dylib writer extrel emission (carried, 089 #1 / 088 #1)

Unchanged. Long-deferred. No in-tree dylib trips it.

### 5. Sibling r11 watch (carried, 067 #3)

Unchanged.

### 6. AltiVec intrinsics / bcheck.c port (carried)

Multi-session lifts, unchanged.

### 7. (optional, low priority) Delete backup dir on imacg3 / rsync polish

Unchanged.

### 8. (low priority) `-undefined dynamic_lookup` opt-out flag (091 #6)

Unchanged. Skip unless concretely required.

### 9. (cosmetic) Update v0.2.66-expat demo to drop the workaround

Carried from 091 #7 / 092. Still cosmetic; the existing demo
serves as a historical snapshot and works correctly.

## How to pick up

### Quick sanity check on imacg3

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.67-m4.sh          # NEW — should PASS (~15 minutes on G3)
./demos/v0.2.67-bc.sh          # prior — should still PASS
./demos/v0.2.67-undef-check.sh # prior — should still PASS
```

### Reproduce the m4 manual-examples self-test

```sh
ssh imacg3
cd /Users/macuser/tmp
mkdir -p m4-self-test && cd m4-self-test
/opt/tigersh-deps-0.1/bin/curl -fsSL \
    --cacert /Users/macuser/tmp/cacert-2026-03-19.pem \
    https://ftp.gnu.org/gnu/m4/m4-1.4.18.tar.gz | tar xz
cd m4-1.4.18
TCC=/Users/macuser/tcc-darwin8-ppc/tcc/tcc
TCCROOT=/Users/macuser/tcc-darwin8-ppc/tcc
PATH=/opt/make-4.3/bin:$PATH \
    CC="$TCC -B$TCCROOT -L$TCCROOT" \
    ./configure
PATH=/opt/make-4.3/bin:$PATH make
PATH=/opt/make-4.3/bin:$PATH make -C checks check
```

Expected last line: `All checks successful`.

### Pick a direction

(a) **`-isystem` support in tcc** — strongest follow-up. Closes the
ergonomic gap this session surfaced and future-proofs the demos'
CC pattern for any other gnulib-using project.

(b) **libpng** — first .a-library demo. Hardens the
tcc-built-archive-of-objects path more than any current demo.

(c) **Dylib writer extrel emission** — carried since 088. Cleanest
long-deferred work item.

(d) **AltiVec intrinsics / bcheck.c port** — multi-session lifts.

(a) and (b) are roughly comparable in value. (a) is a tcc source
change (small but real). (b) is more real-world surface but no
new tcc capability. Neither blocks the other.

## Subagent log

None this session — direct work + verification on imacg3.

## Closing notes

The session's deepest signal isn't the m4 build itself — it's the
**236/236 pass on m4's bundled manual-examples checks**. m4 is the
substrate every autoconf invocation runs on, so every probe in
every autoconf demo we've ever shipped (bc, expat, awk, sed, gzip,
sqlite) ultimately exercised the same macro-expansion engine.
Until now, that engine had only been tested by Apple in 2007 (the
`/usr/bin/m4` 1.4.2 we ship against). With this session, we've
established that **tcc-compiled m4 passes m4's own self-test for
every documented macro/builtin** — bringing the autoconf surface
under regression coverage.

Two takeaways worth carrying forward:

1. **gnulib-using projects need a different CC invocation.** The
   demos' canonical `-I$TCCROOT/include` is redundant with `-B` and
   actively harmful when wrapper headers exist. Drop it, OR add
   `-isystem` to tcc.

2. **Pin the gnulib snapshot.** A program's modernity can come
   from its bundled gnulib, not its own source. Try the older
   release first if a modern release surfaces a portability error
   that gcc-4.0 on Tiger would also hit — that's a signal it's
   gnulib's bundled snapshot, not the program proper.

Next session: [docs/sessions/093-m4-2026-05-18/HANDOFF.md](docs/sessions/093-m4-2026-05-18/HANDOFF.md)
