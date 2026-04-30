# Session 003 — backend scaffold findings

## Goal achieved

`make` succeeds on imacg3, producing a working `tcc` binary that
runs, prints `(PowerPC Darwin)` for `-v`, preprocesses C correctly
(all PPC predefines visible to user code), and hits the stub
`gfunc_prolog` error the moment it tries to actually generate code.
That's the empty canvas — exactly what we wanted.

## What was done

### Source files added
- [`tcc/ppc-gen.c`](../../../tcc/ppc-gen.c) — full scaffold of the
  PPC code generator. `TARGET_DEFS_ONLY` block has real register
  layout (10 GPRs r3-r12, 8 FPRs f1-f8, NB_REGS=18), real ABI macros
  (PTR_SIZE=4, LDOUBLE_SIZE=8, etc.). Function bodies are stubs that
  call `tcc_error()` with a self-identifying message.
- [`tcc/ppc-link.c`](../../../tcc/ppc-link.c) — relocation handler
  with PPC ELF reloc constants (R_PPC_ADDR32, R_PPC_REL24, etc.).
  `code_reloc()` and `gotplt_entry_type()` have real classification
  logic; `relocate()` aborts with `tcc_error_noabort()`.

### Wiring changes (4 files)
- `tcc/configure` — added `TCC_TARGET_PPC` to the target-required
  assertion and added `CONFIG_ppc=yes ⇒ TCC_TARGET_PPC 1` mapping.
- `tcc/Makefile` — added `DEF-ppc`, `DEF-ppc-osx`, `ppc_FILES`,
  `ppc-osx_FILES`, and added `ppc-osx` to the `TCC_X` cross-target
  list.
- `tcc/tcc.h` — added `#elif defined TCC_TARGET_PPC` to the
  per-target include block (so ppc-gen.c gets included with
  `TARGET_DEFS_ONLY` defined). Also extended the **target-fallback
  chain** at line 154 (see blocker #1 below).
- `tcc/tcc.c` — added `"PowerPC"` to the version-string formatter.

## Blockers found and fixed

### Blocker #1 — target-fallback chain doesn't know PPC

`tcc.h:154` has a fallback that defaults to `TCC_TARGET_I386` when
no `TCC_TARGET_*` macro is set. The condition listed every target
*except* PPC:

```c
#if !defined(TCC_TARGET_I386) && !defined(TCC_TARGET_ARM) && \
    !defined(TCC_TARGET_ARM64) && !defined(TCC_TARGET_C67) && \
    !defined(TCC_TARGET_X86_64) && !defined(TCC_TARGET_RISCV64)
```

config.h *does* define `TCC_TARGET_PPC`, but config.h's body is
gated by `#if !(any of the above)`. None were initially defined, so
config.h's body would run and set `TCC_TARGET_PPC=1`. **Then** the
fallback at line 154 ran. Its check missed PPC, fired, and the
inner `#elif __x86_64__ ... __arm__ ... __riscv` chain also missed
PPC, defaulting to `#define TCC_TARGET_I386`. Both PPC and i386 ended
up defined; the per-target `#ifdef` chain at line 374 picked I386
first.

Symptom: `extern const int reg_classes[5]` in the preprocessed
output — i386's NB_REGS, not ours. Diagnosed via
`gcc -E -dM ppc-gen.c` showing `#define TCC_TARGET_I386` (empty)
right next to `#define TCC_TARGET_PPC 1`.

**Fix:** added `&& !defined(TCC_TARGET_PPC)` to the outer condition,
and a `#elif defined __powerpc__ || defined __ppc__ || defined
__POWERPC__` branch in the inner.

This is a real upstream bug that wouldn't bite mob today (no PPC
target there), but it bit us immediately.

### Blocker #2 — Tiger's GNU make 3.80 lacks `$(or ...)`

The Makefile uses `T = $(or $(CROSS_TARGET),$(NATIVE_TARGET),unknown)`,
which is GNU Make 3.81+. Tiger's system `make` is 3.80 (2002).
Result: T resolves to nothing, `$($T_FILES)` looks up `$(_FILES)`
(empty), `LIBTCC_OBJ` is empty, `ar rcs libtcc.a` fails with "no
archive members specified."

