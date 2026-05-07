# Session 052 — DWARF debug info in `-c` Mach-O output

**Date:** 2026-05-07 (sixth release of the evening)
**Build host:** imacg3

## Arrival state

* HEAD `0e1eec9` — v0.2.36-g3.
* All previously-passing tests/demos still pass.
* `tcc -c -g foo.c` accepted `-g` but emitted no DWARF — the .o
  had no `__DWARF` segment. `dwarfdump foo.o` printed `<EMPTY>`.

## Goal

Make `tcc -c -g` produce a Mach-O .o with valid DWARF that
`dwarfdump` (Tiger's bundled binary, plus modern equivalents)
can parse.

DWARF in the linked exe path is a separate follow-up — addressed
under [HANDOFF.md](HANDOFF.md). The macho exe writer
(`macho_output_exe`) doesn't yet enumerate DWARF sections; this
session focuses on the .o path which goes through a simpler
section-classification gate that we can extend.

## Three pieces

### 1. tccdbg.c — PPC arch blocks

The DWARF emitter has five arch-dispatch sites; all five gained
PPC-aware blocks (or were widened to include PPC):

* `DWARF_MIN_INSTR_LEN`: 4 (PPC instructions are 4 bytes).
* CIE setup: code_alignment_factor=4, data_alignment_factor=-4,
  return_address_column=65 (LR's conventional DWARF column on
  PPC32), CFA defined as r1+0 (DWARF column 1 = SP).
* FDE relocation: R_PPC_REL32 (the standard ELF symbol-relative
  reloc; ppc-link.c gained a case for it — see #2).
* Per-FDE prolog frame moves: deliberately empty. Real per-prolog
  CFI would describe the stwu / stw r31 / addi r31 sequence with
  sleb128-encoded negative offsets and per-insn advance_loc
  bookkeeping — that requires understanding ppc-gen.c's
  variable-length prolog backfill (gfunc_epilog) and is deferred.
  lldb's PPC32 unwinder falls back to back-chain following when
  CFI is incomplete, which our prolog does preserve, so source-
  level debugging via `.debug_line` works without it.

Plus a target-endian write helpers block. PPC32 is big-endian and
DWARF integers follow target endianness (per DWARF spec), but
tccdbg.c was written assuming little-endian targets and uses
`write32le` throughout. Without converting these, the .debug_info
unit-length prefix (and other multibyte fields) showed up
little-endian in a big-endian Mach-O — dwarfdump on the BE file
read garbage. New macros `dwarf_target_write{16,32,64}` route
through `write*be` on PPC and `write*le` everywhere else; all
DWARF-section writes (in dwarf_data2/4/8 and the various
length-prefix back-fills) go through them.

### 2. ppc-link.c — R_PPC_REL32

DWARF FDE entries use a PC-relative 32-bit reloc to point at the
function's start address. Existing ppc-link.c handled R_PPC_REL24,
R_PPC_ADDR32, etc. but not R_PPC_REL32. Added the case:

```c
case R_PPC_REL32:
    ppc_link_write32be(ptr, (uint32_t)(val - addr));
    return;
```

### 3. ppc-macho.c — classify_section + .o layout

Two changes:

**classify_section** had an early-out for non-SHF_ALLOC sections,
which excluded all `.debug_*` (DWARF data isn't loaded at runtime
so it isn't allocated). Replaced that early-out with an explicit
DWARF block that maps each `.debug_<x>` to `__DWARF,__debug_<x>`
with `S_ATTR_DEBUG` (= 0x02000000). A finite-set hardcoded mapping
handles the truncations Mach-O's 16-byte sectname requires (e.g.,
`.debug_str_offsets` → `__debug_str_offs`).

**.o layout** — and this is the non-obvious bit — the exe-/object-
agnostic section layout loop assigned `addr = vmaddr` to every
section, including DWARF. After tcc applied R_PPC_ADDR32
relocations targeting DWARF sections (e.g., the abbrev_offset slot
at the start of each compile unit), the in-place value became
`section_pre_link_addr + addend`, not the addend alone. That looked
like garbage offsets to dwarfdump. Fix: skip DWARF sections in the
vmaddr-assigning step, leaving their `addr = 0`. R_PPC_ADDR32 then
resolves to "0 + addend = addend" — the correct .o convention,
where the addend lives in-place and the linker / dsymutil applies
the section base when stitching .o's into a final output.

## Verification

| Check | Pre | Post |
|---|---|---|
| `tcc -c -g`: .o has __DWARF segment | ❌ | ✅ |
| `dwarfdump`: reads compile unit | ❌ "EMPTY" | ✅ |
| `dwarfdump`: AT_producer string | ❌ | ✅ "tcc 0.9.28rc" |
| `dwarfdump`: TAG_subprogram entries | ❌ | ✅ "sq" / "main" |
| Tests2 | 111/111 | 111/111 |
| abitest-cc | 24/24 | 24/24 |
| abitest-tcc | 24/24 | 24/24 |
| libtest, dlltest | pass | pass |
| Bootstrap fixpoint | holds | holds |
| All real-world demos | pass | pass |

Released as **v0.2.37-g3**.

## Sample output

```text
$ ./tcc -c -gdwarf-2 foo.c -o foo.o
$ dwarfdump foo.o
.debug_info contents:

0x00000000: Compile Unit: length = 0x000009c7  version = 0x0002
              abbr_offset = 0x00000000  addr_size = 0x04

0x0000000b: TAG_compile_unit [1] *
             AT_producer( "tcc 0.9.28rc" )
             AT_language( DW_LANG_C99 )
             AT_name( "/tmp/foo.c" )
             AT_low_pc( 0x00000000 )
             AT_high_pc( 0x000001a0 )
             AT_stmt_list( 0x00000000 )
0x00000044:     TAG_subprogram [20] *
                 AT_external( 0x01 )
                 AT_name( "sq" )
                 ...
```

## Bug hit during the work

The biggest mis-step was forgetting that DWARF integers follow
target endianness. The .debug_info unit length showed up as
`a9 00 00 00` (LE) on a BE Mach-O, which Tiger's dwarfdump read
as length 0xa9000000 = 2.8 GB — abandoned the parse and printed
nothing. The "empty contents" symptom was easy to misdiagnose
as "tcc didn't actually emit anything" until I dumped the bytes
directly with `otool -s __DWARF __debug_info` and noticed the
endian flip. Dropping in `dwarf_target_write*` macros fixed it
in one pass.

The second thing was the in-place-value-vs-addend question for
.o relocations. Tcc's macho writer applies relocations during
.o write (in-place value gets section base added). For DWARF
that's wrong — dwarfdump reads the post-applied value as a stale
offset. The fix (DWARF sections keep addr=0) is small but
required understanding that the .o is supposed to leave addends
in-place for the linker.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents — this was tightly coupled with the work flow.

## Closing note

Sixth release of the evening, second of a two-session DWARF arc.
The exe path is the next session's work. With DWARF in .o files
working, a future session can:

1. Add DWARF section enumeration to `macho_output_exe`.
2. Apply DWARF relocations using runtime VAs (text section's
   real load address etc.).
3. Emit a __DWARF LC_SEGMENT load command in the exe header.
4. Validate with `lldb foo.exe` actually stepping through source.

Per-prolog CFI (so backtraces are unwound via DWARF rather than
back-chain heuristics) is also follow-up work. The minimal CFI
in the current CIE is "CFA = r1+0 throughout" which is wrong
after stwu but lldb's heuristics paper over it.
