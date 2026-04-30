# TCC Source Survey: Comparison for Mac OS X 10.4 Tiger PowerPC Backend

## Executive Summary

Of the three source trees examined, the **mob branch (current development tip) is the strongest base** for a Mac OS X 10.4 Tiger PowerPC backend implementation. While none of the trees have any PowerPC-specific code generation, mob offers two critical advantages: (1) a mature Mach-O backend (2,476 lines in `tccmacho.c`, currently supporting x86_64 and arm64 only) that abstracts away the binary format complexity, and (2) the most modern and modular architecture infrastructure, having added RISC-V support between v0927 and mob. The Mach-O support was introduced between bellard (2009) and v0927 (2017), refined into a dedicated module in mob. Starting from mob and retrofitting PPC support into the Mach-O backend is significantly less work than forking from v0927 and building Mach-O support from scratch. Bellard's release (Feb 2009) is too old; it lacks both Mach-O support and the ARM64 infrastructure that provides the most directly transferable patterns.

---

## 1. PPC Backend State

**Finding: No PowerPC backend exists in any of the three trees.**

All three source trees are completely devoid of PowerPC code generation files. Searches for `*ppc*`, `*powerpc*` files returned nothing.

- **Bellard (0.9.25, Feb 2009):** No PPC generator. Has: `i386-gen.c`, `x86_64-gen.c`, `arm-gen.c`, `c67-gen.c`, `il-gen.c` (intermediate language).
- **v0927 (0.9.27, Dec 2017):** No PPC generator. Adds `arm64-gen.c`, `arm-link.c`, and full per-architecture linker/assembler modules.
- **mob (current, Apr 2026):** No PPC generator. Adds `riscv64-gen.c`, `riscv64-asm.c`, `riscv64-link.c` between v0927 and mob.

**ELF/COFF headers only:** The elf.h file in all three trees contains extensive PowerPC relocation definitions (`R_PPC_*`, `R_PPC64_*`) and machine type constants, as does coff.h (for Windows PE format). These are **metadata only**—no code generator or linker backend tied to them. The headers are static reference material.

**Architecture evolution snapshot:**

| Metric | bellard | v0927 | mob |
|--------|---------|-------|-----|
| Total backend code (*.c files) | 8,613 lines | 13,754 lines | 22,469 lines |
| Backends | i386, x86_64, ARM, C67, IL | + arm64 | + riscv64 |
| Linker/ASM modularity | Mixed in tccelf.c | Separate *-link.c per arch | Separate *-link.c, *-asm.c, *-tok.h per arch |

---

## 2. Mach-O / Darwin Support

**Finding: Mach-O support exists only in v0927 and mob, and is robust only in mob.**

### Bellard (0.9.25)
- **Zero Darwin/macOS support.** No references to `TCC_TARGET_MACHO`, `__APPLE__`, or Mach-O structures.
- The configure script detects OS but does not define `CONFIG_OSX`.
- The Makefile hardcodes ELF + PE targets; no macOS-specific build rules.

### v0927 (0.9.27)
- **Partial/embedded Mach-O support.** Found references in:
  - `tcc.c`: Conditional `TCC_TARGET_MACHO` output, Darwin banner string.
  - `libtcc.c`: Dylib loading logic, `.dylib` file extension handling.
  - `tccrun.c`: Apple-specific mmap/code execution handling.
- **No dedicated module:** Mach-O logic is scattered across `tccelf.c` and `libtcc.c`; no standalone handler.
- **Architecture limitation:** While the code defines structures and logic, it's not clear which architectures were actually supported. The relocation and symbol table handling appears generic but untested on 32-bit PowerPC.

### mob (current)
- **Full, dedicated Mach-O backend.** A standalone `tccmacho.c` module (2,476 lines) handles all Mach-O binary generation.
- **Architecture support: x86_64 and arm64 ONLY.** Hard constraint at line 34-36:
  ```c
  #if !defined TCC_TARGET_X86_64 && !defined TCC_TARGET_ARM64
  #error Platform not supported
  #endif
  ```
