# 003 — Backend scaffold

Get the build to complete on imacg3 with a stubbed PPC backend.
Empty canvas: `tcc` runs, preprocesses, exits cleanly with a
self-identifying error the moment it tries to generate code.

## Arrival state

After 002, the source was in-tree, configure detected the host
correctly, but `make` died at `ar rcs libtcc.a` because no
`ppc_FILES` Makefile variable was defined and no `ppc-gen.c` /
`ppc-link.c` / `ppc-tok.h` source files existed.

## What was done

1. Studied the per-arch contract:
   - `arm64-gen.c` (license-clean reference) for full export list.
   - `i386-gen.c` (32-bit reference) for what's *not* needed in
     32-bit backends — `gen_opl` and `gen_cvt_sxtw` are handled by
     tccgen.c when `PTR_SIZE == 4`.
   - `arm64-link.c` for relocation / GOT / PLT API shape.

2. Wrote stubbed source:
   - [`tcc/ppc-gen.c`](../../../tcc/ppc-gen.c) — real register layout
     (10 GPRs r3-r12 + 8 FPRs f1-f8 = NB_REGS 18), real ABI macros
     (PTR_SIZE=4, big-endian instruction emitter `o()`), all required
     codegen / load / store / control-flow functions stubbed with
     `tcc_error()` calls that name themselves.
   - [`tcc/ppc-link.c`](../../../tcc/ppc-link.c) — PPC ELF reloc
     constants, real `code_reloc` / `gotplt_entry_type` classification,
     stubbed `relocate()`.

3. Wired into the build:
   - `tcc/configure` — added `TCC_TARGET_PPC` to required-target
     assertion + the `CONFIG_ppc=yes` mapping.
   - `tcc/Makefile` — `DEF-ppc`, `DEF-ppc-osx`, `ppc_FILES`,
     `ppc-osx_FILES`, added `ppc-osx` to `TCC_X`.
   - `tcc/tcc.h` — added the `#elif defined TCC_TARGET_PPC` per-arch
     include, plus extended the `#if !defined(TCC_TARGET_*)` fallback
     chain at line 154 (root cause of blocker #1).
   - `tcc/tcc.c` — added `"PowerPC"` to the version string.

4. Five blockers diagnosed and fixed; details in
   [findings.md](findings.md). The most interesting two:
   - tcc.h's target-fallback chain *didn't list TCC_TARGET_PPC*, so
     PPC builds silently fell through to the `#define
     TCC_TARGET_I386` default, and i386-gen.c got included instead
     of ppc-gen.c.
   - Tiger's GNU make 3.80 doesn't have `$(or ...)` — Makefile uses
     it, build needs `/opt/make-4.3/bin/make`. Workaround for now.

## Decisions

- **PPC32 register exposure:** 10 GPRs + 8 FPRs in v1. Non-volatile
  registers (r13-r31, f14-f31) added later if/when we need spill
  flexibility.
- **Apple long double = double (8 bytes) for v1.** "Double-double"
  (16-byte) ABI compat is post-v1 work.
- **`tccmacho.c` excluded from build for now.** It hardcodes
  x86_64/arm64 and uses `Rela r_addend` which doesn't exist on PPC.
  Re-adding it is a roadmap phase 3 task (session ~020). The build
  links with `-undefined warning -flat_namespace` so missing
  `_macho_*` symbols are warnings, not errors. tcc can preprocess
  but can't emit object files yet — fine for this session.
- **`/opt/make-4.3/bin/make` is the Tiger build prerequisite** until
  we rewrite the Makefile's `$(or ...)` use.

## Exit state

- `make` succeeds on imacg3 producing a 32-bit PPC Mach-O `tcc`
  binary that runs.
- `./tcc -v` → `tcc version 0.9.28rc (PowerPC Darwin)`.
- `./tcc -E hello.c` → preprocesses correctly, all PPC predefines
  visible.
- `./tcc -c hello.c -o hello.o` → exits 1 with
  `ppc-gen: gfunc_prolog stub`. **Exactly the empty canvas.**

## Next session

Session 004 — first real codegen. Implement `gfunc_prolog`,
`gfunc_epilog`, and `gfunc_return` so `int main(void) { return 0; }`
compiles to a real, runnable PPC function. Then implement enough of
`tccmacho.c` (or an alternative output path) that the resulting
`.o` can actually be written and linked. That gives us our first
runnable end-to-end output.
