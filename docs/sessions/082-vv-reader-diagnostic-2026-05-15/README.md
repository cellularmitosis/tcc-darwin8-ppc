# Session 082 — `-vv` reader diagnostic (carried 068 #4)

**Started:** 2026-05-15
**Arrival HEAD:** `e3de11e` (end of session 081, v0.2.60-g3).
**Target:** v0.2.61-g3.

## Arrival state

Self-link-diagnostics arc closed at end of session 081:

* v0.2.50-g3 — EXE writer pre-write checks (session 068)
* v0.2.59-g3 — dylib writer pre-write checks (session 080)
* v0.2.60-g3 — post-write linter, in-process re-read after `fwrite + fclose`
  for both writers (session 081)

The **writer** side is now bracketed end-to-end: pre-write covers
in-memory state, post-write covers on-disk bytes. The **reader** side
(`macho_load_object_file` in `tcc/ppc-macho.c`) is still effectively
silent — when it skips a section, drops a relocation, fails to resolve
a stub-slot target, or sees a reloc/symbol it doesn't know how to
translate, it just `continue`s or `goto skip_pair_after`s with no log.
Two historical bugs would have been faster to triage with the diagnostic
this session adds:

* v0.2.40 sed-bug — the reader was silently skipping a section it
  needed.
* v0.2.44 SECTDIFF shadow-section bug — a SECTDIFF target VA didn't
  resolve to any loaded section, and the silent `goto skip_pair_after`
  hid the cause of a later mis-link.

This session adds verbose link-time logging for every silent decision
in the reader.

## Scout findings — silent-decision inventory

From the scout pass over `tcc/ppc-macho.c`, the reader makes silent
routing decisions at 25 sites grouped into four categories:

### Section skips (2 sites)

* `macho_section_to_elf` returns 0 for unknown `segname/sectname`
  pairs (`ppc-macho.c:6789-6790`). Examples: `__DWARF/__debug_*`,
  `__TEXT/__symbol_stub1`, `__DATA/__lazy_symbol_pointer`,
  `__DATA/__nl_symbol_pointer`. Filtered at the caller in pass 1
  of `macho_load_object_file` (`~7515-7517`).

### Relocation drops in `macho_translate_relocs` (~14 sites)

* `ppc-macho.c:6950-6951` — early return: section was skipped, so
  its reloc table is dropped wholesale (informative, but worth
  logging — the v0.2.40 bug had a cascade of these).
* `ppc-macho.c:6984-6995` — SECTDIFF reloc seen but `output_type`
  is neither `OBJ` nor `EXE` (e.g. dylib output).
* `ppc-macho.c:7020-7022` — unknown scattered reloc type.
* `ppc-macho.c:7067-7068` — **SECTDIFF target VA maps to no loaded
  section** (v0.2.44 bug shape).
* `ppc-macho.c:7103-7104` — `macho_resolve_stub_slot` returns −1.
* `ppc-macho.c:7111-7112` — translated stub target sym is 0.
* `ppc-macho.c:7168-7169` — standalone `PPC_RELOC_PAIR` (iteration
  mechanics; **don't log** — the paired half was consumed by the
  preceding iteration).
* `ppc-macho.c:7175` — non-extern target section out of range.
* `ppc-macho.c:7177` — reloc instruction bytes past file end.
* `ppc-macho.c:7204` — non-extern `r_type` not in the handled
  set (`VANILLA/BR24/JBSR/HA16/LO16/HI16`).
* `ppc-macho.c:7221` — non-extern stub-slot resolve failed.
* `ppc-macho.c:7266` — extern target sym index out of range.
* `ppc-macho.c:7272` — extern target sym filtered by
  `macho_translate_sym` (returns 0).
* `ppc-macho.c:7286` — extern `r_type` has no ELF mapping.

### Symbol skips in `macho_translate_sym` (5 sites)

* `ppc-macho.c:6861-6862` — STAB symbol (**don't log** — debug
  symbols are routinely skipped; under `-g` this would be hundreds
  per `.o`).
