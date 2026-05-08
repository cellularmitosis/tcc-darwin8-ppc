# Session 053 — DWARF in linked exe (2026-05-08)

## Arrival state

* HEAD `77def3b`, `v0.2.37-g3` shipped at end of session 052.
* `tcc -c -g foo.c` produces a Mach-O `.o` that Tiger's `dwarfdump`
  parses cleanly. CIE / line table / abbrev / info / strings — all
  present and correct.
* Test suite: 111/111 in `tests2`. Bootstrap fixpoint holds.
* The exe path (`tcc -g -o foo foo.c` and the link step inside
  `-run`) does not yet emit DWARF sections into the linked
  Mach-O — `macho_output_exe` doesn't enumerate `.debug_*`.

## Goals

Per session 052's HANDOFF "Open work / DWARF — second half":

1. Section enumeration: walk `s1->sections[]` for `.debug_*` (or
   use `s1->dwlo .. s1->dwhi`) inside `macho_output_exe`'s
   "find sections" loop.
2. Layout: place `.debug_*` after `__LINKEDIT`. `vmaddr` = 0,
   `vmsize` = 0, but file_offset / file_size = real.
3. Relocation application: should work as-is once enumeration
   includes them. R_PPC_ADDR32 to .text resolves to
   `text.sh_addr + addend = runtime VA`, which is what lldb
   wants for exe DWARF.
4. Load command: emit `LC_SEGMENT __DWARF` with per-section
   headers carrying `S_ATTR_DEBUG`. Mirror the existing
   __TEXT / __DATA / __LINKEDIT segment-write code.

## What landed

**`v0.2.38-g3`**, in two commits (one source change, one release
bookkeeping):

