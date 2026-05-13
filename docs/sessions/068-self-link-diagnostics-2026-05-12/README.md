# 068 — Self-link diagnostics (roadmap #7) — 2026-05-12

## Arrival state

- HEAD: `df31e67` (end of session 067).
- v0.2.49-g3 tag is on origin pointing at `e6d435a` (clz/ctz UB fix).
- Regression suite: green at end of 067.
- Roadmap item #7 ("Self-link diagnostics") is the long-open
  follow-up to session 025's debugging marathon: when the EXE writer
  in `tcc/ppc-macho.c::macho_output_exe` produces a syntactically
  valid but semantically broken Mach-O, the user finds out at run
  time via `dyld: Symbol not found …`, `Cannot allocate memory`,
  or `SIGBUS during crt1 startup` — with no link to the actual root
  cause inside tcc.

## What landed

Four pre-write sanity checks inserted in `macho_output_exe` just
after the __DWARF layout block, before any byte of the output file
is serialized. On any failure: emit one `tcc_error_noabort` per
violation (multiple OK — they all print) and `goto cleanup`.

### (a) VM layout

- `text_seg_vmaddr` == `EXE_TEXT_VMADDR_BASE` and page-aligned.
- `text_seg_filesize` page-aligned; `text_seg_vmsize >= filesize`.
- `data_seg_vmaddr` == `text_seg_vmaddr + text_seg_vmsize` (no gap,
  no overlap with __TEXT). Mirror invariant for `data_seg_file_off`.
- `data_seg_filesize` page-aligned; `data_seg_vmsize >= filesize`.
- `linkedit_vmaddr` == `text_seg_vmaddr + text_seg_vmsize +
  data_seg_vmsize` — uses **vmsize**, not filesize. **This is the
  session-025 "Cannot allocate memory" bug directly:** linkedit
  computed off filesize collides with __bss's vmsize footprint.
- `linkedit_file_off` == `text_seg_filesize + data_seg_filesize`.
- Per-section: every section's `[vmaddr, vmaddr+size)` lies inside
  its owning segment's `[vmaddr, vmaddr+vmsize)`.
- Adjacent sections within the same segment do not overlap.
- __DWARF sections excluded from layout checks (vmaddr=0 by design).

### (b) Required symbols

- `__mh_execute_header` is present in `s1->symtab` as `SHN_ABS` with
  `st_value == text_seg_vmaddr`. **This is the session-025 "Symbol
  not found: __mh_execute_header" bug:** if the registration at
  line ~2017 is broken, crt1's UNDEF ref routes through libSystem
  and dyld fails at load.
- `entry_addr` falls within `[text_sect_vmaddr, text_sect_vmaddr +
  text_data_size)`. Catches a misset entry point.

### (c) Stub/slot wiring

- Every `stubs[i].addr` lies in
  `[sects[sect_idx_stub].vmaddr,           +size)`.
- Every `stubs[i].la_ptr_addr` lies in
  `[sects[sect_idx_nlptr].vmaddr,          +size)`.
- Every `nlptrs[i].slot_addr`         lies in same nlptr range.
- Every `ST_PPC_NEEDS_STUB` UNDEF symbol has `stub_for_elfsym[i] >= 0`.
- **This is the session-025 crt1 SIGBUS:** the indirect-call
  sequence (`lis r12, ha(addr); ori r12, r12, lo(addr); mtctr;
  bctr`) landed on data slots instead of code stubs, faulted on
  bctr.

### (d) Section-presence

For every defined symbol in `s1->symtab` (skip SHN_UNDEF / SHN_ABS /
SHN_COMMON / STT_SECTION / STT_FILE), the symbol's target tcc
Section* must appear in `sects[]` as the `elf` field of some entry.
**This is the session-025 "common in .bss but __bss not emitted"
bug:** symbols silently disappear from the linked image when their
section is dropped, surfaces as runtime crash. One error per missing
section (tracked in a small `reported[]` bitmap so we don't flood
when many symbols share a missing section).

