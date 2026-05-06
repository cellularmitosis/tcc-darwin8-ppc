# Findings — session 045

## v0.2.25 — initial dylib output

* dyld faults in `ImageLoaderMachO::doBindExternalRelocations`
  with `KERN_PROTECTION_FAILURE` if `LC_DYSYMTAB` is missing,
  even when there are no extern bindings. Always emit it (with
  all-zero counts when nothing else applies). Also always emit
  `LC_LOAD_DYLINKER` for dylibs.
* `__mh_dylib_header` is the dylib equivalent of
  `__mh_execute_header`. Register at `__TEXT` base as `N_ABS`.
* PIC stubs for dylib (8 instructions, 32 bytes per stub):
  ```
  mflr   r0
  bcl    20, 31, .L1   ; LR = .L1 (next insn)
  .L1:
  mflr   r11           ; r11 = .L1's absolute address
  addis  r11, r11, ha(slot - .L1)
  mtlr   r0            ; restore caller's LR
  lwz    r12, lo(slot - .L1)(r11)
  mtctr  r12
  bctr
  ```
  The `(slot - .L1)` displacement is a SECTDIFF: invariant under
  whole-image slide. The exe writer's 4-instruction stubs use
  absolute lis/lwz of the slot address — those break under
  slide, but exes are loaded at fixed `EXE_TEXT_VMADDR_BASE` so
  the slide path is never hit.
* Default dylib `__TEXT` vmaddr `0x40000000`. Tiger libSystem
  lives in `0x9xxxxxxx`, so this avoids conflict in the common
  case. We don't yet emit local relocations, so dylibs only work
  at default vmaddr — sliding silently breaks data initializers
  with absolute pointers (function calls survive via PIC).

## v0.2.26 — link-time dylib support

* `set_elf_sym` for an UNDEF re-add takes the new `st_other`
  but only updates the visibility bits if the existing sym was
  defined (line 717 in tccelf.c). For our case (registering
  UNDEFs from dylibs), the `else` branch at line 752 sets
  `st_other = other` outright. So custom bits in `st_other`
  could survive — but we don't need them; we use a simpler
  flat-namespace approach instead.
* Mach-O symbol names have a leading underscore (`_zlibVersion`)
  but tcc's symtab uses bare C names internally (`zlibVersion`).
  When importing from a dylib's `LC_SYMTAB`, strip the leading
  underscore before `set_elf_sym`. The exe writer re-adds it
  via `s->leading_underscore` at output time.
* Tiger libSystem doesn't ship `strnlen` (10.7+). Inline a small
  loop in `macho_load_dll` to extract the install-name string
  from `LC_ID_DYLIB`.
* When extra dylibs are loaded, switch to FLAT namespace (clear
  `MH_TWOLEVEL`). Strict TWOLEVEL would need per-symbol ordinal
  tracking via a side-table mapping each undef sym -> source dll.
  Flat is simpler and well-supported on Tiger.
* `dlltest` standalone passes consistently (5/5 runs). Under
  the full upstream `make -k test` suite, it sometimes fails
  with `Illegal instruction` — but that's the long-deferred JIT
  heisenbug surfacing because earlier tests (test3,
  abitest-tcc) use JIT and corrupt some memory state. The
  dylib feature itself is correct.

## Open problems still untouched in this session

* JIT heisenbug (carried from session 044). Surfaces in
  `make -k test` runs that mix JIT-using stages.
* Local relocs for slidable dylibs. Acceptable for now since
  default vmaddr usually wins.
