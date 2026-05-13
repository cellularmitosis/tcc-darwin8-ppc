# Session 075 — stabs round-trip through `tcc -c` (v0.2.54-g3)

**Date:** 2026-05-13
**Build hosts:** uranium ⇄ imacg3 (rsync + ssh)
**Arrival HEAD:** `7c062ba` (v0.2.53-g3, end of session 074).
**Exit HEAD:** session 075 commit (v0.2.54-g3).
**Slug history:** opened as `075-n-oso-scope`; the planned "scope
assessment for item 1b" expanded into the full first step of 1b
landing in the same session.

## TL;DR

`tcc -gstabs` stabs now survive the `.o` round-trip: `tcc -gstabs
-c foo.c -o foo.o; tcc -gstabs foo.o -o foo` produces a Mach-O
exe with the full stab chain in `LC_SYMTAB`, matching what the
one-step `tcc -gstabs foo.c -o foo` flow has produced since
v0.2.51. The two-step flow is what real Makefiles use, so this
unlocks gdb-on-Tiger for normal build systems, not just for
single-source quick demos.

* **v0.2.53-g3 → v0.2.54-g3**
* **Source changes:** `tcc/ppc-macho.c` (+~80 lines across 5 hunks)
* **Demo added:** [`demos/v0.2.54-stabs-roundtrip.{c,sh}`](../../../demos/) — two-source-file build that exercises the full cross-TU gdb workflow.
* **tests2 111 / 111; abitest cc+tcc 24/24; bootstrap fixpoint holds.**

## Scope reassessment

Session 074's HANDOFF flagged roadmap #7.5 Phase 2 item 1b
("emit `N_OSO` pointing at a real `.o` file path") as the
highest-value remaining work, and called it "a session-sized
chunk — likely most of a focused day." Investigation found that
1b in its full form is actually a multi-session arc — three
structural gaps need closing before `dsymutil exe -o exe.dSYM`
can produce a standalone bundle:

1. **`.o` writer drops `.stab` entirely** — `classify_section`
   maps `.debug_*` to `__DWARF,__debug_*` but had no mapping for
   `.stab` / `.stabstr`, which fell through to the `!SHF_ALLOC`
   gate.
2. **`.o` reader has no path to load stabs back** — even if step
   1 emitted them, `macho_load_object_file` had no code to
   repopulate `stab_section` + its parallel reloc table.
3. **No `N_OSO` schema** — `tcc/stab.def` has no
   `__define_stab (N_OSO, 0x66, ...)` entry; v0.2.51's release
   name "OSO STAB" turned out to be aspirational (the actual
   change in 072 was generic stabs+ emission, not N_OSO).

Per the user's pick of **Option A** from the session-075 plan
doc, this session scoped to step 1 (`.o` write of stabs) plus the
loader side (step 2). The N_OSO schema + per-`.o`-path threading
+ `dsymutil` end-to-end demonstration are deferred to a follow-up
session — but now they have a working stab chain to plug into.

## Investigation phase

* Read 074's HANDOFF; confirmed v0.2.53-g3 is HEAD and tag is
  pushed (no v0.2.53 housekeeping remaining).
* Grepped for N_OSO / 0x66 / `__define_stab.*OSO` across `tcc/`;
  no hits.
* Read `classify_section` in `tcc/ppc-macho.c:370+` — confirmed
  `.stab` isn't in the dwarf_map; falls through to the
  `!SHF_ALLOC` skip.
* Read `macho_load_object_file` at line 6218+ and
  `macho_section_to_elf` at line 5670+ — no reverse mapping for
  `__DWARF,__stab` / `__DWARF,__stabstr`.
* Rsynced uranium → imacg3, `make clean && make`, ran demos and
  tests2. v0.2.53 arrival state confirmed solid (demo green,
  tests2 111/111).
* Wrote session-075 README scope-assessment laying out three
  options (A: land step 1 here, B: pivot to 1d default-stabs,
  C: pick from carried list). User picked A.

## Implementation

Five coupled changes in `tcc/ppc-macho.c`.

### (1) `classify_section` — forward mapping at .o write

Added two entries to the `dwarf_map[]` table in
`classify_section()`:

```c
{".stab",    "__stab"},
{".stabstr", "__stabstr"},
```

Both go into `__DWARF` segment with `S_ATTR_DEBUG`. This matches
Apple's convention — anything debug-related that isn't a
Mach-O nlist (stabs in LC_SYMTAB) lives in __DWARF. With this
change, `tcc -gstabs -c foo.c -o foo.o` writes a Mach-O `.o`
whose section table has `__DWARF,__stab` + `__DWARF,__stabstr`
slots populated.

### (2) `macho_section_to_elf` — reverse mapping at .o read