## Implementation notes

- All four checks share a single block at the natural pre-serialize
  point in `macho_output_exe` (just before line `/* ---- Serialize:
  Mach header. ---- */`). Linear walks; no new data structures
  required.
- Cost: one loop over `nsec` (max 32), two loops over the symtab
  (typically <1000 entries), one loop over stubs+nlptrs. Negligible
  vs the actual link work (<1 ms per link in practice).
- The block sets `sanity_ok = 0` on any failure, emits as many
  errors as it finds, then jumps to `cleanup` only at the end — so
  the user sees the full picture rather than chasing one error at a
  time.
- `macho_output_dylib` was **not** modified in this pass. Its
  failure modes are similar but slightly different (no __PAGEZERO,
  no __mh_execute_header, no entry point). Extending the same set
  of checks there is a natural follow-up but out of scope for #7
  as scoped here. (Listed as open work below.)
- The hand-injected breaks I used to verify each check did not
  leave any test infrastructure in the codebase — they were
  ephemeral edits, applied and reverted in-session. The session
  log below records the exact errors observed.

## Verification — each check fires when its invariant breaks

Each break was a one-line injection at the top of the sanity-check
block, rebuilt, rerun on `imacg3`, then reverted.

### Break #1 (VM layout):
```c
text_seg_vmaddr ^= 0x10000;
```
Output (cascade of related violations is informative — same root
cause shows in multiple places):
```
tcc: error: ppc-macho: __TEXT vmaddr 0x11000 != expected EXE_TEXT_VMADDR_BASE 0x1000
tcc: error: ppc-macho: __DATA vmaddr 0x2000 != __TEXT vmaddr+vmsize 0x12000 (gap or overlap)
tcc: error: ppc-macho: __LINKEDIT vmaddr 0x4000 != __TEXT+__DATA end 0x14000 (would collide with __bss or earlier section)
tcc: error: ppc-macho: section '.text' [0x1484+0x630] escapes its segment [0x11000+0x1000]
tcc: error: ppc-macho: section '.data.ro' [0x1ab4+0xfd] escapes its segment [0x11000+0x1000]
tcc: error: ppc-macho: section '<synthetic>' [0x1bc0+0x60] escapes its segment [0x11000+0x1000]
tcc: error: ppc-macho: section '.eh_frame' [0x1c20+0x64] escapes its segment [0x11000+0x1000]
tcc: error: ppc-macho: '__mh_execute_header' value 0x1000 != __TEXT base 0x11000
```

### Break #2a (__mh_execute_header):
```c
int _t = find_elf_sym(s1->symtab, "__mh_execute_header");
if (_t > 0) ((ElfW(Sym) *)s1->symtab->data)[_t].st_shndx = SHN_UNDEF;
```
Output:
```
tcc: error: ppc-macho: '__mh_execute_header' has shndx 0, expected SHN_ABS
```

### Break #2b (entry_addr out of __text):
```c
entry_addr = 0xdeadbeef;
```
Output:
```
tcc: error: ppc-macho: entry_addr 0xdeadbeef outside __text [0x1484+0x630] (would crash at dyld handoff)
```

### Break #3 (stub la_ptr_addr corruption):
```c
if (nstubs > 0) stubs[0].la_ptr_addr = 0xfeedface;
```
Output:
```
tcc: error: ppc-macho: stub 0 la_ptr_addr 0xfeedface outside __nl_symbol_ptr [0x201c+0x2c]
```

### Break #4 (bss section dropped from `sects[]`):
```c
{ int _k; for (_k=0;_k<nsec;_k++) if (sects[_k].elf == bss) sects[_k].elf = NULL; }
```
Output (with a hello-world that uses a static array):
```
tcc: error: ppc-macho: symbol '_big_bss' is defined in section '.bss' but that section is not emitted by the EXE writer (missed section-pickup case in macho_output_exe?)
```