1. `ppc-macho.c::macho_output_exe` grew end-to-end DWARF support:
   * Section enumeration (uses `classify_section`'s existing
     `.debug_*` handling — same code path as the .o writer).
   * Per-DWARF-section bookkeeping: parallel arrays for the ELF
     section, the chosen `(__DWARF, __debug_*)` segment/section
     names, the S_ATTR_DEBUG flag, and a writable copy of the
     section bytes.
   * Layout: the new `__DWARF` segment lands after `__LINKEDIT`
     in file order. `vmaddr=0/vmsize=0` (debug data isn't
     loaded; dyld doesn't try to map it). Per-section file
     offsets are byte-packed (4-byte aligned at the segment
     start). Per-section `addr` stays 0 — same trick as the
     .o writer, so cross-DWARF R_PPC_ADDR32 resolves to
     `0 + addend = addend` (offset within the target debug
     section, which is what dwarfdump reads).
   * Relocation: `exe_resolve_section_relocs` runs over each
     `.debug_*` section. R_PPC_ADDR32 from `.debug_info` to
     `.text` resolves to `text_runtime_va + addend` — the
     actual VA, which dwarfdump reports as `AT_low_pc /
     AT_high_pc`.
   * LC emit: a fresh `LC_SEGMENT __DWARF` block right after
     `LC_SEGMENT __LINKEDIT`, with one section header per
     debug section carrying `S_ATTR_DEBUG`.
   * Cleanup: free the per-section data buffers and parallel
     arrays at the end of `macho_output_exe`.

2. `EXE_MAX_SECTS` bumped 16 → 32 to leave headroom for the
   ~5-9 `.debug_*` sections plus the existing 9 loaded sections.
   Counter messages now report DWARF count too.

3. `R_PPC_REL32` case added to `exe_resolve_section_relocs`. Used
   by `.eh_frame` FDE `initial_location` fields where the
   in-place value holds the function's offset within `.text`
   and the relocation is to `.text`'s section symbol. Treats
   the in-place as the addend (ELF Rel-format implicit-addend
   semantics) and writes
   `delta = (text_runtime_va + addend) - (sect_vmaddr + reloc_off)`.

4. `ppc-link.c::relocate`'s pre-existing R_PPC_REL32 case had
   the same lossy "overwrite, don't add the addend" pattern that
   v0.2.22 fixed for R_PPC_ADDR32. Fixed in the same edit so both
   relocators handle non-zero REL32 addends correctly. (No tests
   currently exercise this case in the standard relocate path —
   the .o path leaves DWARF section addr=0 — but it's the right
   thing.)

## Verification

| Check | Result |
|---|---|
| Bootstrap fixpoint | tcc-self2.o == tcc-self3.o |
| `tests2` | 111/111 |
| `abitest-cc` / `abitest-tcc` / `libtest` / `dlltest` | all pass |
| Demos v0.2.32 .. v0.2.37 | all pass |
| New demo `v0.2.38-dwarf-exe.sh` | passes |
| `dwarfdump` on linked exe | reads compile unit, types, line table, aranges, strings |
| `AT_low_pc` for `sq` | matches runtime VA from `nm` (0x000015cc) |
| Linked exe still runs | `sq(5) = 25 (argc=1)` |

## Try it

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
echo 'int sq(int n){return n*n;} int main(void){return sq(5);}' > /tmp/d.c
./tcc/tcc -gdwarf-2 -B./tcc -L./tcc -I./tcc/include /tmp/d.c -o /tmp/dexe
dwarfdump /tmp/dexe       # compile unit, types, all readable
otool -l /tmp/dexe | grep -A 5 -i dwarf   # __DWARF segment, vmaddr=0
/tmp/dexe; echo $?        # 25
```

## Caveats

* **Tiger gdb 6.3 doesn't read embedded `__DWARF` segments.**
  Apple's gdb expects either OSO STAB entries (a debug-map
  pointing back at the .o files) OR a separate `.dSYM` bundle.
  Embedded DWARF in the exe is unusual for the Apple workflow
  but is what dwarfdump and lldb expect. To get gdb-on-Tiger to
  step-debug tcc binaries we'd need to either emit OSO STABs
  during linking or post-process into a .dSYM. Tracked as
  follow-up.

* **DWARF version: -gdwarf-2 recommended.** Tiger's dwarfdump
  fully understands DWARF-2 and partially DWARF-4 (warns
  "Unknown DW_FORM constant: 0x17" = `DW_FORM_sec_offset`).
  Empty `.debug_info contents:` with default DWARF-4; correct
  output with `-gdwarf-2`. Same as the v0.2.37 .o path.

## Open work for next session

The DWARF arc is functionally complete (information present and
parseable). The next-tier polish:

* **Per-prolog CFI** so lldb (and any DWARF unwinder) can
  reconstruct frames. The CIE has minimal initial state
  (CFA=r1+0); after `stwu r1, -frame_size(r1)` the actual CFA
  is `r1 + frame_size`. Real fix: emit per-FDE
  `DW_CFA_advance_loc` + `DW_CFA_def_cfa_offset` annotations
  describing the prolog's frame moves. Requires sleb128-encoded
  offsets and per-insn advance_loc bookkeeping keyed off
  ppc-gen.c's variable-length prolog backfill.

* **OSO STAB entries** so gdb-on-Tiger can find the .o files
  during link and pull DWARF from them on demand. Would also
  make `dsymutil` work as expected. Requires keeping `.o` paths
  in scope during linking.

* **Default DWARF version**: tcc emits DWARF-4 by default;
  Tiger's dwarfdump only partially understands it. Defaulting to
  DWARF-2 or 3 on PPC for tooling compatibility would make
  bare `-g` produce the most readable output. Workaround for
  now: `-gdwarf-2`.

## Other open items (carried forward)

* libtcc thread safety (OOS per CLAUDE.md).
* Real bcheck.c port — multi-session, unblocks 3 test stages.
* AltiVec intrinsics — multi-session, niche.
* Self-link diagnostics — small QoL.
* test3 structural curation — 13 lines of intentional divergence
  (Apple ABI alignof, `_Bool` size, UB cast).
