# Handoff — end of session 090 (2026-05-16)

## TL;DR

**No tcc version bump this session.** libexpat 2.5.0 builds clean
with tcc as CC on Tiger PPC (10th real-world program, full 345-test
internal suite passes). Demo shipped at
[`demos/v0.2.66-expat.sh`](../../../demos/v0.2.66-expat.sh).

Along the way, surfaced a meaningful tcc finding now tracked as
roadmap **#12**: **`ppc-macho.c`'s EXE writer doesn't validate
undefined external symbols against linked dylibs' exports**, so
autoconf's AC_LINK_IFELSE / AC_CHECK_FUNCS false-positive every
function they probe. dyld then fails at runtime. Workaround that
ships in the demo: `CFLAGS=-Werror=implicit-function-declaration`
breaks the conftest at compile time before the link step.

- **HEAD at session start:** `cf4fc45` (end of session 089).
- **No tag.** No tcc source change.
- **tests2:** 111 / 111 (unchanged).
- **Bootstrap fixpoint:** holds (unchanged).
- **Real-world demos verified end-to-end on imacg3:** v0.2.66-awk
  (re-run, PASS — same milestone, sanity check), v0.2.66-expat
  (new, PASS — 345/345 internal tests). Older demos not re-run this
  session; their state is unchanged since session 089.

## What landed

### Source

None. No tcc source change this session.

### Demos

* [`demos/v0.2.66-expat.sh`](../../../demos/v0.2.66-expat.sh) —
  new. Downloads expat-2.5.0 source, configures with `CC=tcc` and the
  `-Werror=implicit-function-declaration` workaround, runs `make`,
  asserts `xmlwf` links only `libSystem.B.dylib`, runs 4 smoke tests
  (well-formed XML, malformed-XML-rejected, numeric character refs,
  K&R amp entity), and runs the embedded 345-test `runtests` suite.

### Docs

* [`docs/roadmap.md`](../../../docs/roadmap.md):
  - Item #10 (Real-world program builds) bumped from "Nine" to "Ten"
    programs and the expat entry added.
  - New item #12 (PPC undefined-external symbol validation) — full
    diagnosis + fix sketch.
* [`demos/README.md`](../../../demos/README.md) — new row for
  v0.2.66-expat.
* [`docs/sessions/090-real-world-expat-2026-05-16/README.md`](README.md) —
  full session narrative including how the AC_CHECK_FUNCS trap was
  found.
* [`docs/sessions/090-real-world-expat-2026-05-16/findings.md`](findings.md) —
  durable lessons: the undef-validation gap (with proper-fix sketch
  for the next session), the C++ runtestspp `__eh_frame` mismatch
  (logged but not in scope), the AC_LINK_IFELSE workaround pattern.
* This `HANDOFF.md`.

### Memory updates

None.

## Status of session 089's open items

