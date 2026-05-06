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

## Out of scope this session (deferred to follow-ups)

1. **Multi-dylib link-time support** — `tcc -lz file.c` still
   produces a binary that fails at runtime with
   `dyld: Symbol not found: _zlibVersion (Expected in: libSystem)`.
   Reason: `macho_load_dll()` is a no-op (returns success without
   reading the dylib), and the exe writer always emits exactly
   one `LC_LOAD_DYLIB` (libSystem). Fix is straightforward but a
   chunk of work: parse the dylib's `LC_SYMTAB`, register
   exported syms as UNDEF in our own symtab, track which dylib
   provides each, emit per-dylib `LC_LOAD_DYLIB` entries with
   correct two-level ordinals (or switch to flat namespace).
   Will land as v0.2.26.

2. **Local relocation entries for sliding** — currently dylibs
   work only when dyld loads them at the preferred vmaddr. If
   dyld slides them, absolute references in `__data` (e.g.
   `int *p = &arr[N]` static initializers) won't be patched.
   Function calls survive (PIC stubs). For an MVP this is
   acceptable; programs hit the slide path rarely with a high
   default vmaddr.

3. **JIT heisenbug** (carried from session 044). Untouched this
   session.

4. **dlltest upstream stage** — needs the multi-dylib link-time
   support above (loading libtcc.dylib as a link-time dependency
   for tcc.c). Will follow naturally once v0.2.26 lands.

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