In every case `tcc` exits 1 with no Mach-O file written — the
diagnostic surfaces *before* the broken image is produced.

## Regression suite (post-checks, no breaks injected)

On `imacg3` after the changes were in place:

- `make -C tcc tcc` — clean build, only pre-existing warnings.
- `make libtcc1.a` — clean.
- tests2: **111 / 111 (100%)**.
- abitest cc + tcc: all `success`.
- Hello-world smoke: `tcc -o /tmp/h /tmp/h.c && /tmp/h` →
  `self-link works`, exit 0.
- Sampled demos that exercise different code paths: all PASS.
  - `demos/v0.2.49-clz-ctz-ub.sh`
  - `demos/v0.2.48-r12-clobber.sh`
  - `demos/s025-self-link.sh`
  - `demos/v0.2.42-python.sh`
- New: `demos/v0.2.50-self-link-diagnostics.sh` → PASS, prints
  `rodata works | data=42 | bss=0 33 77 | p=ok`.

`make -C tcc/tests test` (the broader upstream-tcc test driver)
hits a pre-existing libtest_mt failure under "running tcc.c in
threads" — not introduced by this change; same status as session
067. Worth a future cleanup pass but unrelated to #7.

## Bootstrap

Not run end-to-end this session. The diagnostics block is purely
additive (single `if`/`tcc_error_noabort` site, no mutation of
emitter state). All happy-path tests pass; bootstrap was last
verified clean in session 067.

## Files changed

- `tcc/ppc-macho.c` — sanity-check block in `macho_output_exe`,
  inserted between the __DWARF layout block and the Mach-header
  serialize step.
- `demos/v0.2.50-self-link-diagnostics.{c,sh}` — the milestone demo.
- `demos/README.md` — table row.
- `docs/roadmap.md` — v0.2.50 row, #7 marked done.
- `docs/sessions/068-self-link-diagnostics-2026-05-12/{README,HANDOFF}.md`
  — this session.

## What this doesn't catch

The checks are pre-write invariant guards on `macho_output_exe`.
They do not catch:

- Bugs in the .o **reader** (`macho_load_object_file`) — those
  already have decent diagnostics in their own right.
- Bugs in **`macho_output_dylib`** — same shape of EXE writer, not
  modified here. Reasonable follow-up.
- Codegen bugs that produce wrong instruction encodings inside a
  valid section layout. Those still surface at runtime as wrong
  behavior or SIGBUS. (Different problem space — addressed by
  csmith differential testing.)
- Runtime-only bugs from dyld: bad symbol-table sort order,
  incorrect `LC_DYLINKER`/`LC_LOAD_DYLIB` paths, missing
  `MH_DYLDLINK` flag, etc. These would benefit from a separate
  "verify generated mach-o" linter pass that uses `otool`/`nm` after
  the write — out of scope for this session.

## Open follow-ups

- **Extend the same checks to `macho_output_dylib`.** Same writer
  shape, different fixed points (no __PAGEZERO, no
  __mh_execute_header, vmaddr base = 0x40000000, no entry_addr).
  Probably 50-100 LOC mirrored from the EXE block.
- **Post-write linter.** A small helper that reopens the just-
  written file and verifies the Mach header / load commands match
  the in-memory layout. Catches a different class of bug:
  obuf-corruption / put32be-byteorder regressions / wrong cmd-size.
  Useful but distinct from "is the design correct."
- **Diagnostic for the reader's `else { continue; }` paths.** The
  reader has several `continue`-on-unknown sites
  (`macho_translate_relocs` PIC anchor synthesis, etc.) that
  silently drop relocations. A diag mode that emits one-line "
  dropped reloc type N targeting <section>" warnings on `-vv`
  would help diagnose archive-loading regressions like the
  libgcc.a whole-archive bug from session 025.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.