Added the inverse mapping:

```c
if (!strcmp(segname, "__DWARF") && !strcmp(sectname, "__stab")) {
    *out_name = ".stab";
    *out_sh_type = SHT_PROGBITS;
    *out_sh_flags = 0;
    return 1;
}
/* same for __stabstr -> .stabstr, SHT_STRTAB */
```

`sh_flags=0` keeps them non-allocated (no runtime mapping). The
loader's existing section-merge logic then appends the .o's
stab data to the merged `stab_section` (which the link tcc's
`tcc_debug_new` has already created with the standard 12-byte
placeholder).

### (3) `emit_section_relocs` — let STT_SECTION relocs through

The stab section's relocs target `text_section`'s STT_SECTION
sym (the anchor for all SLINE / BNSYM / ENSYM / N_SO / LBRAC /
RBRAC entries). `build_symtab` deliberately skips STT_SECTION
syms (Mach-O doesn't need a separate nlist entry for them — the
section number alone identifies the target), so they're not in
the xlat table. Before this session, the gate at the top of
`emit_section_relocs` rejected them outright:

```c
if (elfsym <= 0 || !xlat_present[elfsym]) continue;
```

Split that into:

```c
if (elfsym <= 0) continue;
esym = ...;
if (!xlat_present[elfsym]) {
    if (ELFW(ST_TYPE)(esym->st_info) != STT_SECTION)
        continue;
}
```

The downstream local-handling path already uses `st_shndx` to
look up the section number, so STT_SECTION syms route to the
right output reloc form without needing an nlist entry.

### (4) `macho_load_object_file` — Pass 1.5 stabs fixup

Stab entries carry `n_strx` as a raw `uint32` offset into the
.o's `.stabstr` (not a reloc target — it's just an in-struct
index). After Pass 1 appends the .o's `.stabstr` to the merged
`.stabstr` at offset `stabstr_off`, each just-loaded `.stab`
entry's non-zero `n_strx` needs `stabstr_off` added so it indexes
into the merged strtab. The mirror of what `tcc_load_object_file`
(the ELF path in `tccelf.c:3598`) does.

