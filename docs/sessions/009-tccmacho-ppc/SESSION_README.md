# 009 — Real PowerPC Mach-O .o output

🎉 **Major milestone:** tcc compiles C source to a real on-disk
PowerPC Mach-O `.o` file that Tiger's `gcc-4.0` / `ld` link into a
runnable executable.

## Arrival state

After 008, tcc could compile a wide swath of C code (locals,
arithmetic, branches, function calls, pointers, arrays, modulo) and
JIT-execute via `tcc -B. -run`. But `tcc -c file.c -o file.o` produced
ELF .o files Tiger's linker couldn't consume. The path to `printf`,
to bootstrapping, and to a shippable tarball was all blocked on this.

## What was done

### Big upfront design pass

Spawned an exploration subagent to do an exhaustive surgical survey
of `tcc/tccmacho.c` (the existing x86_64/arm64-only Mach-O writer)
and produce a porting plan. The output: [findings.md](findings.md)
(1,436 lines), [README.md](README.md) (140-line overview), and
[SURVEY_SUMMARY.txt](SURVEY_SUMMARY.txt) (433 lines) — together they
list every code site needing change for PPC32, with line numbers,
difficulty estimates, and a 5-phase rollout plan.

The survey recommended extending tccmacho.c with `#ifdef
TCC_TARGET_PPC` branches everywhere (~25 surgical sites). After
weighing complexity vs. risk we chose a different path:

### Strategy chosen: stand-alone `tcc/ppc-macho.c`

Rather than thread PPC32 through tccmacho.c's existing 64-bit
machinery, a separate file was written specifically for PPC32. Pros:

- Tccmacho.c stays untouched (no risk to the working x86_64/arm64
  paths).
- The PPC file is small (~1,100 lines) and focused — easy to read,
  easy to extend.
- No `#ifdef TCC_TARGET_PPC ... #else ... #endif` clutter.

Con: some duplication with tccmacho.c. Acceptable for a Tiger-only
fork that we don't plan to upstream.

### `tcc/ppc-macho.c` (new, ~1,100 lines)

Implements 32-bit Apple PowerPC Mach-O object output:

- **`mach_header`, `segment_command`, `section`, `nlist`** — 32-bit
  variants, hand-written from the Apple Mach-O format reference.
- **CPU constants:** `CPU_TYPE_POWERPC = 18`, `CPU_SUBTYPE_POWERPC_ALL = 0`.
- **Magic:** `MH_MAGIC = 0xfeedface` (32-bit), in big-endian order.
- **Big-endian field writers** — every Mach-O header / load command /
  section header field is emitted big-endian, matching what Tiger's
  `ld` and `dyld` expect to read.
- **`macho_output_file`** — main export. For `TCC_OUTPUT_OBJ`:
  - Walks tcc's internal sections (text, data, bss).
  - Builds a `MH_OBJECT` Mach-O with one anonymous segment containing
    those sections (Mach-O .o convention).
  - Pre-resolves same-section relocations via `relocate()` from
    `ppc-link.c`.
  - Emits the symbol table (LC_SYMTAB) with local + global symbols.
  - Emits per-section relocation entries, translating ELF reloc types
    to Mach-O: `R_PPC_REL24 → PPC_RELOC_BR24`, etc.
  - Writes everything big-endian.
- **The other macho_* exports** — `tcc_add_macos_sdkpath` (adds
  `/usr/lib`), `macho_load_dll`/`macho_load_tbd` (still error,
  feature-gated for a later session), `macho_tbd_soname` (returns
  NULL), `__clear_cache` (PPC dcbst/sync/icbi/sync/isync sequence).

Replaces `ppc-macho-stubs.c` in the `ppc-osx_FILES` Makefile entry.

### `tcc/tccelf.c` — output dispatch