**Fix (workaround):** use `/opt/make-4.3/bin/make` (already in our
fleet's `/opt`). Long-term we may want to make this transparent —
either rewrite the Makefile line to be 3.80-compatible (pure
substitution chain, no `$(or)`), or document `gmake` as a build
prerequisite. Filed for later session.

### Blocker #3 — tccmacho.c hardcoded to x86_64/arm64

`tccmacho.c:34-36`:
```c
#if !defined TCC_TARGET_X86_64 && !defined TCC_TARGET_ARM64
#error Platform not supported
#endif
```

Plus a bunch of `r_addend` references that assume `Elf*_Rela`. PPC
uses `Elf32_Rel` (no addend).

**Fix (workaround for 003):** removed `tccmacho.c` from
`ppc-osx_FILES`. The build links cleanly because the linker is
invoked with `-undefined warning -flat_namespace`, so the missing
`_macho_load_dll`, `_macho_load_tbd`, `_macho_output_file` are just
warnings. The resulting `tcc` can preprocess but can't emit
Mach-O. That's fine for session 003.

**Real fix lands in session 020 (roadmap phase 3)** — extend the
platform check to include PPC, audit r_addend usage and rewrite as
needed for PPC32, add CPU type/subtype constants for ppc, add the
PPC reloc enumerators, add load command variants for 10.4.

### Blocker #4 — `tcc_error()` macro requires `USING_GLOBALS`

ppc-link.c was using `tcc_error("...")`. The macro maps to
`use_tcc_error_noabort` (an undefined symbol) when `USING_GLOBALS`
is not defined. Convention across all `*-link.c` files: use
`tcc_error_noabort()` instead, which doesn't need `USING_GLOBALS`.

**Fix:** changed every `tcc_error(...)` in ppc-link.c to
`tcc_error_noabort(...)`. Matches arm/arm64 style.

### Blocker #5 — `func_bound_add_epilog` referenced unconditionally

tccgen.c references `func_bound_add_epilog` at line 1702 *outside*
any `#ifdef CONFIG_TCC_BCHECK`. The declaration in tcc.h:1527 *is*
inside `#ifdef CONFIG_TCC_BCHECK`, but the use site isn't.

When we don't enable BCHECK, the symbol is referenced but never
defined → link warning becomes runtime "Symbol not found" via
`-flat_namespace`.

**Fix:** in `ppc-gen.c`, define `ST_DATA int func_bound_add_epilog`
unconditionally. Matches what i386-gen.c and x86_64-gen.c do (they
also define it always; only the bcheck-internal `func_bound_offset`
and `func_bound_ind` are gated).

This is also an upstream bug — declaration is gated, use site
isn't. Would fix in tccgen.c if upstreaming, but not blocking us.

## Decisions captured

- **PPC32 register layout (TCC's view):** 10 int slots (TREG_R(0..9)
  → r3..r12) plus 8 float slots (TREG_F(0..7) → f1..f8). NB_REGS=18.
  Non-volatile r13-r31 / f14-f31 not exposed in v1 — adds spill
  flexibility later. Documented in [`ppc-gen.c`](../../../tcc/ppc-gen.c).
- **Apple PPC long double = double (8 bytes), not "double-double"
  (16 bytes).** v1 simplification. Real Apple ABI uses double-double;
  we'll add when we tackle real-world FP testing.
- **PROMOTE_RET set.** Caller is responsible for sign/zero-extending
  small int return values. PPC ABI permits either side; we mirror
  what gcc-4.0 on Darwin does.

## Follow-ups not done in 003

1. Replace `$(or ...)` in Makefile so system make 3.80 works (low
   priority; `gmake` workaround is fine for now).
2. Re-add tccmacho.c to ppc-osx_FILES with PPC support (session 020).
3. Set up scripts/build-tiger.sh wrapper to capture the
   `--config-semlock=no` + `gmake` requirements in one place.
4. tcc -E -dM doesn't show `__POWERPC__` and `__ppc__` even though
   they're in `target_machine_defs`. Investigate whether the
   string-list iteration drops them, or our dM filter just missed
   them.

## Test record

```
$ ./tcc -v
tcc version 0.9.28rc (PowerPC Darwin)

$ ./tcc -E /tmp/zero.c            # works
$ ./tcc -E -dM /tmp/zero.c | grep PPC
#define __PPC__ 1
#define _ARCH_PPC 1                # plus __powerpc__, __APPLE__, __MACH__,
                                   #      __BIG_ENDIAN__

$ ./tcc -c /tmp/zero.c -o /tmp/zero.o
/tmp/zero.c:1: error: ppc-gen: gfunc_prolog stub
```

That last one is success: codegen invokes the first ABI hook
(`gfunc_prolog`) and our stub fires with a self-identifying message.
This is the right baseline for session 005's prologue/epilogue work.