Inserted a new Pass 1.5 between the section-merge loop and Pass 2.
Walks the loaded section list looking for `.stab` + `.stabstr`,
walks the just-appended `.stab` slice, adds `stabstr_off` to each
non-zero `n_strx`. Also wires `stab_section->link` /
`sh_entsize` / `sh_addralign` on first encounter (defensive — the
link tcc's `tcc_debug_new` under `-gstabs` already does this).

### (5) SECTDIFF target-section search — skip S_ATTR_DEBUG

This was the bug that turned a clean implementation into a
crash. Once `__DWARF,__stab` is mapped via `macho_section_to_elf`,
it appears in `ctx->sec_to_tcc` as a non-NULL entry with
`addr=0` (DWARF layout convention — debug sections don't get
loaded VAs). The SECTDIFF target-section search in
`macho_translate_relocs` (lines 5982+) iterates `ctx->sects[]`
looking for the section whose `[addr, addr+size)` range contains
the scattered reloc's `r_value`. For a PIC reference to an
extern data symbol, `r_value` is the .o-internal address of the
`__nl_symbol_ptr` slot (e.g. `0x140` for `__sF` in stdoutref.o).

`.stab` at `addr=0, size=0x6cc` *contains* `r_value=0x140`. So
the search picks `.stab` instead of `__nl_symbol_ptr`. The code
then takes case (b) (real merged section) and synthesizes a
local anchor sym at text-offset `0x140` instead of resolving
through the indirect symtab to `__sF`. Result at exe-write time:
`nl_for_elfsym` has no slot for `__sF`, exe_resolve falls into
the "locally-defined" fallback path which rewrites the `lwz`
through `__nl_symbol_ptr` into an `addi` directly — pointing at
random memory in the header region. SIGBUS at runtime.

The fix: also skip `S_ATTR_DEBUG` sections during the search:

```c
if (t->flags & S_ATTR_DEBUG)
    continue;  /* never a SECTDIFF target */
```

Hidden by all prior testing because we'd never previously mapped
a __DWARF section back through `macho_section_to_elf` — the
__DWARF,__debug_* sections in v0.2.39+ were written to .o but
never round-tripped.

### (6) `emit_stab_nlist` — skip placeholder entries

Each `.o` compiled with `-gstabs` writes a leading 12-byte
placeholder Stab_Sym at the start of its `.stab` (`n_type=0`,
no name, no reloc — emitted by `tcc_debug_new`). When the link
tcc loads multiple `.o`s, each contributes its own placeholder
in addition to the link tcc's. `emit_stab_nlist` already skipped
the very first entry (`for idx=1; ...`); now it also skips any
subsequent `n_type == 0` placeholder to avoid emitting junk
stabs into the exe's `LC_SYMTAB`.

## The bug that almost shipped

After the four-coupled-changes implementation, the single-file
two-step (`hello-stabs.c` → `hello-stabs.o` → `hello-stabs`)
worked end-to-end — gdb breakpoints by file:line, prints, the
whole workflow. But the multi-file two-step demo crashed at
runtime with SIGBUS at `lwz r3, 0(r12)` while loading `g_seed`
(extern data).

Bisect-by-revert: even reverting every change except the
classify_section forward-mapping reproduced the crash. The
forward mapping AND the macho_section_to_elf reverse mapping
together were enough.

Adding a `fprintf(stderr, ...)` to `exe_resolve_section_relocs`
showed the broken case: the __text PIC reloc targeting `__sF`
was being processed against a symidx with shndx=text (not
SHN_UNDEF) and slot_idx=-1 — i.e., the loader had translated
the reloc to point at a synthesized local anchor in text instead
of routing through the indirect symtab to `__sF`. That smelled
like a SECTDIFF target-section misidentification, which (per
the existing comment in `macho_translate_relocs`) was a known
issue with debug sections at `addr=0`. The S_ATTR_DEBUG skip
closes the gap for the new `.stab` case.

A good test catches this kind of regression. The multi-file
demo (`v0.2.54-stabs-roundtrip.sh`) does — it exercises PIC
data references explicitly via `extern int g_seed` from a
different TU.

## Verification (imacg3, Tiger 10.4.11 PPC G3)

* Built fresh via `scripts/build-tiger.sh` after rsync.
* `bash demos/v0.2.54-stabs-roundtrip.sh` — green:
  * Each `.o` has `__DWARF,__stab` + `__DWARF,__stabstr` with
    non-zero `nreloc` on `__stab`.
  * Linked exe has SO/FUN/SLINE/BNSYM/ENSYM entries in LC_SYMTAB
    (9 SO, 10 FUN, 23 SLINE, 5 BNSYM, 5 ENSYM).
  * `helper_add` / `helper_mul` (from TU 2) and `main` (from TU 1)
    all appear as FUN entries.
  * Program runs and prints `a=15 b=30`.
  * gdb script: break in main, helper_add, helper_mul; run; print
    `g_seed=5`; step past assignments in each helper; print
    `sum=15`, `product=30`; backtrace shows file:line in both
    TUs (`helper_add ... helpers.c:18`, `main ... roundtrip.c:47`).
* `bash demos/v0.2.53-bnsym-ensym.sh` — green (no regression).
* `bash scripts/run-tests2.sh` — 111 / 111.
* `cd tcc/tests && make abitest` — 48 success (24 cc + 24 tcc).
* `FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh` — FIXPOINT HOLDS.

## Status of session 074's open items

| # | Item | Status |
|---|---|---|
| (074 #1b) | Emit `N_OSO` pointing at real `.o` path | **prerequisite landed (this session); N_OSO emission itself unchanged** |
| (074 #1d) | Default `-g` to stabs on Darwin | unchanged |
| (074 #2 carried) | self-link diagnostics for `macho_output_dylib` (068 #2) | unchanged |
| (074 #2 carried) | Post-write linter (otool/nm) (068 #3) | unchanged |
| (074 #2 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (074 #2 carried) | Sibling r11 watch (067 #3) | unchanged |
| (074 #2 carried) | AltiVec intrinsics | unchanged |
| (074 #2 carried) | bcheck.c port | unchanged |
| (074 #3) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (counted as one more confirmed rebuild cycle) |
| (074 #4) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open for next session

* **#7.5 Phase 2 item 1b proper.** Now that stabs round-trip
  through `.o`, the next step is emitting `N_OSO` entries
  pointing at real `.o` paths so `dsymutil exe -o exe.dSYM`
  produces a standalone bundle. Concretely: add `N_OSO` to
  `tcc/stab.def`, capture each input `.o`'s `realpath` + mtime
  at load time, emit one `N_OSO` per input `.o` in
  `emit_stab_nlist` between per-`.o` stab subranges. Detail in
  074's HANDOFF section 1.
* **#7.5 Phase 2 item 1d** — flip `-g` default to stabs on
  Darwin. Now that the full stabs pipeline (1-step, 2-step,
  cross-TU, params/locals/globals/BNSYM-ENSYM) works, this is
  a small `configure` change + release note.
* Carried multi-session arcs (068 #2/#3/#4, AltiVec, bcheck.c)
  unchanged.

(README ends here; HANDOFF.md has the next-session pickup
instructions.)
