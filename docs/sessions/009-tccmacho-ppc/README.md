# Session 009: TCC Mach-O PPC 32-bit Port — Design Documentation

## Overview

This directory contains the comprehensive design and analysis for porting `tcc/tccmacho.c` to support 32-bit PowerPC (Apple PPC) on Mac OS X 10.4 Tiger.

**Status:** Planning Phase (Session 009)  
**Target File:** `/Users/cell/claude/tcc-darwin8-ppc/tcc/tccmacho.c` (2,476 lines)  
**Current Support:** x86_64 and arm64 only  
**Goal Support:** 32-bit PowerPC (Mach-O big-endian)

## Documents

### findings.md (1,436 lines, ~50 KB)

**Complete surgical survey of every location in tccmacho.c that requires changes for PPC32 support.**

Includes:
- **22 detailed change locations** with line numbers, current code snippets, and exact PPC32 requirements
- **Difficulty estimates** (trivial/small/medium/large) for each change
- **Summary table** of all changes at a glance
- **Porting strategy recommendation** (monolithic file with #ifdefs)
- **5-phase implementation plan** with smoke tests and deliverables
- **Risk assessment** and mitigations
- **Detailed site map** for reference during implementation
- **Open questions & research needed** (7 critical items)
- **Files to create/modify** list

## Key Findings Summary

### Surgical Change Count
- **~25 discrete locations** in tccmacho.c
- **8 instruction-heavy #ifdef blocks** for PPC (stub generation, initialization, etc.)
- **~480 lines of new code** (struct definitions, typedefs, conditional blocks)

### High-Complexity Areas
1. **Sections 8.1–8.4** (lines 575–944): PPC instruction sequences for stubs and initialization
2. **Section 7** (line 1750–1768): LC_UNIXTHREAD instead of LC_MAIN for Tiger 10.4
3. **Section 14** (throughout): Endianness (big-endian for PPC)

### Critical Design Decisions
- **Use monolithic file** with per-arch #ifdefs (shared infrastructure, cleaner than splitting)
- **Per-arch typedefs** to reduce `_64` suffix duplication:
  ```c
  #ifdef TCC_TARGET_PPC
      typedef struct mach_header Mach_Header;
      typedef struct segment_command Segment_Command;
  #else
      typedef struct mach_header_64 Mach_Header;
      typedef struct segment_command_64 Segment_Command;
  #endif
  ```
- **Classic Mach-O binding mode** (REBASE/BINDING sections, not LC_DYLD_CHAINED_FIXUPS which is 10.15+)

## Implementation Roadmap

### Phase 1: Structural Foundation (1–2 sessions)
- Define types, constants, structs
- Allow code to compile for TCC_TARGET_PPC
- Smoke test: `tcc -c test.c -o test.o && otool -h test.o` shows PPC cputype

### Phase 2: Load Command & Entry Point (1 session)
- LC_UNIXTHREAD for PPC instead of LC_MAIN
- Omit LC_BUILD_VERSION (10.6+ only)
- Set srr0 in thread state for entry point
- Smoke test: `otool -h test.o` shows correct magic, CPU type, load commands

### Phase 3: Basic Relocation & Symbols (1 session)
- Symbol table size handling (nlist_64 vs nlist)
- FAT binary parsing for PPC
- Simple programs without function calls
- Smoke test: `ld test.o -o test` and run on G3

### Phase 4: Stub Generation & Dynamic Linking (1–2 sessions)
- PPC stub sequences (lis/ori/lwz/mtctr/bctr)
- Stub helper for dyld_stub_binder
- __GLOBAL_init with PPC calling convention
- Smoke test: `printf("Hello")` works on G3

### Phase 5: Verification & Cleanup (1 session)
- Test on imacg3 hardware (Tiger G3)
- Compare Mach-O structure with gcc-4.0
- Debug relocation/symbol issues
- Smoke test: Full test suite passes

## Open Research Questions

Before implementation, these items need clarification:

1. **PPC instruction sequences** — Validate lis/ori/lwz/mtctr/bctr against ABI docs
2. **Tiger load command set** — What's required vs. optional on 10.4?
3. **PPC thread state** — Exact `ppc_thread_state` structure for LC_UNIXTHREAD
4. **__dyld_private on PPC** — Address/symbol for PPC 10.4 dyld
5. **FAT binary endianness** — Verify handling of big-endian FAT headers
6. **ELF→Mach-O relocation mapping** — Confirm PPC ELF types are sufficient
7. **CONFIG_NEW_MACHO compatibility** — Tiger support for LC_DYLD_INFO_ONLY vs chained fixups

**Resources:**
- `/Users/cell/datasheets/books/prog/_powerpc/` — Apple PPC ABI & instruction guides
- `https://leopard-adc.pepas.com/` — Leopard ADC docs (Tiger-compatible features)
- `~/tmp/tcc-survey/mob/` — Original tinycc mob branch
- `ssh imacg3 gcc-4.0` — Real Tiger PPC compiler for validation

## Quick Reference

**Critical File Locations:**
- `tccmacho.c`: Lines 34–36 (guard), 49–58 (CPU types), 78–151 (structs), 575–944 (stubs), 1750–1768 (load commands), 1964–1971 (header init), 2376–2402 (DLL loading)

**Struct Size Differences (PPC32 vs x86_64):**
- `mach_header`: 28 bytes vs `mach_header_64`: 32 bytes
- `segment_command`: 48 bytes vs `segment_command_64`: 72 bytes
- `section`: 48 bytes vs `section_64`: 80 bytes
- `nlist`: 12 bytes vs `nlist_64`: 16 bytes

**Constants to Add:**
- `CPU_TYPE_POWERPC = 18`
- `CPU_SUBTYPE_POWERPC_ALL = 0`
- `LC_SEGMENT = 0x1`
- `LC_UNIXTHREAD = 0x5`

**Endianness Note:**
- x86_64, arm64: little-endian (`write32le`)
- PPC: big-endian (define `write32be` helper, use for PPC)

## Session Notes

**Session 009 Focus:** Design and planning only. No implementation yet.

**Next Session Goals:**
- Implement Phase 1 (structural foundation)
- Get code compiling for TCC_TARGET_PPC
- Validate struct definitions with `sizeof()` checks

---

**Document Generated:** 2026-04-30  
**Author:** Claude Code (Haiku 4.5)  
**Scope:** Complete surgical survey for porting guide

For implementation guidance, refer to `findings.md` sections in order.
