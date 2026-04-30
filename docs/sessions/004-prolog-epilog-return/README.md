# 004 — Prologue, epilogue, return constant

First real PPC instructions emitted from our backend. Goal: tcc
compiles `int main(void) { return N; }` to a correct, hand-verifiable
sequence of 32-bit big-endian PowerPC instructions.

## Arrival state

After 003, the backend was stubbed: `tcc` could preprocess but
aborted with `gfunc_prolog stub` on first codegen. No real PPC
instructions emitted yet.

## What was done

Three real implementations in `ppc-gen.c`, plus one helper:

1. **`gfunc_prolog`** — reserves `PPC_PROLOG_SIZE` (12 bytes) of
   space at function start, records `func_sub_sp_offset` for
   backfill. Pattern lifted from `i386-gen.c`: emit nothing yet,
   wait for `gfunc_epilog` to know the final frame size.
2. **`gfunc_epilog`** — emits the 4-instruction epilogue
   (`addi/lwz/mtlr/blr`), then seeks back to
   `func_sub_sp_offset - PROLOG_SIZE` and backfills the 3-instruction
   prologue (`mflr/stw/stwu`). Calls `gsym(rsym)` to patch any
   pending return-jumps (none for a single-return function, so a
   no-op).
3. **`load`** — handles `VT_CONST` integer immediate. Helper
   `ppc_emit_li(gpr, v)` picks the smallest sequence:
   - `li rD, v` (1 instr) for signed 16-bit
   - `lis rD, hi` (1 instr) when low 16 are zero
   - `lis rD, hi; ori rD, rD, lo` (2 instr) for full 32-bit
4. **`ppc_frame_size()`** — Apple PPC ABI frame: 24-byte linkage area
   + locals, rounded up to 16 bytes, minimum 32.

## Test record

`int main(void) { return 42; }` compiled to `.text`:

```
0x180  7c0802a6   mflr   r0          \
0x184  90010008   stw    r0, 8(r1)    > prologue
0x188  9421ffe0   stwu   r1, -32(r1) /
0x18c  3860002a   li     r3, 42       (body)
0x190  38210020   addi   r1, r1, 32  \
0x194  80010008   lwz    r0, 8(r1)    \  epilogue
0x198  7c0803a6   mtlr   r0           /
0x19c  4e800020   blr                /
```

Hand-decoded against the PPC architecture manual (Vol 1, fixed-form
instruction encodings). This is exactly what gcc-4.0 emits for the
same input minus a couple of stylistic differences (gcc may use
`lwz r1, 0(r1)` for back-chain-style restore instead of
`addi r1, r1, frame_size`; both are correct).

Smoke-tested seven different return values (0, 1, -1, 100, 1000,
65536, -65535) — all encode correctly:

| Value  | Bytes              | Decoded                  |
|--------|--------------------|--------------------------|
| 0      | `38 60 00 00`      | `li r3, 0`               |
| 1      | `38 60 00 01`      | `li r3, 1`               |
| -1     | `38 60 ff ff`      | `li r3, -1` (sign-ext)   |
| 100    | `38 60 00 64`      | `li r3, 100`             |
| 1000   | `38 60 03 e8`      | `li r3, 1000`            |
| 65536  | `3c 60 00 01`      | `lis r3, 1`              |
| -65535 | `3c 60 ff ff`<br>`60 63 00 01` | `lis r3, -1; ori r3, r3, 1` (= 0xFFFF0001) |

## What we can't yet do

- **Run the code.** `tcc -run` requires `TCC_IS_NATIVE` to be
  defined for our (host=PPC, target=PPC) combo, which in turn pulls
  in dependencies on `tcc_add_macos_sdkpath` and friends from
  `tccmacho.c`. We excluded `tccmacho.c` in session 003 because it
  hardcodes x86_64/arm64. Wired the `TCC_IS_NATIVE` macro change
  in tcc.h but commented it out — it lights up the moment `tccmacho.c`
  builds. Same applies to producing usable `.o` files (tcc emits
  ELF without `tccmacho.c`, and Tiger's `ld` expects Mach-O).
- **Anything beyond constant return.** Variables, arithmetic, calls,
  branches — all still stub-aborts. Coming in 006+.

## Decisions

- **Backfill prologue pattern.** Same shape as `i386-gen.c`: skip
  space, emit body, backfill prologue once frame size is known. Less
  surprising than peeking ahead.
- **PROLOG_SIZE = 12 bytes always**, even for leaf functions that
  could skip prolog entirely. Simplifies bookkeeping; 12 bytes per
  leaf function is negligible.
- **Frame size minimum 32 bytes.** 24-byte linkage area + 8-byte
  alignment slack. Above the minimum we round to 16-byte alignment
  (Apple ABI requirement).
- **`TCC_IS_NATIVE` for PPC commented out** with a note pointing to
  `tccmacho.c` as the unblocker. Will reactivate in session 005.
- **`tccrun.c` icache flush** — added `TCC_TARGET_PPC` to the
  `__clear_cache()` invocation #if so it'll work the moment
  `TCC_IS_NATIVE` lights up.

## Exit state

- Three real codegen functions (`gfunc_prolog`, `gfunc_epilog`,
  `load` for VT_CONST integer).
- `tcc -c hello.c` emits a 32-byte `.text` section containing
  correct PowerPC instructions.
- `tcc -run` blocked on `tccmacho.c` PPC support (next session).

## Next session

Session 005 — bring `tccmacho.c` to life on PPC. Extend the
`#if !defined TCC_TARGET_X86_64 && !defined TCC_TARGET_ARM64`
platform check, audit `r_addend` usage (PPC32 uses `Rel`, not
`Rela`), add `CPU_TYPE_POWERPC = 18`, the PPC Mach-O reloc
enumerators (`PPC_RELOC_VANILLA` etc.), and audit anything else
hardcoded to 64-bit. Goal: `tcc -run hello.c` returns exit code
42 on the G3.

After that: more codegen (locals, arithmetic, branches, function
calls). The cycle is short — write codegen, write a small C test,
compile, hand-verify or run, commit, repeat.
