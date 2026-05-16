# Handoff — end of session 091 (2026-05-16)

## TL;DR

**v0.2.67-g3 shipped.** Roadmap item #12 closed: tcc's PPC EXE
writer now validates undefined external symbols against linked
dylibs' export tables before laying out the exe. Autoconf's
`AC_LINK_IFELSE` / `AC_CHECK_FUNCS` machinery is no longer
fooled into reporting every probed symbol as present. Verified by
re-running libexpat 2.5.0's configure on imacg3 WITHOUT session
090's `-Werror=implicit-function-declaration` workaround:
`HAVE_ARC4RANDOM_BUF` correctly stays undefined, `HAVE_ARC4RANDOM`
correctly defined, all 345/345 internal tests still pass.

- **HEAD at session start:** `01b2b8b` (end of session 090).
- **HEAD at session end:** `9a2e672` (release commit) — to be
  followed by a docs commit on top.
- **Tag:** `v0.2.67-g3`.
- **tests2:** 111 / 111 (unchanged).
- **abitest:** cc + tcc all `success` (unchanged).
- **Bootstrap fixpoint:** holds (unchanged).
- **Demos verified end-to-end on imacg3:** v0.2.18-bzip2,
  v0.2.21-cjson, v0.2.41-gzip, v0.2.40-sed, v0.2.12-lua,
  v0.2.25-dylib, v0.2.26-link-dylib, v0.2.63-extern-data-ref,
  v0.2.64-funcptr-const, v0.2.66-awk, v0.2.66-expat,
  v0.2.67-undef-check — all PASS. (v0.2.23-sqlite-file SKIPPED
  because the amalgamation source isn't present on imacg3.)

## What landed

### Source — `tcc/ppc-macho.c`

Five coupled changes in one file, no header touches. Detailed in
[`README.md`](README.md) section "What landed → Source". Summary:

1. `tcc_add_macos_sdkpath` auto-loads `/usr/lib/libSystem.B.dylib`
   into `s1->dynsymtab_section`; pre-seeds dyld-runtime helpers.
2. `macho_load_dll` refactored with `as_subdylib` flag + recursion
   into umbrella dylib's LC_LOAD_DYLIB sub-libraries (Tiger
   libSystem → libmathCommon for log2/sqrtf/exp2/...).
3. Symbol names in dynsymtab now keep their leading underscore
   (aligning PPC with `tccmacho.c` convention).
4. New `ppc_macho_check_symbols` validates `SHN_UNDEF + STB_GLOBAL`
   non-weak syms in `symtab_section` against
   `s1->dynsymtab_section`.
5. `macho_output_file` calls it on the `TCC_OUTPUT_EXE` path
   before dispatching to `macho_output_exe`.

### Demos

* [`demos/v0.2.67-undef-check.sh`](../../../demos/v0.2.67-undef-check.sh) —
  new. Four sub-tests covering bogus-symbol rejection, real
  libSystem call acceptance, sub-library (libmathCommon `log2`)
  acceptance, and umbrella-LC_LOAD_DYLIB invariance.

### Docs

* [`docs/roadmap.md`](../../roadmap.md): "where we are" bumped to
  v0.2.67; item #10 reworded; item #12 marked done; v0.2.67-g3 row
  added to the milestone table.
* [`demos/README.md`](../../../demos/README.md) — new row for
  `v0.2.67-undef-check.sh`.