- **CPU types defined:** Lines 55-58 list only:
  - `CPU_TYPE_X86_64` (mapped from `CPU_TYPE_X86 | CPU_ARCH_ABI64`)
  - `CPU_TYPE_ARM64` (mapped from `CPU_TYPE_ARM | CPU_ARCH_ABI64`)
- **No PowerPC constants:** Searches found no `CPU_TYPE_PPC`, `CPU_TYPE_PPC64`, or equivalent in tccmacho.c.
- **macOS deployment configuration:** The Makefile sets `MACOSX_DEPLOYMENT_TARGET := 10.6` for all OSX builds, and supports both 64-bit architectures' dylib versioning.
- **Modern dylib/TBD handling:** The mob branch's libtcc.c includes support for Apple's .tbd (text-based dylib stubs) format, introduced in Xcode 7 (2015+). This is critical for linking against system frameworks on modern macOS but irrelevant for Tiger (10.4).

**Key implication:** mob's Mach-O backend is a **proven, working foundation** for binary format handling. PowerPC support would require:
1. Adding `CPU_TYPE_PPC` (0x00000007) and `CPU_TYPE_PPC64` (0x00000007 | 0x01000000) constants.
2. Extending the switch statement in `macho_output_file()` to recognize `TCC_TARGET_PPC` and route to PPC relocation/symbol handling.
3. Implementing PPC-specific relocation logic (likely 200–400 lines for 32-bit PPC).

---

## 3. Architecture Additions Since Bellard

**Between bellard (Feb 2009) and v0927 (Dec 2017):**
- **New backend: ARM64** (64-bit ARM, AArch64).
  - Files: `arm64-gen.c` (1,837 lines), `arm64-link.c` (256 lines), supporting library code.
  - This was a major addition, as no 64-bit ARM existed in bellard.
  - Introduced separate `*-link.c` modules, moving architecture-specific linking out of the monolithic tccelf.c.

**Between v0927 (Dec 2017) and mob (Apr 2026):**
- **New backend: RISC-V 64** (RISC-V, 64-bit).
  - Files: `riscv64-gen.c` (1,434 lines), `riscv64-asm.c` (2,628 lines), `riscv64-link.c` (419 lines), `riscv64-tok.h`.
  - Demonstrates the **current "template" for adding a new backend:** dedicated generator, assembler, linker, and token/instruction definition header.
  - ARM64 also gained an `arm64-asm.c` (94 lines) module in mob, and both ARM and ARM64 gained `*-tok.h` files (instruction definitions).

**Pattern for new backends:** The progression shows that TCC's architecture now expects:
- `<arch>-gen.c` — code generation (lvalue, rvalue, operators, function calls).
- `<arch>-link.c` — relocations, symbol binding, executable/shared library layout.
- `<arch>-asm.c` — inline assembly support (increasingly modularized; historically in `-gen.c`).
- `<arch>-tok.h` — architecture-specific instruction/constraint definitions (new in mob).

**Implication for PPC:** A complete PPC backend following mob's conventions would require all four components. The RISC-V backend (added since v0927) is the best precedent: both are RISC, similar register widths, and RISC-V's addition is recent enough to show current best practices.

---

## 4. Build System

### Bellard (0.9.25)
- **Autoconf-style configure** (382 lines), generates `config.mak` and `config.h`.
- **Simple Makefile:** Hardcoded architecture detection; conditional compilation via `ARCH=` variable (i386, x86-64, arm).
- **No macOS support:** configure does not define `CONFIG_OSX`; no darwin-specific flags.
- **Cross-compilation:** Basic, via NATIVE_TARGET defines; not tested for modern cross-toolchains.

### v0927 (0.9.27)
- **Enhanced configure** (527 lines). Adds detection for multiple OSes (WIN32, BSD variants).
- **Improved Makefile** (13 KB). Still architecture-driven but more granular per-target rules.
- **Mach-O mentions:** Emerging but minimal. configure does not yet set `CONFIG_OSX` as a first-class option.

