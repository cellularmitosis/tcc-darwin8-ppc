# Session 091 — port `check_symbols` to `ppc-macho.c` (roadmap #12)

## Arrival state

- HEAD: `01b2b8b` (end of session 090).
- Last tag: `v0.2.66-g3`.
- tests2: 111 / 111 (last verified end of 089).
- Bootstrap fixpoint: holds.
- Roadmap item #12 (PPC undefined-external symbol validation): open.
  Full diagnosis in
  [`docs/sessions/090-real-world-expat-2026-05-16/findings.md`](../090-real-world-expat-2026-05-16/findings.md)
  and a fix sketch in session 090's HANDOFF.md.

## Goal

Close roadmap #12. The PPC EXE writer was emitting every
`SHN_UNDEF + STB_GLOBAL` symbol into the Mach-O nlist regardless of
whether anything actually provided it at runtime — so any binary
calling a nonexistent function linked cleanly, and dyld SIGABRT'd at
startup. The blast radius was autoconf's `AC_LINK_IFELSE` /
`AC_CHECK_FUNCS` machinery: every probe of a candidate symbol
reported success, so config.h ended up with HAVE_X for every X,
including symbols not present in Tiger libSystem. Real-world
programs built with that false-positive config.h then failed at
startup.

## Plan (session 090's HANDOFF #1)

(a) Auto-load `/usr/lib/libSystem.B.dylib`'s exports into
`s1->dynsymtab_section` for PPC EXE output (currently only loaded
when `-lSystem` is explicit).

(b) Walk `s1->symtab` for `SHN_UNDEF + STB_GLOBAL` non-weak syms;
look each up in `s1->dynsymtab_section`; error if not found.

(c) Decide on an opt-out flag (`-undefined dynamic_lookup` is gcc/ld
convention). Deferred — nobody currently needs it on PPC Tiger.

## What landed

### Source — `tcc/ppc-macho.c`

Five coupled changes (one file, no header touches):

1. **`tcc_add_macos_sdkpath`** now opens `/usr/lib/libSystem.B.dylib`
   and calls `macho_load_dll` to populate `s1->dynsymtab_section`.
   Also pre-seeds `dyld_stub_binding_helper` and `__dyld_func_lookup`
   directly into dynsymtab — Tiger dyld provides them at fixed
   `0x8fe00000`-base addresses but they're not in libSystem's nlist.