| # | Item | Status |
|---|---|---|
| (089 #1, carried 086/087/088) | Dylib writer extrel emission | unchanged |
| (089 #2, carried 067 #3) | Sibling r11 watch | unchanged |
| (089 #3) | Try another real-world program | **PROGRESS → expat shipped, bc/libpng/m4/vim still options** |
| (089 #4, carried) | AltiVec intrinsics | unchanged |
| (089 #5, carried) | bcheck.c port | unchanged |
| (089 #6, optional, carried 069) | Delete `tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (089 #7, optional, carried) | rsync exclude-list polish | unchanged |
| (090 NEW) | **PPC undefined-external symbol validation** (roadmap #12) | OPEN — see #1 below |

## Open work for next session

### 1. Roadmap #12 — port `check_symbols` to `ppc-macho.c` (NEW from 090)

The clean known-target follow-up. See
[`findings.md`](findings.md) section 1 for the full diagnosis. Three
sub-tasks:

(a) **Auto-load libSystem.B.dylib's exports into `s1->dynsymtab_section`
   for PPC EXE output.** Currently `macho_load_dll` is only called
   when the user passes `-lSystem` (or another `-l`); for plain
   `tcc foo.c -o foo` it's not, so `s1->dynsymtab_section` is empty.
   The writer always emits an LC_LOAD_DYLIB for libSystem regardless,
   so we should match by always loading libSystem's exports.
   Probably add a call to `macho_load_dll` for `/usr/lib/libSystem.B.dylib`
   in `tcc_add_macos_sdkpath` (or in `tcc_add_runtime`'s PPC branch).

(b) **Port `check_symbols`-shape logic into `ppc-macho.c`.** Walk
   `symtab_section`, for each `SHN_UNDEF + STB_GLOBAL` sym that isn't
   `STB_WEAK`, call `find_elf_sym(s1->dynsymtab_section, name)`; if
   not found, `tcc_error_noabort("undefined symbol '%s'", name)`.
   Call from `macho_output_exe` before the section/load-command
   layout passes.

(c) **Decide on an opt-out flag.** `-undefined dynamic_lookup` is
   gcc/ld's convention; would set a state bit that suppresses the
   error. Probably nobody actually needs this for PPC Tiger, but
   matching ld's interface costs nothing.

**Verification plan:**
- The 2-line probe (`extern void nonexistent_function_xyzzy(void); int main() { nonexistent_function_xyzzy(); }`)
  should fail to link cleanly with `tcc: error: undefined symbol`.
- All existing tests2 (111/111), abitest, and demos must still pass.
- The `v0.2.66-expat.sh` demo should keep passing as-is (the
  Werror workaround should still work — it operates at compile-time,
  ortho to the new link-time check).
- A fresh expat configure WITHOUT the Werror workaround should
  now correctly detect `HAVE_ARC4RANDOM_BUF=no` and
  `HAVE_ARC4RANDOM=yes` because the AC_LINK_IFELSE link step will
  fail on its own.

**Risk:**
- Any demo / test that currently relies on tcc producing a binary
  with unresolved-at-link-time externs would break. Most likely
  none — but bootstrap, abitest, tests2, all demos must be re-run
  after the change.
- libSystem auto-loading might pull in unexpected symbol-table
  state (e.g. tcc reading more than just exports from the TBD).
  Mitigated by following the existing `macho_load_dll` path, which
  the explicit `-lSystem` flow already exercises cleanly.

### 2. Dylib writer extrel emission (carried, 089 #1 / 088 #1)

Unchanged. Long-deferred. No in-tree dylib trips it.

### 3. Try yet another real-world program (carried, 089 #3)

Candidates that would expand the surface beyond what expat covers:

- **`bc`** (GNU bc, arbitrary-precision calculator) — heavy
  floating-point. Most directly tests v0.2.66's math-builtin fix.
- **`m4`** — autoconf's own macro processor; lots of string
  munging and small-program patterns.
- **`libpng`** — image library, exercises lots of integer + endian
  + table-driven code.
- **`vim`** — bigger; lots of terminal handling, file I/O,
  could surface POSIX-API edge cases.

Each would also be a real-world test of #1's fix (does the
AC_CHECK_FUNCS detection do the right thing without the Werror
workaround?).

### 4. Sibling r11 watch (carried, 067 #3)

Unchanged.

### 5. AltiVec intrinsics / bcheck.c port (carried)

Multi-session lifts, unchanged.

### 6. (optional, low priority) Delete backup dir on imacg3 / rsync polish

Unchanged.

## How to pick up

### Quick sanity check on imacg3

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.66-expat.sh    # NEW — should PASS 345/345
./demos/v0.2.66-awk.sh      # sanity-check the milestone's first demo
```

### Reproduce the AC_CHECK_FUNCS trap

```sh
ssh imacg3
T=/Users/macuser/tcc-darwin8-ppc/tcc
cat > /tmp/undef-probe.c <<'EOF'
extern void definitely_not_a_real_function_qwerty(void);
int main(void) { definitely_not_a_real_function_qwerty(); return 0; }
EOF
$T/tcc -B$T -L$T -I$T/include -o /tmp/undef-probe-tcc /tmp/undef-probe.c
echo "tcc exit: $?"              # exit 0 — WRONG
gcc-4.0 -o /tmp/undef-probe-gcc /tmp/undef-probe.c
echo "gcc exit: $?"              # exit 1 — correct
/tmp/undef-probe-tcc 2>&1 | head -3   # dyld: Symbol not found
```

### Pick a direction

(a) **Roadmap #12 — port `check_symbols` to `ppc-macho.c`** — the
cleanest known-target follow-up directly enabled by session 090's
diagnosis. The fix has a clear scope and a clear verification plan.
Most valuable immediate next step.

(b) **Try another real-world program** (bc / m4 / libpng / vim)
— continues 089's #3 strategy. High-variance bet. Each candidate
either confirms broad correctness or surfaces a new bug class.

(c) **Dylib writer extrel emission** — carried since 088. Cleanest
long-deferred work item. Still no in-tree dylib that trips it.

(a) is the strongest follow-up given the session-090 finding. (b)
is the natural fallback if (a) turns out to have hidden complexity.
Neither blocks the other.

## Subagent log

None this session — direct investigation + edits + verification
on imacg3.

## Closing notes

Three takeaways worth carrying forward (also in
[`findings.md`](findings.md)):

1. **AC_CHECK_FUNCS-on-tcc is silently broken on Tiger PPC.** The
   link succeeds for every probed symbol because `ppc-macho.c`
   doesn't validate undefined externals against linked dylibs.
   Every autoconf-driven build is at risk of false-positive feature
   detection. `CFLAGS=-Werror=implicit-function-declaration` is the
   immediate workaround for autoconf-built programs that use
   AC_LINK_IFELSE-style conftests; the proper fix is roadmap #12.

2. **"appears to work because tcc doesn't check"** is a third
   member of the "appears to work" family alongside session 089's
   two: "appears to work by ABI coincidence" (math-builtin int-vs-
   double return) and "appears to work because no real program
   exercised the wrong path" (the math-builtin trap took 5 months
   to surface for the same reason). Pattern: tcc's permissive
   behavior produces an output that looks fine until a specific
   real-world configure-and-probe pattern depends on the strict
   behavior gcc/ld provide.

3. **Real-world program sweeps remain high-value.** Each program
   in the past 30 sessions has surfaced at least one previously-
   unknown tcc bug (or confirmed a previously-feared one doesn't
   exist). expat continues the streak with a substantially different
   bug class (link-time validation gap) than awk's (compile-time
   ABI prototype gap). The next program to try should ideally
   exercise a fourth dimension of compiler behavior. Candidates:
   floats-heavy (bc), preprocessor-heavy (m4), endian-heavy
   (libpng), system-call-heavy (vim).

Next session: [docs/sessions/090-real-world-expat-2026-05-16/HANDOFF.md](docs/sessions/090-real-world-expat-2026-05-16/HANDOFF.md)
