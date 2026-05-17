# Handoff — end of session 092 (2026-05-16)

## TL;DR

**GNU bc 1.07.1 builds + runs correctly with tcc under ordinary
autoconf, no CFLAGS shim.** Eleventh real-world program in the
demos tree (after lua / zlib / bzip2 / cJSON / sqlite / sed /
gzip / Python / awk / expat) and the first one in the post-v0.2.66
series to build cleanly with no tcc source change. Validates
session 091's prediction that v0.2.67-g3's AC_CHECK_FUNCS fix
would unlock autoconf-driven programs in the wild. Cross-validated
against Tiger's stock `/usr/bin/bc` 1.06: bit-identical output
across a 30-line arbitrary-precision battery (sqrt / atan / exp /
log / sin / cos at scale 50, 2^200, 40!, gcd via Euclid recursion).

- **HEAD at session start:** `fd52d81` (end of session 091).
- **HEAD at session end:** post this session's docs commit.
- **Tag:** none — no tcc source change this session.
- **tests2:** 111 / 111 (unchanged — no source change to risk it).
- **abitest:** unchanged.
- **Bootstrap fixpoint:** unchanged.
- **No tcc source change.** Demo + docs only.

## What landed

### Source — none

Zero tcc files touched this session.

### Demos

* [`demos/v0.2.67-bc.sh`](../../../demos/v0.2.67-bc.sh) — new.
  Downloads GNU bc 1.07.1, configures with `CC=tcc` and default
  CFLAGS (no `-Werror=implicit-function-declaration`), `make`,
  asserts bc + dc each link only `libSystem.B.dylib`, runs 10
  smoke tests:
    1. Basic arithmetic (precedence)
    2. 2^100 big-integer exponentiation
    3. 50! recursion + big integer
    4. sqrt(2) at scale 60 (Newton via libmath)
    5. π via 4·a(1) at scale 60 (Machin-style atan)
    6. e at scale 60 (exp Taylor)
    7. e(l(7)) round-trip within 10⁻³⁰ of 7 (log/exp inverse)
    8. sin²+cos² within 10⁻³⁰ of 1 (Pythagorean identity)
    9. **Bit-identical cross-validation against /usr/bin/bc 1.06**
       on a 17-input / 30-line arbitrary-precision battery
       (sqrt/atan/exp/log/sin/cos at scale 50, 2^200, 40!, gcd
       via Euclid recursion, loop over sqrt(1..10) at scale 30)
   10. dc smoke tests (`2 3 + p`, `12 7 % p`)
  Also includes a config.h sanity block asserting
  `HAVE__DOPRNT` undef'd, `HAVE_LIB_H` undef'd, `HAVE_VPRINTF`
  defined — this is the "demo can't silently regress to a
  CFLAGS shim" guardrail (see findings.md §3).

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — appended a "Companion
  demo" mention of v0.2.67-bc.sh to v0.2.67-g3's milestone-table
  row.
* [`demos/README.md`](../../../demos/README.md) — new row for
  v0.2.67-bc.sh inserted above v0.2.67-undef-check (newest-at-top
  convention).
* [`docs/sessions/092-bc-2026-05-16/README.md`](README.md) — full
  session narrative (arrival state → goals → 7-step work log →
  exit state → findings index).
* [`docs/sessions/092-bc-2026-05-16/findings.md`](findings.md) —
  durable lessons:
  1. Cross-validation against Tiger's stock-bc-1.06 isolates tcc
     as the only variable; bit-identical 30-line output is the
     deepest correctness statement to date.
  2. `_doprnt` is a load-bearing autoconf-honesty canary on
     Tiger (genuinely missing from libSystem; pre-v0.2.67 tcc
     would have accepted it).
  3. The "config.h sanity" pattern for autoconf demos (positive +
     negative assertions, so the demo can't silently regress to
     a CFLAGS shim).
  4. Tolerance checking for FP transcendentals in bc (use
     in-bc `|r - expected| < 10⁻³⁰` assertions, not exact
     digit matches).
  5. bc's Test/ dir is timing inputs, not a self-validating
     suite — cross-validation is the right substitute.
  6. v0.2.66-expat's `-Werror=implicit-function-declaration`
     workaround is now demonstrably superfluous.
  7. bc-1.07.1 is the first post-v0.2.66 real-world program
     that needed no tcc fix — the recent fixes have stabilized
     the autoconf-driven-C surface.
* This `HANDOFF.md`.

### Memory updates

None. The findings are documented in this session's findings.md
and the project's roadmap; memory would duplicate.

## Status of session 091's open items

