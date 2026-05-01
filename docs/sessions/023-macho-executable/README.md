# 023 — Mach-O executable output

`tcc -o exe foo.c` now produces a Mach-O executable directly — no
`gcc-4.0` link step. Two phases:

## Phase A: Bare-bones MH_EXECUTE

Self-contained executables for programs that use no libSystem (or
only direct syscalls). Layout:

```
__PAGEZERO  vmaddr 0           vmsize 0x1000   no file content
__TEXT      vmaddr 0x1000      file 0..N       header+cmds + .text + crt-shim
__LINKEDIT  vmaddr next-page   file N..        symtab + strtab
LC_SYMTAB, LC_UNIXTHREAD with srr0 = entry
```

The crt-shim (4 instructions) is appended to .text:
```
bl   _main          ; r3 = main's return value
li   r0, 1          ; SYS_exit
sc                  ; trap to kernel
trap                ; defensive
```

Internal `R_PPC_REL24` / `R_PPC_ADDR16_HA` / `R_PPC_ADDR16_LO` /
`R_PPC_ADDR32` relocations are resolved at link time.

✓ Works: `int main(){return N;}`, `sq(7)`, `fact(5)`, recursive calls.

## Phase B: libSystem stubs

When the program references external functions (e.g. `printf`,
`puts`, `getpid`), the EXE writer:

1. Calls `collect_extern_stubs()` to enumerate undef-external function
   references via REL24.
2. Synthesizes `__TEXT,__symbol_stub` (16 bytes/stub, 4 instructions:
   `lis r12, ha(slot); lwz r12, lo(slot)(r12); mtctr r12; bctr`).
3. Synthesizes `__DATA,__nl_symbol_ptr` (4 bytes/stub) — initial 0,
   filled by dyld at load.
4. Adds `LC_LOAD_DYLINKER /usr/lib/dyld`.
5. Adds `LC_LOAD_DYLIB /usr/lib/libSystem.B.dylib`.
6. Adds `LC_DYSYMTAB` with indirect symbol table.
7. Sets each undef symbol's `n_desc` = `(LIBRARY_ORDINAL=1) << 8` for
   two-level namespace lookup.
8. Uses an extended crt-shim (9 instructions, 36 bytes) that aligns
   the stack to 32 bytes and sets up a back-chain word per Apple ABI
   before `bl _main`.

The header flags are `MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL`.

✓ Works:
- `getpid()`: returns pid (mod 256).
- `write(1, "hello\n", 6)`: prints "hello\n".
- `strlen`, `strchr`, `strcmp`: pure-function libSystem calls.

✗ Fails (SIGBUS, exit 138):
- `puts("hello")` — touches `stdout` (a libSystem-internal `FILE*`).
- `printf(...)` — same.
- `getenv("HOME")` — reads `_environ` (a libSystem global).
- `malloc` — needs heap setup.
- Anything that touches per-process libSystem state.

## What's missing for full libSystem support

The same `puts` program **works** when linked via `gcc-4.0`. The
difference is `gcc-4.0` includes `/usr/lib/crt1.o` automatically.

Apple's `crt1.o` does considerable startup work that we don't
replicate:

```
start:
    ; align stack
    ; set up r3=argc, r4=argv, r5=envp from kernel-passed r1
    bl __start

__start:
    ; spill argc/argv/envp to libSystem globals (_NXArgc, _NXArgv, _environ)
    ; call __dyld_func_lookup repeatedly to find dyld helpers
    ; call dyld init helpers (image_state, etc.)
    ; call __keymgr_dwarf2_register_sections (libgcc EH unwind tables)
    ; call _atexit chain registration
    ; call _main(argc, argv, envp)
    ; call _exit(main's return)
```

`_NXArgc`, `_NXArgv`, `_environ` live in libSystem's `__DATA` segment,
accessible via `__nl_symbol_ptr` indirection (the same machinery we
use for OBJ output). Without these set, libSystem's stdio and
environment functions deref invalid pointers and crash.

## Two paths to full libSystem support (deferred)

1. **Implement a minimal Mach-O .o linker** in tcc that can read and
   merge `/usr/lib/crt1.o` (or any `.o`) into our executable output.
   This is the proper long-term path; it also enables linking
   user-provided `.o` files into the final executable.

2. **Hardcode a crt1-equivalent init stub** in `tcc/lib/lib-ppc.c`
   that reads argc/argv/envp from r1, sets up the libSystem globals
   via `__nl_symbol_ptr` references, and calls main. Smaller scope
   than option 1 but doesn't help with linking other `.o` files.

Either way, this is a follow-up session.

## Test record

| Test | Result |
|---|---|
| `int main(){return N;}` for various N | ✓ |
| `sq(7) = n*n` | ✓ exit 49 |
| `fact(5)` | ✓ exit 120 |
| `getpid()` | ✓ |
| `write(1, "hello\n", 6)` | ✓ |
| `strlen("foo") + strchr` | ✓ |
| `puts("hello")` | ✗ SIGBUS (libSystem state) |
| `printf("hello %d", 42)` | ✗ SIGBUS (libSystem state) |
| `getenv("HOME")` | ✗ SIGBUS (libSystem state) |
| Same `puts` via tcc -c + gcc-4.0 link | ✓ (gcc bundles crt1.o) |

## Files changed

- `tcc/ppc-macho.c`: +691 lines for `macho_output_exe` and helpers.

## Decisions

- **Non-lazy stubs (4-instruction load-and-jump) instead of Apple's
  32-byte PIC lazy stubs.** Simpler; works for our purposes since
  dyld resolves the slots eagerly (`S_NON_LAZY_SYMBOL_POINTERS`).
  No need for `dyld_stub_binding_helper`.

- **Two-level namespace by default** (`MH_TWOLEVEL`). Mirrors gcc-4.0
  output and is the modern Mach-O convention.

- **Single `LC_LOAD_DYLIB` for libSystem.** Not yet hooked up to
  arbitrary `-l` flags; user-requested dylibs would be a separate
  enhancement.

## Next session

024 — implement a Mach-O `.o` reader so we can pull in
`/usr/lib/crt1.o` automatically when building executables. That
unblocks full libSystem support, which is the path to a fully
self-linked tcc-self.