### mob (current)
- **Mature configure** (763 lines, 22 KB source). Explicit `--targetos=` flag; detects `Darwin` and sets `CONFIG_OSX`.
- **Sophisticated Makefile** (18 KB). Full target matrix:
  - `TCC_X = i386 x86_64 i386-win32 x86_64-win32 x86_64-osx arm arm64 arm-wince c67 riscv64 arm64-osx`
  - Each target has `DEF-<target>` defines; macOS targets append `-DTCC_TARGET_MACHO`.
  - Support for both static and dynamic libtcc builds; dylib versioning on macOS.
- **Modern cross-compilation:** configure now probes for GCC/Clang versions, supports `--sysroot`, detects deployment target.
- **macOS-specific:** 
  - Lines 51–75 of Makefile handle macOS rpath, dylib versioning, and set `MACOSX_DEPLOYMENT_TARGET := 10.6`.
  - Recent commit (Apr 28, 2026) shows active macOS development.

**Implication for Tiger/PPC development:**
- The `10.6` deployment target in mob's Makefile must be **overridden** to `10.4` for our PPC work.
- Tiger (10.4) lacks dyld_info, uses classic symbol tables (which mob already generates). This is **supported by the current code**.
- Cross-compilation from modern macOS → Tiger PPC is feasible with the mob infrastructure, but requires explicit target tuning (e.g., `--targetos=Darwin --target=powerpc-apple-darwin8`).
- Native compilation on Tiger itself would be possible: gcc 4.0.x (stock) or gcc 4.9/10.3 (via /opt) can build C89 code, though the modern Makefile assumes POSIX tools not present on Tiger. A Tiger-native build would need the bash 3.2, perl 5.36, and modern gcc in /opt.

---

## 5. Licensing

### Summary
The licensing situation is **clean and favorable for derived work.** The project has successfully relicensed to MIT-compatible terms for most contributions.

### Bellard (0.9.25)
- **COPYING file:** GNU Lesser General Public License (LGPL) v2.1.
- **All source files:** Carry LGPL v2.1 headers.
- **No relicensing:** This is the original GPLv2-era code under Fabrice Bellard's initial license.

### v0927 (0.9.27)
- **COPYING file:** LGPL v2.1 (unchanged).
- **RELICENSING file:** A landmark file introduced. Lists 20 contributors who have agreed to relicense their work under an **MIT-style license** (permissive, commercial-friendly). Notable holdouts:
  - Daniel Glöckner: NO for arm-gen.c (YES for other contributions).
  - Timo VJ Lähde & TK: Unknown ("?") status.
- **Per-file impact:** Most backends (i386, x86_64, ARM64, RISC-V) are relicensed or authored by YES signatories. **Only arm-gen.c (the original ARM32 backend by Glöckner) remains LGPL without relicensing consent.**

### mob (current, Apr 2026)
- **COPYING file:** LGPL v2.1 (original, not changed).
- **RELICENSING file:** Updated. Now includes **27 contributors** (7 additional since v0927). Recent additions:
  - Danny Milosavljevic (YES, arm-asm.c, riscv64-asm.c).
  - Herman ten Brugge, Steffen Nurpmeso, Tyge Løvset, Reimar Döffinger, others (all YES).
- **Holdouts remain:**
  - Daniel Glöckner: NO for arm-gen.c (unchanged from v0927).
  - Timo VJ Lähde & TK: Still "?" for tiny_libmaker.c, tcccoff.c, c67-gen.c.