| # | Item | Status |
|---|---|---|
| (089 #1, carried 086/087/088) | Dylib writer extrel emission | unchanged |
| (089 #2, carried 067 #3) | Sibling r11 watch | unchanged |
| (089 #3, carried) | **Try another real-world program** | ✅ **bc-1.07.1, CLEAN** |
| (089 #4, carried) | AltiVec intrinsics | unchanged |
| (089 #5, carried) | bcheck.c port | unchanged |
| (089 #6, optional, carried 069) | Delete `tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (089 #7, optional, carried) | rsync exclude-list polish | unchanged |
| (091 #6, optional) | `-undefined dynamic_lookup` opt-out | unchanged |
| (091 #7, cosmetic) | Drop expat demo's `-Werror` workaround | unchanged (091 #7 still open; this session's findings §6 confirm the workaround is superfluous, but didn't touch the demo) |

## Open work for next session

### 1. Dylib writer extrel emission (carried, 089 #1 / 088 #1)

Unchanged. Long-deferred. No in-tree dylib trips it.

### 2. Try yet another real-world program (carried, 089 #3 — now partially burnt-down)

bc is in. Remaining candidates from 091's list:

- **`m4`** — autoconf's own macro processor; lots of string
  munging, macro expansion stack. Different shape from bc
  (no FP, all string/symbol management).
- **`libpng`** — image library, exercises integer + endian +
  table-driven code. First library demo (vs. executable);
  would test the .a-archive-of-tcc-built-objects path harder.
- **`vim`** — bigger; lots of terminal handling, file I/O,
  custom memory management. Highest-variance bet but slowest
  to iterate on G3 build times.
- **`gawk`** — comparison point with one-true-awk (already
  shipped in v0.2.66-awk). gawk uses more of glibc-isms that
  may surface different gaps than the BSD-leaning
  one-true-awk.

Strongest follow-up: **m4**. It's small (fast iteration), pure
ASCII processing (different from bc's FP-heavy workout, gives
us a different bug class if there is one), and is the macro
processor autoconf itself uses — pleasingly recursive validation.

### 3. Sibling r11 watch (carried, 067 #3)

Unchanged.

### 4. AltiVec intrinsics / bcheck.c port (carried)

Multi-session lifts, unchanged.

### 5. (optional, low priority) Delete backup dir on imacg3 / rsync polish

Unchanged.

### 6. (low priority) `-undefined dynamic_lookup` opt-out flag (091 #6)

Unchanged. Skip unless concretely required.

### 7. (cosmetic) Update v0.2.66-expat demo to drop the workaround

Carried from 091 #7. This session's findings.md §6 explicitly
confirms the workaround is demonstrably superfluous (bc demo
runs without it on a fresh autoconf project and gets honest
answers from configure). Still cosmetic; the existing demo
serves as a historical snapshot and works correctly. If a
future session does a demos cleanup pass, this is a one-line
edit + header comment refresh pointing at v0.2.67-g3.

## How to pick up

### Quick sanity check on imacg3

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.67-bc.sh          # NEW — should PASS all 10 sub-tests
./demos/v0.2.67-undef-check.sh # prior — should still PASS
```

### Reproduce the cross-validation

```sh
ssh imacg3
TCCBC=/Users/macuser/tmp/v092-bc/bc-1.07.1/bc/bc
SYSBC=/usr/bin/bc

cat > /tmp/xv.bc <<'EOF'
2^200
scale=50; sqrt(2)
scale=50; 4*a(1)
scale=50; e(1)
scale=50; l(2)
scale=50; s(1)
define f(n) { if(n<=1) return 1; return n*f(n-1); }
f(40)
quit
EOF

$TCCBC -l /tmp/xv.bc > /tmp/tcc.out
$SYSBC -l /tmp/xv.bc > /tmp/sys.out
diff /tmp/tcc.out /tmp/sys.out && echo "IDENTICAL"
```

### Pick a direction

(a) **m4 (recommended)** — strongest follow-up for "try another
real-world program." Small, fast to iterate, different shape from
bc (string/macro vs. FP). Pleasingly recursive: m4 is what autoconf
itself runs on. Likely to surface a new bug class if one exists, or
add a second clean autoconf-driven build that strengthens the
"recent fixes have stabilized this surface" claim.

(b) **libpng** — first .a-library demo. Would harden the
tcc-built-archive-of-objects path more than any current demo
(which all produce single executables).

(c) **Dylib writer extrel emission** — carried since 088.
Cleanest long-deferred work item. Still no in-tree dylib that
trips it; would be "fix it because the gap exists" rather than
user-driven.

(d) **AltiVec intrinsics / bcheck.c port** — multi-session lifts.

(a) is the natural strongest follow-up. (b) is the natural
fallback if you want stronger archive-path coverage. Neither
blocks the other.

## Subagent log

None this session — direct work + verification on imacg3.

## Closing notes

Two takeaways worth carrying forward (also in
[`findings.md`](findings.md)):

1. **Cross-validation against Tiger's stock binaries is the
   strongest correctness signal we can produce.** Lean toward
   real-world programs Tiger ships its own copy of (bc, awk,
   sed, gzip, ...). Same source compiled by Apple-gcc vs. tcc
   is a near-perfect controlled experiment.

2. **The v0.2.66 + v0.2.67 fixes have stabilized the
   autoconf-driven-C surface on Tiger PPC.** bc-1.07.1 just
   works — first post-v0.2.66 real-world program with that
   property. The next program is still high-variance, but the
   "everyone trips the same bug" mode appears to be over.

Next session: [docs/sessions/092-bc-2026-05-16/HANDOFF.md](docs/sessions/092-bc-2026-05-16/HANDOFF.md)