2. **`macho_load_dll`** refactored into a thin wrapper around a new
   `macho_load_dll_inner(..., int as_subdylib)`. The inner function:
   - Walks the LC_LOAD_DYLIB commands to collect sub-library paths.
   - Populates `s1->dynsymtab_section` (not `s1->symtab` — that was
     the pre-091 behavior and was anomalous vs `tccmacho.c`; user-code
     references flow through `put_extern_sym2` which adds them to
     symtab on demand, so we don't need to pre-pollute symtab).
   - Skips `tcc_add_dllref` when `as_subdylib == 1` — keeps the
     final EXE's LC_LOAD_DYLIB list libSystem-only even though
     libmathCommon's symbols are now in dynsymtab.
   - At top level (`as_subdylib == 0`), recurses into each collected
     LC_LOAD_DYLIB sub-library path with `as_subdylib = 1`.

3. **Symbol-name storage** in dynsymtab no longer strips the
   leading underscore. Pre-091 the code stored `atexit` (stripped)
   in symtab; tcc's `leading_underscore` mechanism makes symtab
   store `_atexit` (with underscore). The mismatch was only harmless
   because nothing was looking the dynsymtab entries up. Now that
   `check_symbols` does, names must match — store with underscore
   intact, matching `tccmacho.c`'s convention.

4. **New `ppc_macho_check_symbols`**: walks `symtab_section`, for
   each `SHN_UNDEF` sym checks `bind != STB_LOCAL`, `bind != STB_WEAK`,
   name nonempty. Allow-lists `__mh_execute_header` and `start`
   (synthesized later in `macho_output_exe`). Looks up the remainder
   in `s1->dynsymtab_section` via `find_elf_sym`; if not found,
   `tcc_error_noabort("undefined symbol '%s'", name)` and returns -1.

5. **`macho_output_file`** gate: when `output_type == TCC_OUTPUT_EXE`,
   calls `ppc_macho_check_symbols(s1)` and propagates -1. EXE-only —
   DLL and OBJ retain the loose pre-091 behavior.

Plus the matching forward declarations at the top of the file
(both `macho_load_dll` and the new static `ppc_macho_check_symbols`
are referenced earlier than they're defined).

### Demo — `demos/v0.2.67-undef-check.sh`

New. Four sub-tests covering the surface:

1. `extern void definitely_not_a_real_function_xyzzy(void); int main(){ ... }`
   → tcc must reject with `undefined symbol '_definitely_not_a_real_function_xyzzy'`.
2. `printf("hello"...)` → links and runs.
3. `log2(8.0)` → links and runs (exercises the LC_LOAD_DYLIB
   sub-library recursion: `log2` lives in
   `libmathCommon.A.dylib`, not libSystem.B.dylib's own nlist).
4. `otool -L` on the `log2` exe → only `/usr/lib/libSystem.B.dylib`
   (the umbrella). libmathCommon stays invisible despite supplying
   the symbol.

### Docs

- [`docs/roadmap.md`](../../roadmap.md):
  - Item #10 (Real-world program builds) reworded — expat's
    AC_CHECK_FUNCS finding now points forward to item #12's fix
    instead of leaving the issue open.
  - Item #12 marked `✅ Done in v0.2.67-g3`.
  - `v0.2.67-g3` row added to the milestone table.
  - "Where we are" line bumped from v0.2.66 to v0.2.67.
- [`demos/README.md`](../../../demos/README.md) — new row for
  `v0.2.67-undef-check.sh`.
- This README plus `findings.md`, `commits.md`, `HANDOFF.md`.

## Verification

All on imacg3 (real Tiger PowerPC G3):

- **Bogus undef probe** (the 2-line repro from session 090's HANDOFF)
  now produces `tcc: error: undefined symbol '_definitely_not_a_real_function_qwerty'`
  with exit code 1 (was: linked clean, dyld SIGABRT at runtime).
- **tests2**: 111 / 111 (unchanged).
- **abitest**: cc+tcc all `success` (unchanged).
- **Bootstrap fixpoint**: holds (`tcc-self2.o == tcc-self3.o`).
- **Demos re-run on imacg3**: v0.2.41-gzip, v0.2.40-sed,
  v0.2.12-lua, v0.2.25-dylib, v0.2.26-link-dylib, v0.2.66-awk,
  v0.2.66-expat, v0.2.18-bzip2, v0.2.21-cjson,
  v0.2.63-extern-data-ref, v0.2.64-funcptr-const,
  v0.2.67-undef-check — all PASS. (sqlite-file demo skipped
  because the amalgamation source isn't present on imacg3; not a
  regression.)
- **expat WITHOUT the `-Werror=implicit-function-declaration`
  workaround**: re-ran the autoconf configure on imacg3 without
  the CFLAGS shim. `expat_config.h` correctly says
  `HAVE_ARC4RANDOM 1` and `/* #undef HAVE_ARC4RANDOM_BUF */`
  (the workaround's purpose). Full 345/345 internal tests still
  pass. AC_CHECK_FUNCS is now honest on tcc-on-Tiger.

## How the fix differs from session 090's plan

Two surprises during implementation:

1. **PPC's `macho_load_dll` was registering dylib symbols in
   `s1->symtab`, not `s1->dynsymtab_section`.** This was the
   anomaly that motivated session 090's "auto-load" idea — if we
   loaded all of libSystem's exports into symtab, the eventual
   nlist would bloat with unreferenced libSystem entries. The fix
   was to align PPC's `macho_load_dll` with `tccmacho.c`'s
   convention (write to dynsymtab) — at which point auto-loading
   libSystem is harmless and `check_symbols` works.

2. **Tiger libSystem is an umbrella.** Its `LC_LOAD_DYLIB` lists
   `/usr/lib/system/libmathCommon.A.dylib`, and at runtime dyld's
   flat namespace finds `log2` / `sqrtf` / `exp2` etc. there. But
   libSystem's own nlist only has UNDEF references to those names.
   So an auto-load of just libSystem rejected every `log2`-using
   program. Adding sub-library recursion (with `as_subdylib=1` to
   suppress `tcc_add_dllref`) fixed it without polluting the
   exe's own LC_LOAD_DYLIB list.

## Open follow-ups

See [HANDOFF.md](HANDOFF.md). Notable:

- DLL output is NOT validated by `check_symbols` (only EXE).
  Probably fine — dylibs commonly contain unresolved syms that
  bind from the consuming exe — but worth revisiting if a real
  dylib build trips on it.
- `-undefined dynamic_lookup` opt-out: not implemented this
  session, since nothing on PPC Tiger needs it. Could be added
  in a one-liner state bit + check_symbols gate when first asked
  for.
