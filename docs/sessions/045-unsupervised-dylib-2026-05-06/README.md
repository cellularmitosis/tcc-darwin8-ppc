# Session 045 — unsupervised dylib + beyond (2026-05-06)

## Arrival state

- HEAD: `2f078dd` (v0.2.24-g3 default — `tcc -ar` native Mach-O).
- tests2: 111 / 111 (100%).
- Bootstrap fixpoint holds.
- Five real-world programs work end-to-end (lua, zlib, bzip2, cJSON, sqlite-with-file).
- `tcc -ar` is now native Mach-O.
- `tcctest.c` diff vs gcc reference: 33 lines.

## Exit state (after v0.2.25-g3)

- v0.2.25-g3 cut. **First Mach-O dylib output on Tiger PPC.**
- `tcc -shared -o foo.dylib foo.c` produces a real PPC dylib;
  dyld loads it via `dlopen`; dlsym finds exported functions.
- New demo: [`v0.2.25-dylib.sh`](../../../demos/v0.2.25-dylib.sh) —
  builds a libgreet.dylib with `greet(const char *)` and
  `get_count(void)`; links a tcc-built exe that dlopens it; prints
  three greetings.
- tests2 still 111/111. Bootstrap fixpoint still holds.

## What landed

### v0.2.25-g3 — Mach-O dylib output

- New `macho_output_dylib()` in `tcc/ppc-macho.c` (~600 lines,
  mirrors `macho_output_exe()` with these dylib-specific
  differences):
  - `MH_DYLIB` filetype (was `MH_EXECUTE`).
  - No `__PAGEZERO` segment (host process owns page 0, not the
    dylib).
  - No `LC_UNIXTHREAD` (dylibs are not entry points).
  - No crt-shim, no `_main` lookup, no auto-injected
    `__tcc_start_main`.
  - `LC_ID_DYLIB` describes this dylib's install name.
  - `__mh_dylib_header` (vs `__mh_execute_header`) at __TEXT base.
  - Default __TEXT vmaddr `0x40000000` (high enough to avoid
    common conflicts).
  - Function-call stubs are **PIC** (8 instructions / 32 bytes
    per stub). Old absolute stubs would break under dyld's
    whole-image slide; new PIC stubs use `mflr / bcl / mflr / addis
    / mtlr / lwz / mtctr / bctr` with a `(slot - .L1)` SECTDIFF
    anchor that's invariant under sliding.

- Output dispatch in `macho_output_file` now routes
  `TCC_OUTPUT_DLL` to `macho_output_dylib`.
- `tcc.c` — when no `-o` is supplied, `-shared` produces a
  `.dylib` extension on Mach-O.
- `tccelf.c` — for `TCC_OUTPUT_DLL` skip crt1.o auto-link, skip
  `__tcc_start_main` injection, skip the keymgr stub. Still link
  libtcc1.a (for libgcc helpers like `__muldi3` etc.).

- Tested end-to-end on ibookg37:
  - Simple functions (`int double_it(int)`).
  - Functions calling printf/strlen via PIC stubs.
  - Static initialized data (`static int call_count`).
  - `__attribute__((constructor))` runs at dlopen time.
  - pthread mutex usage.
  - Long-long arithmetic (libtcc1.a auto-linked).

### Debugging note: dyld faults without LC_DYSYMTAB

First attempt produced a dylib with everything **except**
`LC_DYSYMTAB`. dyld then faulted with `KERN_PROTECTION_FAILURE`
in `ImageLoaderMachO::doBindExternalRelocations()` during
`dlopen`. Resolution: always emit `LC_DYSYMTAB` (and
`LC_LOAD_DYLINKER`) for dylibs, even when all the counts are
zero. Without `LC_DYSYMTAB` dyld walks uninitialized state when
processing the dylib's external relocations.

### v0.2.26-g3 — link-time dylib support

Landed in the same session as v0.2.25. `tcc -lz hello.c` now
actually works at runtime:

- `macho_load_dll()` (was a no-op stub) now parses the dylib's
  Mach-O header (with fat-binary handling), walks `LC_SYMTAB`,
  and registers each defined-external symbol as UNDEF in our own
  symtab via `set_elf_sym(SHN_UNDEF)`. The Mach-O leading
  underscore is stripped to match tcc's bare-C-name internal
  convention. Captures the install name from `LC_ID_DYLIB` and
  adds a `DLLReference` via `tcc_add_dllref()`.
- Both `macho_output_exe` and `macho_output_dylib` now walk
  `s1->loaded_dlls` and emit one `LC_LOAD_DYLIB` per loaded dll
  (libSystem first when externs exist, extras after). Total
  `cmdsize` and `ncmds` updated accordingly.
- When extra (non-libSystem) dylibs are loaded, the writer
  switches to **FLAT namespace** (clears `MH_TWOLEVEL`) so dyld
  searches all loaded dylibs at runtime. This avoids the
  per-symbol two-level-ordinal tracking that strict TWOLEVEL
  would require — a side-table mapping each undef sym to its
  source dll. UNDEF n_desc gets ordinal=0 (DYNAMIC_LOOKUP) under
  flat; ordinal=1 (libSystem) under two-level (preserves
  existing libSystem-only behavior).
- **Closes upstream `dlltest`** (multi-session deferred).
  tcc compiles libtcc.c into libtcc2.dylib, links a tcc2 exe
  against that dylib (the libtcc.c symbols come from the
  parsed `LC_SYMTAB`), and tcc2 -run prints "Hello World"
  through the full round trip.
- Demo: [`v0.2.26-link-dylib.sh`](../../../demos/v0.2.26-link-dylib.sh)
  builds a tiny exe that calls into Tiger's bundled
  `/usr/lib/libz.1.dylib` (`zlibVersion`, `adler32`) at link
  time. `otool -L` shows the libz `LC_LOAD_DYLIB`. Runs at
  runtime printing zlib 1.2.3.

Pre-v0.2.26, that demo failed at runtime with:
```
dyld: Symbol not found: _zlibVersion
  Referenced from: /tmp/testz
  Expected in: /usr/lib/libSystem.B.dylib
```
because the exe never emitted `LC_LOAD_DYLIB libz.1.dylib`.

### Out of scope this session (deferred to follow-ups)

1. **Local relocation entries for dylib sliding** — currently
   our dylibs work only when dyld loads them at the preferred
   vmaddr. If dyld slides them, absolute references in `__data`
   (e.g. `int *p = &arr[N]` static initializers) won't be
   patched. Function calls survive (PIC stubs). For an MVP this
   is acceptable; programs hit the slide path rarely with a high
   default vmaddr.

2. **JIT heisenbug** (carried from session 044). Untouched this
   session.

3. **Two-level namespace with multi-dylib** — currently we
   switch to flat namespace whenever extra dylibs are loaded.
   That works but is slightly less efficient at dyld lookup
   time and is more permissive (any dylib's symbol can shadow
   another). Strict two-level requires per-symbol ordinal
   tracking. Future polish.

## Subagent log

See [subagents.md](subagents.md). No subagents used in v0.2.25;
the codebase is small enough and the dylib writer is enough of a
careful design (PIC stubs, layout, dyld quirks) that solo work
was simpler than briefing a subagent.

## Files touched (v0.2.25-g3)

- `tcc/ppc-macho.c` — added `macho_output_dylib()` function and
  dispatcher entry; added `MH_DYLIB`, `LC_ID_DYLIB` constants.
- `tcc/tcc.c` — `.dylib` default extension when `-shared` and no
  explicit `-o` on Mach-O.
- `tcc/tccelf.c` — skip crt1/__tcc_start_main/keymgr injection for
  DLL output type; still link libtcc1.a.
- `demos/v0.2.25-dylib.{c,sh}` — runnable demo.
- `demos/README.md` — table row.
- `docs/roadmap.md` — release row + structural item #3.5 closed.
- `scripts/build-release-tarball.sh` — VERSION default + release
  notes for v0.2.22 / v0.2.23 / v0.2.24 / v0.2.25.