* [`docs/sessions/091-ppc-undef-check-2026-05-16/README.md`](README.md) —
  full session narrative including the two implementation
  surprises (PPC's anomalous `macho_load_dll` symtab pollution,
  and Tiger libSystem's LC_SUB_LIBRARY umbrella structure).
* [`docs/sessions/091-ppc-undef-check-2026-05-16/findings.md`](findings.md) —
  durable lessons covering the symtab/dynsymtab convention, the
  umbrella structure, the dyld-only helpers, the EXE-only scope of
  the check, and the now-obsolete v0.2.66 demo workaround.
* This `HANDOFF.md`.

### Memory updates

None. (One worth-considering candidate is "Tiger libSystem is an
umbrella over libmathCommon via LC_LOAD_DYLIB + LC_SUB_LIBRARY,"
but it's documented in findings.md and the ppc-macho.c comments
where it's load-bearing; memory would duplicate.)

## Status of session 090's open items

| # | Item | Status |
|---|---|---|
| (089 #1, carried 086/087/088) | Dylib writer extrel emission | unchanged |
| (089 #2, carried 067 #3) | Sibling r11 watch | unchanged |
| (089 #3, carried) | Try another real-world program | unchanged |
| (089 #4, carried) | AltiVec intrinsics | unchanged |
| (089 #5, carried) | bcheck.c port | unchanged |
| (089 #6, optional, carried 069) | Delete `tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (089 #7, optional, carried) | rsync exclude-list polish | unchanged |
| (090 #1) | **Port `check_symbols` to `ppc-macho.c`** (roadmap #12) | ✅ **CLOSED — v0.2.67-g3** |

## Open work for next session

The list shrinks by exactly one — #12 is done. Remaining items:

### 1. Dylib writer extrel emission (carried, 089 #1 / 088 #1)

Unchanged. Long-deferred. No in-tree dylib trips it.

### 2. Try yet another real-world program (carried, 089 #3)

Now that AC_CHECK_FUNCS works correctly, autoconf-driven programs
should configure cleanly without the `-Werror` shim. Good
candidates that would expand the surface:

- **`bc`** (GNU bc, arbitrary-precision calculator) — heavy
  floating-point; directly tests v0.2.66's math-builtin fix.
- **`m4`** — autoconf's own macro processor; lots of string
  munging.
- **`libpng`** — image library, exercises integer + endian +
  table-driven code.
- **`vim`** — bigger; lots of terminal handling, file I/O.
- **`gawk`** — comparison point with one-true-awk (already
  shipped in v0.2.66-awk). gawk uses more of glibc-isms that may
  surface different gaps than the BSD-leaning one-true-awk.

### 3. Sibling r11 watch (carried, 067 #3)

Unchanged.

### 4. AltiVec intrinsics / bcheck.c port (carried)

Multi-session lifts, unchanged.

### 5. (optional, low priority) Delete backup dir on imacg3 / rsync polish

Unchanged.

### 6. (low priority) `-undefined dynamic_lookup` opt-out flag

Session 090 listed this as part of roadmap #12; deferred this
session because nothing on PPC Tiger currently needs flat-namespace
runtime lookup. If a real consumer asks for it, the implementation
is small: one state bit (set by an option parser entry for
`-undefined dynamic_lookup`), one early-out in
`ppc_macho_check_symbols`. Skip unless concretely required.

### 7. (cosmetic) Update v0.2.66-expat demo to drop the workaround

The `-Werror=implicit-function-declaration` line in
[`demos/v0.2.66-expat.sh`](../../../demos/v0.2.66-expat.sh) is now
obsolete (verified — see findings.md section 6 and README.md
"Verification"). It still works (operates at a different
mechanism — compile-time), so leaving it in is harmless and the
demo serves as a historical snapshot. If a future session does a
demos cleanup pass, removing it would be a one-line edit with the
header comment updated to point to v0.2.67-g3.

## How to pick up

### Quick sanity check on imacg3

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.67-undef-check.sh    # NEW — should PASS all 4 sub-tests
./demos/v0.2.66-expat.sh          # confirm prior milestone still PASS
```

### Reproduce the (now-fixed) AC_CHECK_FUNCS trap

```sh
ssh imacg3
T=/Users/macuser/tcc-darwin8-ppc/tcc
cat > /tmp/undef-probe.c <<'EOF'
extern void definitely_not_a_real_function_qwerty(void);
int main(void) { definitely_not_a_real_function_qwerty(); return 0; }
EOF
$T/tcc -B$T -L$T -I$T/include -o /tmp/undef-probe-tcc /tmp/undef-probe.c
echo "tcc exit: $?"               # exit 1, with "undefined symbol" error
                                   # (was exit 0 pre-v0.2.67-g3)
```

### Pick a direction

(a) **Try another real-world program** (bc / m4 / libpng / vim /
gawk) — most natural follow-up; with AC_CHECK_FUNCS honest, more
autoconf-driven programs should work cleanly. The next program is
the highest-variance bet — most likely to surface a previously-
unknown bug class or confirm broad correctness.

(b) **Dylib writer extrel emission** — carried since 088. Cleanest
long-deferred work item. Still no in-tree dylib that trips it.

(c) **AltiVec intrinsics / bcheck.c port** — multi-session lifts.

(a) is the natural strongest follow-up given v0.2.67's autoconf
unlock. (b) is the natural fallback. Neither blocks the other.

## Subagent log

None this session — direct investigation + edits + verification
on imacg3.

## Closing notes

Three takeaways worth carrying forward (also in
[`findings.md`](findings.md)):

1. **PPC's `macho_load_dll` was anomalous vs `tccmacho.c`.**
   Pre-091, it registered dylib symbols in `s1->symtab` rather
   than `s1->dynsymtab_section`, with a misleading code comment
   claiming "tcc's symtab uses bare C names internally." That was
   simply wrong (tcc's `leading_underscore=1` prepends `_` to
   every symtab entry for Mach-O targets). The mismatch was
   harmless because nothing looked the entries up — until
   `check_symbols` did. The fix was to align PPC with
   `tccmacho.c`'s convention; doing so also stops bloating the
   eventual nlist with unreferenced libSystem entries.

2. **Tiger libSystem is an umbrella.** Its nlist has UNDEF
   references to math symbols (log2, sqrtf, exp2, ...) that live
   in libmathCommon.A.dylib (LC_LOAD_DYLIB + LC_SUB_LIBRARY at
   runtime). A naive auto-load of libSystem alone misses those
   names. Sub-library recursion with `as_subdylib=1` (suppress
   `tcc_add_dllref` so the exe's LC_LOAD_DYLIB list stays
   libSystem-only) is the right pattern.

3. **`-Werror=implicit-function-declaration` is no longer the
   right autoconf-with-tcc workaround.** With link-time validation
   in place, AC_CHECK_FUNCS gives correct answers without it.
   New autoconf-driven demos can configure with default CFLAGS.

Next session: [docs/sessions/091-ppc-undef-check-2026-05-16/HANDOFF.md](docs/sessions/091-ppc-undef-check-2026-05-16/HANDOFF.md)