### License Impact for PPC Work
- **New code we write is ours.** If we contribute back to mob, we can specify our license preference (MIT preferred).
- **Derivative code from arm-gen.c:** arm-gen.c is LGPL-only (Glöckner's work, no relicense consent). If we fork arm-gen.c to create ppc-gen.c, **our derivative must also be LGPL v2.1**, and derived works must preserve the LGPL notice.
- **Everything else (core, tccmacho.c, arm64-*, riscv64-*, libtcc.c):** Licensed under MIT-compatible terms (per RELICENSING). Derivatives can be relicensed to MIT.
- **Publishing:** A Tiger/PPC port using mob as the base can be published under MIT. If we must use arm-gen.c as a template (likely), any resulting ppc-gen.c is bound by LGPL v2.1—which is permissive enough, but must be clearly marked.

**Recommendation:** Base the PPC generator on arm64-gen.c (Edmund Grimley Evans, MIT-relicensed) rather than arm-gen.c, if architecturally feasible. ARM64 is more similar to modern PPC (64-bit, fixed-width instructions) than ARM32.

---

## 6. Prior Darwin/PPC Attempts

### Git History Search
- **mob branch:** No commits with "darwin", "macho", "apple", "ppc", or "powerpc" in the log (searched --all, --grep, -S, and file name patterns).
- **v0927:** Not a git repo (distributed tarball only).
- **bellard:** Not a git repo (distributed tarball only).

**Inference:** The mob branch's tccmacho.c was added as part of a larger modernization effort (likely around 2020–2022, based on the relicensing activity timeline), without any dedicated PPC/Darwin work. The module was built for x86_64 and later extended for arm64, but no PowerPC effort was made.

### Mailing List & Web Presence

From the TinyCC mailing list (tinycc-devel@nongnu.org):
- **2021 (Re: "Can tcc compile itself with Apple M1?"):** Discussion of arm64 support on macOS. At that time:
  - x86_64 Mach-O backend was stable.
  - arm64 support was considered "not too far away" but the infrastructure existed (arm64-gen.c had been written).
  - Relocation errors were blocking arm64 native compilation.
- **2021 (Re: "Linking system dylibs on macOS 11+"):** Discussions about dylib linking and .tbd format support, indicating active work on macOS cross-compilation in that period.

**No PowerPC discussion found** in public archives. PowerPC was dropped from macOS at 10.5 (Leopard, 2007) and from Apple's development focus in 2009 when Intel transitioned was complete. TCC's subsequent development (2010–2026) focused on ARM (mobile) and x86_64, with no recorded attempt at PPC.

---

## 7. Key Files Reference

### Build & Configuration
- `~/tmp/tcc-survey/mob/configure` — Main build configuration script (763 lines).
- `~/tmp/tcc-survey/mob/Makefile` — Full build rules (18 KB); defines DEF-x86_64-osx, DEF-arm64-osx, TCC_X target matrix.
- `~/tmp/tcc-survey/mob/config.h.in` — Config template (auto-generated by configure).

### Mach-O Support (mob only)
- `~/tmp/tcc-survey/mob/tccmacho.c` — Complete Mach-O binary generator (2,476 lines). **This is the key file for PPC porting.**
  - Lines 34–36: Hardcoded x86_64/arm64 check.
  - Lines 55–58: CPU_TYPE_* constants (no PPC).
  - Lines 1966–1969: CPU type assignment logic in `macho_output_file()`.
  - Lines 2377–2380: FAT binary (universal) handling; only checks x86_64 and arm64.

### Architecture-Specific Code
- `arm64-gen.c` (across all three trees): 1,837–2,209 lines. **Best reference for similar 64-bit RISC backend structure.**
- `arm64-link.c` (mob): 322 lines. Relocation handling; PPC equivalent would be ~200–300 lines.
- `riscv64-gen.c` (mob only): 1,434 lines. **RISC-V is the newest backend; best modern example of "how to add a backend."**
- `arm-gen.c` (all three trees): Contains LGPL-only code (Glöckner's original). Avoid copying if possible.

### Library & Runtime
- `~/tmp/tcc-survey/mob/libtcc.c` — Core compiler library interface. Contains Mach-O loading logic (dylib, .tbd).
- `~/tmp/tcc-survey/mob/tccrun.c` — JIT and runtime handling; has Apple-specific code execution paths.
- `~/tmp/tcc-survey/mob/tccpp.c` — Preprocessor; defines `__APPLE__` macro for target OS detection.

### Licensing
- `~/tmp/tcc-survey/mob/RELICENSING` — MIT relicensing registry; critical for determining which files can be MIT-licensed derivatives.
- `~/tmp/tcc-survey/mob/COPYING` — LGPL v2.1 (original license, still applies to unrelicensed code).

---

## Open Questions

1. **PPC instruction encoding & ABI specifics:** What is the exact ABI for 32-bit PPC on Tiger (10.4)? Does it follow the System V PPC ABI, or are there macOS-specific deviations (e.g., stack alignment, TOC handling)?
   - Reference: Apple's Xcode 2.5 headers in /Developer/SDKs/MacOSX10.4u.sdk.
   
2. **Mach-O load commands for PPC:** Tiger's dyld supports classic symbol tables and LC_SEGMENT commands. Does tccmacho.c's current LC_MAIN handling work for PPC, or must we fall back to LC_UNIXTHREAD or traditional LC_MAIN with 32-bit semantics?

3. **Floating-point conventions:** PPC has soft-float, hard-float (FPU), and FPSCR conventions. Did Tiger mandate a specific FP ABI? How should we configure tcc's -DTCC_ARM_VFP equivalent for PPC?

4. **Relocation types:** tccmacho.c currently handles only x86_64 and arm64 relocations. We need a PPC-specific relocation handler. Should we reference ELF PPC relocation types (already defined in elf.h), or consult Apple's legacy Mach-O documentation?

5. **Symbol table format:** Classic Mach-O symbol tables (used by tccmacho.c for x86_64/arm64) are text-based and less efficient than dyld_info. Are we confident Tiger's dyld can link and execute binaries with only classic symbol tables and no DYLD_INFO?

6. **Cross-compilation toolchain:** For building on modern macOS targeting Tiger/PPC, do we need to:
   - Use Xcode 2.5's gcc-4.0.1 (if available), or can we use a modern cross-gcc (e.g., via Homebrew powerpc-unknown-linux-gnu)?
   - Handle endianness differences (PPC is big-endian; modern x86/ARM are little-endian)?

7. **Testing target:** Once we have a ppc-darwin8-tcc compiler, where will it run? On an actual imacg3 running Tiger, or under a PPC emulator? Does tccrun (JIT) need special handling for Tiger's XNU kernel?

8. **Feature parity:** Should the PPC backend support inline assembly (arm-asm.c style)? What about SIMD (Altivec/VMX)? These are nice-to-haves but out of scope for a minimal v1.

---

## Recommendation Summary

**Start with mob. Key steps:**

1. **Extend tccmacho.c:** Add `CPU_TYPE_PPC` (0x7) and `CPU_TYPE_PPC64` (0x1000007) constants. Extend `macho_output_file()` to branch on `TCC_TARGET_PPC` and invoke a new `gen_ppc_macho()` routine.

2. **Create ppc-gen.c:** Base on arm64-gen.c's structure (64-bit RISC patterns are closer to PPC than ARM32's thumb/conditional codegen). Expect ~2,000–2,500 lines.

3. **Create ppc-link.c:** Relocation handler (200–300 lines), likely emulating PPC ELF relocations for Mach-O output.

4. **Update configure & Makefile:** Add `--target=powerpc-apple-darwin8` support, define `TCC_TARGET_PPC` when appropriate, add ppc-darwin8 to the `TCC_X` matrix.

5. **Licensing:** Your ppc-gen.c and ppc-link.c can be MIT-licensed (our contributions). Document this clearly in file headers.

6. **Testing:** Once compiled, test on imacg3 or a PPC emulator running Tiger. Validate with simple C programs (hello world, recursion, struct handling, etc.) before tackling libtcc1.a and library runtime support.

The mob branch has the maturity, modularity, and licensing clarity needed. Bellard is too old, and v0927 sits uncomfortably between "Mach-O support scattered in tccelf.c" and "a clean dedicated module." Mob is the clear winner.