Added a small `#if defined TCC_TARGET_MACHO && defined TCC_TARGET_PPC`
branch in `tcc_output_file()` to route `TCC_OUTPUT_OBJ` to
`macho_output_file()` instead of `elf_output_obj()`. (The existing
non-PPC Mach-O targets historically emit ELF .o here; that's a
separate cleanup, out of scope.)

## Test record

```
$ ssh imacg3
$ cd ~/tcc-darwin8-ppc/tcc
$ echo 'int main(void){return 42;}' > /tmp/h.c
$ ./tcc -c /tmp/h.c -o /tmp/h.o
$ file /tmp/h.o
/tmp/h.o: Mach-O object ppc                  ← real PPC Mach-O!
$ otool -h /tmp/h.o
Mach header
   magic    cputype  cpusubtype  filetype ncmds sizeofcmds  flags
0xfeedface  18       0           1        3     296         SUBSECTIONS_VIA_SYMBOLS
$ gcc-4.0 /tmp/h.o -o /tmp/h
$ /tmp/h ; echo $?
42
```

Programs verified to compile + link + run end-to-end:

| Program | Result |
|---|---|
| `int main(void){return 42;}` | exit 42 ✓ |
| Sum 1..5 via while-loop | exit 15 ✓ |
| `sq(7)` (one helper function) | exit 49 ✓ |
| `fib(10)` (recursive) | exit 55 ✓ |
| `sum_array` over `int[10]` | exit 55 ✓ |

## What works

- `MH_OBJECT` output (`.o` files).
- Single anonymous segment containing all sections.
- Local + global symbol table (LC_SYMTAB + LC_DYSYMTAB).
- R_PPC_REL24 (intra-section `bl`) → PPC_RELOC_BR24.
- gcc-4.0 / Tiger ld accepts and links the output.

## What's deferred

- Calls into dynamic libraries (printf, malloc) — needs PIC stub
  emission, `__la_symbol_ptr`, and `macho_load_dll`. Future session.
- HA16/LO16 with non-zero addends — current PAIR entries always have
  `other_half = 0` (sufficient for tcc-emitted relocations, won't
  scale to externs with offsets).
- Executable output (we emit `.o`, gcc/ld does the link). Direct
  `tcc -o exe` is a separate piece of work.
- Pre-existing tcc PPC codegen bug: global variable references hit
  `ppc-gen: load stub` before reaching the writer. To fix in the next
  codegen session.

## Decisions

- **Stand-alone `ppc-macho.c`** rather than `#ifdef`-extending
  tccmacho.c. Cleaner, lower-risk.
- **Pre-resolve same-section relocations via tcc's `relocate()`**
  rather than emit explicit Mach-O relocs for every reference.
  Reduces the .o file's reloc count and matches what gcc-4.0 emits.
- **Big-endian writers everywhere** — even for fields that "look
  like" they could be assigned directly. PPC Mach-O is target-endian
  big, host-endian on tcc's build hosts is little (arm64/x86_64).

## Demo

[`demos/s009-real-macho.sh`](../../../demos/s009-real-macho.sh) — compiles
a multi-function program, links with gcc, runs. Exit 42.

## Exit state

The compile → link → run pipeline works on the G3 for any C
program tcc can already JIT-compile, modulo the deferred items above.
This unblocks the path to: dylib loading, printf, struct/varargs work,
bootstrap, and the G3 tarball.

## Next session

Per the agreed roadmap (B → C → A):

1. **Long long** — tcc uses these heavily in its own source; needed
   for self-host. PPC32 passes int64 in r3:r4 register pairs.
2. **Struct passing** — Apple PPC ABI: small structs in regs, larger
   ones via hidden pointer in r3.
3. **Varargs** — `gen_va_start` / `gen_va_arg`. Apple PPC convention
   uses the parameter save area at top of frame.
4. **Floating point** — gen_opf, FP load/store, FP arg/return per
   Apple ABI.

After C and A are done, we'll have everything needed to attempt the
self-host bootstrap.