* `ppc-macho.c:6863-6864` — invalid `n_strx`.
* `ppc-macho.c:6866-6867` — empty name string.
* `ppc-macho.c:6901` — `n_sect` out of range.
* `ppc-macho.c:6918` — unknown `N_TYPE`.

### Pass-2 symbol-loop skips (4 sites)

* `ppc-macho.c:7724` — STAB symbol (**don't log**).
* `ppc-macho.c:7725` — non-extern symbol with `N_TYPE` other than
  `N_SECT` (e.g. `N_ABS` local).
* `ppc-macho.c:7728` — `n_sect` out of range.
* `ppc-macho.c:7729` — local in a discarded section.

**Total emit sites after filtering noisy mechanics**: ~20 (skip
PPC_RELOC_PAIR mechanics + STAB symbol skips).

## Existing diagnostic surface (comparison)

The reader already emits `tcc_error_noabort("macho: ...")` at hard
failure points (empty file, bad magic, truncated header, etc., 11
sites total in `macho_load_object_file` + `macho_translate_relocs`).
None of those are silent — they're the fatal half. The new logs
cover the *non-fatal* "we decided not to do X" half.

## `-v` plumbing — already supports `-vv`

* `unsigned char s->verbose` on `TCCState` (`tcc/tcc.h:771`).
* `-v` parser counts repeated `v`s: `do ++s->verbose; while (*optarg++ == 'v');`
  at `tcc/libtcc.c:2124-2126`. So `-v`, `-vv`, `-vvv` all already
  parse — no new option needed.
* Existing convention: level 1 = general info (file names being
  linked, output summaries); level 2 = detail (PE section walks
  at `tccpe.c:681,694`, ELF archive member info at
  `tccelf.c:3898-4067`, runtime relocs at `tccrun.c:372-443`).
* `ppc-macho.c` already prints level-1 writer summaries at 4486
  and 5868 via `printf("…")` to stdout.

## Design

### Gate

`s1->verbose >= 2`. Match the `tccelf.c` / `tccrun.c` convention
(level 2 enables detail; level 3 doesn't disable it).

### Helper

A single static `macho_reader_log(s1, category, fmt, ...)` near the
top of the reader section in `ppc-macho.c`. It:

* Returns early if `s1->verbose < 2`.
* Looks up `s1->current_filename` (set in `tcc_add_binary` at
  `libtcc.c:1129` before the reader is called) and strips the
  directory prefix.
* Writes `tcc: reader: <basename>: <category>: <details>\n` to
  **stderr** (not stdout — these are diagnostics; should be
  greppable separately from `tcc -run` program output).

Three category labels: `section-skip`, `reloc-drop`, `sym-skip`.

### Output format examples

```
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub1 (sect 5, flags 0x80000408)
tcc: reader: foo.o: reloc-drop: section __text reloc 12: SECTDIFF target VA 0x1234 maps to no section
tcc: reader: foo.o: reloc-drop: section __text reloc 7: stub-slot resolve failed (sect 3, value 0x2010)
tcc: reader: foo.o: sym-skip: index 14: n_sect 9 out of range [1, 7]
```

### Why `fprintf(stderr, ...)` instead of `printf(...)`

`ppc-macho.c`'s existing verbose prints (4486, 5868) use `printf` for
single-line writer success summaries — fine to mix with program output
of a separate compile/link invocation. The new reader logs are
diagnostics that may fire many-per-`.o`; routing them through stderr
keeps them separable from any program output (including `tcc -run`)
and matches the convention `tcc_error_noabort` itself uses (via
`tcc_warning` → stderr).

### What gets logged where

20 emit sites across 4 categories (see scout inventory above). Skipped
intentionally: STAB symbol filtering (too noisy under `-g`) and
standalone PPC_RELOC_PAIR (iteration mechanics, not a decision).

## What landed

### `tcc/ppc-macho.c`

* New helper `macho_reader_log(s1, category, fmt, ...)` (~30 lines,
  inserted just after `macho_section_to_elf`). Gated on
  `s1->verbose >= 2`. Looks up `s1->current_filename`, strips the
  directory prefix, writes `tcc: reader: <basename>: <category>:
  <details>\n` to **stderr** via `vfprintf`.
* **21 emit sites** wired up:
  - **2 section-skip sites**: pass-1 section skip in
    `macho_load_object_file` (the `macho_section_to_elf` returning 0
    cascade — both halves are covered by the one log call at the
    caller).
  - **13 reloc-drop sites**: 12 inside `macho_translate_relocs`
    (SECTDIFF illegal for output type, unknown scattered type,
    SECTDIFF target maps to no section, SECTDIFF stub-slot resolve
    fail, SECTDIFF translated sym is 0, non-extern target sect OOR,
    instruction past file end, non-extern type/length unhandled,
    non-extern stub-slot resolve fail, extern target sym OOR, extern
    translate_sym filtered, extern r_type has no ELF mapping) plus
    1 in pass-3 of `macho_load_object_file` for whole-section drops
    where a pass-1-skipped section still had populated relocs.
  - **7 sym-skip sites**: 4 in `macho_translate_sym` (bad n_strx,
    empty name, n_sect OOR, unknown N_TYPE basic) and 3 in pass-2 of
    `macho_load_object_file` (local with non-N_SECT type, n_sect
    OOR, local in discarded section).
* **Intentionally not logged**: STAB symbols (would flood under
  `-g` — debug syms are routine, not a routing decision) and
  standalone `PPC_RELOC_PAIR` continues (iteration mechanics — the
  paired half was consumed by the preceding iteration). 4 silent
  sites total (2 STAB + 2 PAIR-mechanics).

### Demo

* [`demos/v0.2.61-vv-reader-diagnostic.c`](../../../demos/v0.2.61-vv-reader-diagnostic.c)
  — tiny program calling `helper(7)` from a sibling `.o`.
* [`demos/v0.2.61-vv-reader-diagnostic.sh`](../../../demos/v0.2.61-vv-reader-diagnostic.sh)
  — builds `foo.o` + `bar.o`, links three times (default / `-v` /
  `-vv`), asserts:
  - default stderr is empty (no reader logs at default verbosity)
  - `-v` stderr has zero `tcc: reader:` lines (writer summary only)
  - `-vv` stderr has ≥ 5 reader lines, covering both `section-skip`
    and `reloc-drop` categories, each matching the documented
    `tcc: reader: <basename>: <category>: <details>` format
  - `-g` + `-vv` produces zero `sym-skip` lines (STAB filter holds)
  - the linked program still outputs `v=14`

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped ("v0.2.60-g3"
  → "v0.2.61-g3", "Thirty-eight" → "Thirty-nine"); v0.2.61-g3 row
  prepended.
* [`demos/README.md`](../../../demos/README.md) — v0.2.61 row added
  above v0.2.60.

## Results on imacg3

Full regression at default verbosity:

* `bash scripts/run-tests2.sh` → **Total: 111  Pass: 111  Fail: 0**
* `make abitest` in `tcc/tests/` → **48/48 success** (24 cc + 24 tcc)
* `FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh` → **FIXPOINT HOLDS**

Demo: **PASS: -vv reader diagnostic surfaces 17 reader lines
(9 section-skip, 8 reloc-drop); default verbosity silent**.

All ten v0.2.25..v0.2.61 demos green: v0.2.25-dylib, v0.2.26-link-dylib,
v0.2.29-dylib-slide, v0.2.31-libtcc-slide, v0.2.40-sed, v0.2.41-gzip,
v0.2.42-python, v0.2.59-dylib-self-link-diagnostics,
v0.2.60-post-write-linter, v0.2.61-vv-reader-diagnostic.

## What the diagnostic surfaces on a real link

Linking a trivial `int main() { return 0; }` against `crt1.o` with
`-vv` (stderr only):

```
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub (sect 1, flags 0x80000008, size 0, nreloc 0)
tcc: reader: crt1.o: section-skip: __TEXT,__picsymbol_stub (sect 2, flags 0x80000008, size 0, nreloc 0)
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub1 (sect 3, flags 0x80000408, size 64, nreloc 16)
tcc: reader: crt1.o: section-skip: __DATA,__nl_symbol_ptr (sect 6, flags 0x00000006, size 12, nreloc 0)
tcc: reader: crt1.o: section-skip: __DATA,__la_symbol_ptr (sect 7, flags 0x00000007, size 16, nreloc 4)
tcc: reader: crt1.o: reloc-drop: section __text reloc 21: non-extern stub-slot resolve failed (target sect 3, value 0x330)
tcc: reader: crt1.o: reloc-drop: section __text reloc 23: non-extern stub-slot resolve failed (target sect 3, value 0x340)
tcc: reader: crt1.o: reloc-drop: section __text reloc 29: non-extern stub-slot resolve failed (target sect 3, value 0x350)
tcc: reader: crt1.o: reloc-drop: section __text reloc 85: non-extern stub-slot resolve failed (target sect 3, value 0x370)
tcc: reader: crt1.o: reloc-drop: section __TEXT,__symbol_stub1 skipped: 16 relocs dropped wholesale
tcc: reader: crt1.o: reloc-drop: section __DATA,__la_symbol_ptr skipped: 4 relocs dropped wholesale
```

The five section-skips are all **expected** — `__symbol_stub`,
`__picsymbol_stub`, `__symbol_stub1`, `__nl_symbol_ptr`,
`__la_symbol_ptr` are Apple-internal sections that tcc's link path
re-synthesizes from scratch via the indirect symtab.

The two **wholesale reloc drops** cascade from the section skips —
when `__symbol_stub1` is dropped, its 16 relocs (4 per stub, 4
stubs) are dropped with it; same for `__la_symbol_ptr`'s 4 relocs.

The four **non-extern stub-slot resolve fail** entries reveal
something genuinely interesting: crt1.o contains internal branches
in `__text` (relocs at indices 21, 23, 29, 85) targeting absolute
addresses 0x330, 0x340, 0x350, 0x370 — which lie within
`__symbol_stub1`'s address range. The loader's
`macho_resolve_stub_slot` walks `(target_value - sec->addr) /
entry_size` to index into the indirect table, but for crt1.o the
result lands past `n_indirect`, so `resolve_stub_slot` returns -1
and the reloc is dropped. The fact that hello-world still runs
through `crt1.o` proves these dropped relocs don't break anything
at runtime — those branches are presumably patched by dyld's stub
binder, not tcc's static link. Worth a closer look in a future
session, but **the diagnostic surfaces it; the bug-or-not? question
is now askable.** That's the point.

## Findings worth remembering

### `-vv` was already in the option parser

The `-v` parser at `libtcc.c:2124-2126` is
`do ++s->verbose; while (*optarg++ == 'v');` — it counts repeated
`v`s, so `-vv` and `-vvv` were always accepted, just not used. No
new option needed.

### Convention: `>= 2` for detail prints

`tccpe.c`, `tccelf.c`, `tccrun.c`, `tccpp.c` all gate detail prints
at level 2 with patterns like `if (2 == s1->verbose)` or
`if ((verbose|1) == 3)`. Used `>= 2` here for ergonomics — level 3
should not be less verbose than level 2.

### Stream choice: stderr for diagnostics

`ppc-macho.c`'s existing verbose prints (writer summaries at lines
4486, 5868) use `printf` (stdout) for single-line success messages.
The new reader diagnostics may fire dozens per `.o`, are diagnostics
not summaries, and shouldn't mix with `tcc -run` program output —
so route through `fprintf(stderr, ...)`. Matches the convention
`tcc_error_noabort` → `tcc_warning` uses internally.

### `s1->current_filename` is the right hook for filename

Set in `tcc_add_binary` at `libtcc.c:1129` right before the
target-specific reader is called. Available throughout
`macho_load_object_file` and its helpers without adding a parameter.

### Design call: re-read in-process, not shell out

(Carries forward the v0.2.60 decision logic to the read path —
single C helper, no fork-and-grep fragility, runs anywhere tcc
runs.)

### Verification helper for the diagnostic shape itself

The demo asserts not just that lines exist but that they match the
documented `tcc: reader: <basename>: <category>: <details>` shape
(via a `grep -vE` against the expected pattern). If a future
emitter accidentally writes lines without that shape, the demo
catches it.
