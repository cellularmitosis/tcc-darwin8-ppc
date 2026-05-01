# 025 — Mach-O .o reader (handoff to next session)

## Goal recap

Close the libSystem-init gap from
[024](../024-libsystem-init/README.md) by implementing the proper
path: a classic-Mach-O `.o` reader so tcc can pull in
`/usr/lib/crt1.o` automatically for executable output. crt1.o sets up
all the magic that makes libSystem stdio (printf, malloc, etc.) work.
Path chosen per the [linker-design notes](../../notes/linker-design.md)
— this is what every other tcc target does internally.

## What's done

* **`AFF_BINTYPE_MACHO_REL` (=5)** added in `tcc/tcc.h`.
* **`tccelf.c::tcc_object_type`** extended to detect classic Mach-O
  files (magic `0xfeedface`, filetype `MH_OBJECT=1`) and return the
  new bintype.
* **`libtcc.c::tcc_add_binary`** dispatches `AFF_BINTYPE_MACHO_REL`
  to `macho_load_object_file()` (gated on PPC Mach-O).
* **`tcc/ppc-macho.c`** — ~600-line `macho_load_object_file()` and
  helpers that:
  - Read the whole file into memory.
  - Parse the Mach header + load commands.
  - Map Mach-O sections to tcc's ELF-style section list:
    `__TEXT,__text → .text`, `__TEXT,__const|__cstring → .rodata`,
    `__DATA,__data|__dyld → .data`, `__DATA,__bss → .bss`. Skip
    `__symbol_stub*`/`__nl_symbol_ptr`/`__la_symbol_ptr` so our exec
    writer can re-synthesize them.
  - Translate the symbol table; common symbols get space allocated
    in `.bss`.
  - Translate relocations: VANILLA, BR24/JBSR, HA16/HI16/LO16 with
    PAIR consumption; references that target a skipped stub/la-ptr
    section get rewritten to the underlying extern via the indirect
    symbol table (`macho_resolve_stub_slot`).

## What's blocking

After all the above lands and **tcc rebuilds successfully on
imacg3**, passing `/usr/lib/crt1.o` to tcc on the command line still
errors with:

```
$ ./tcc -B. -I. -nostdinc -o /tmp/h /tmp/h.c /usr/lib/crt1.o
tcc: error: /usr/lib/crt1.o: unrecognized file type
```

So `tcc_object_type` is returning 0, not `AFF_BINTYPE_MACHO_REL`.
The check at the bottom of `tcc_object_type`:

```c
#if defined(TCC_TARGET_MACHO) && defined(TCC_TARGET_PPC)
    if (size >= 16) {
        ...
        if (magic == 0xfeedface && ftype == 1) return AFF_BINTYPE_MACHO_REL;
    }
#endif
```

is either not being compiled in (preprocessor guard wrong?) or
returning false at runtime (magic/ftype not what we expect).

**First debug step:** confirm `TCC_TARGET_MACHO` and `TCC_TARGET_PPC`
are both defined when `tccelf.c` is compiled. `grep '#define
TCC_TARGET_' tcc/config.h` and inspect. If only one is defined, the
guard never fires; if both are defined, add a `printf("magic=%x
ftype=%x\n", ...)` to the function and re-run to see actual values.

## Other rough edges in the reader

After the dispatch is fixed, expect these to surface:

1. **Local-section reloc targets** are dropped (the `else { continue;
   }` branch in `macho_translate_relocs`). This will hurt `crt1.o`'s
   internal `bl _start → __start` REL24 (a few of crt1's 127 text
   relocs). Need a way to look up the merged tcc symbol from a
   crt1-internal address, or synthesize section-relative anchor syms
   during pass 1.

2. **Pre-injected `__tcc_start_main` will conflict** with crt1's own
   `_NXArgc`/`_NXArgv`/`_environ` definitions. Once crt1 is linking,
   gate the auto-injection in `tccelf.c::tcc_output_file` so it only
   fires when `crt1.o` is *not* loaded. Easy way: skip injection if
   the symtab already has `_start` defined.

3. **Auto-add `/usr/lib/crt1.o`** for TCC_OUTPUT_EXE on PPC. Once
   manual `tcc foo.c /usr/lib/crt1.o` works, hook it in
   `tcc_output_file` (or earlier — like in `tcc.c`'s arg processing)
   so the user doesn't have to specify it. Match what other tcc
   targets do for their crt files.

4. **Entry point becomes `start` (from crt1)**, not our synthesized
   `_start` (the shim). Update `macho_output_exe`'s entry-point
   selection to prefer `_start`/`start` from crt1 over the shim when
   present.

5. **Common symbols** (allocated in `.bss`) need an `EXE writer`
   pass to actually emit a `__bss` section in __DATA. The OBJ writer
   handles this; the exe writer's `data` handling in
   `macho_output_exe` doesn't yet pick up `.bss`.

## How to test

The minimal end-to-end test once everything works:

```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
./scripts/build-tiger.sh                   # rebuild tcc
cat > /tmp/h.c <<'EOF'
int printf(const char *,...);
int main(void) { printf("hello via full self-link\n"); return 0; }
EOF
./tcc/tcc -B./tcc -I./tcc -o /tmp/h /tmp/h.c
/tmp/h                                     # should print, exit 0
```

If that works without `/usr/lib/crt1.o` on the command line, we have
fully self-linked exec output for libSystem-using programs.

## Dev-host change

This session also switches preferred dev host from `imacg3` to
`ibookg37` (300 MHz faster). All the build/sync scripts assume
`imacg3` — searching for that string and replacing with `ibookg37`
should cover it. Check:

* `scripts/build-tiger.sh`
* `scripts/bootstrap-tcc-self.sh`
* `scripts/build-release-tarball.sh`
* `README.md` build instructions

`ibookg37` runs the same Tiger 10.4.11 PowerPC OS as `imacg3` — same
gcc-4.0, same `/usr/lib/crt1.o`, same dyld, same libSystem.

## Files changed this session

- `tcc/tcc.h`: added `AFF_BINTYPE_MACHO_REL`.
- `tcc/tccelf.c`: `tcc_object_type` Mach-O detection + executable
  auto-inject of `__tcc_start_main` (from 024).
- `tcc/libtcc.c`: dispatch in `tcc_add_binary`.
- `tcc/ppc-macho.c`: `macho_load_object_file` and helpers
  (`macho_section_to_elf`, `macho_translate_sym`,
  `macho_translate_relocs`, `macho_resolve_stub_slot`, `mach_get32`,
  `mach_get16`, `struct mach_section`/`mach_nlist`/`mach_load_ctx`).

## Status table

| Step | State |
|---|---|
| Add MH_OBJECT detection in `tcc_object_type` | code added; runtime detection not firing |
| Implement `macho_load_object_file` | code added; not exercised |
| Hook into `tcc_add_binary` | code added; dispatch not reached |
| Auto-add `/usr/lib/crt1.o` for `-o exe` | not started |
| Drop `__tcc_start_main` auto-inject when crt1 loaded | not started |
| Full self-link end-to-end | not started |
