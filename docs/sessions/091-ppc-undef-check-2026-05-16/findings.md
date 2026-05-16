# Session 091 findings

## 1. PPC's `macho_load_dll` was anomalous vs tccmacho.c

Pre-091, `ppc-macho.c::macho_load_dll` registered each loaded
dylib's defined-external symbols in `s1->symtab` (with leading
underscore stripped), while `tccmacho.c::macho_load_dll` (x86_64 /
arm64) registers them in `s1->dynsymtab_section` (with underscore
intact).

The PPC version's behavior had a comment claiming "tcc's symtab
uses bare C names internally; the leading underscore gets re-added
at output emission" — that comment was simply wrong.
`tcc_state->leading_underscore = 1` makes `put_extern_sym2` prepend
`_` to every C symbol when storing in `symtab_section`, so symtab
names like `_atexit` always carry the underscore. Stripping it in
the dylib-load path produced TWO entries in symtab for any
explicitly-`-lSystem` load (`atexit` and `_atexit`), both UNDEF,
both emitted into the nlist. The nlist bloat was wasteful but
harmless because every actual reference used the underscore form
and dyld's flat namespace found it either way.

Session 091 aligns PPC with the tccmacho.c convention:
- Write to `s1->dynsymtab_section`, not `s1->symtab`.
- Keep the leading underscore.

That converts the dylib-load path from "pre-populate symtab as a
side-effect" to "build a separate validation set." Symtab UNDEFs
are now added only on demand by user-code references through
`put_extern_sym2`, which keeps the eventual nlist clean.

## 2. Tiger libSystem is an umbrella over libmathCommon

`/usr/lib/libSystem.B.dylib`'s nlist does NOT define `log2`,
`sqrtf`, `exp2`, etc. — only UNDEF references to them. The actual
definitions live in `/usr/lib/system/libmathCommon.A.dylib`, which
libSystem references via `LC_LOAD_DYLIB` + `LC_SUB_LIBRARY`.

At runtime, dyld's flat namespace finds the math symbols in
libmathCommon (because libSystem brings it in as a transitive
dependency). At LINK time, our auto-load of just libSystem's
direct exports misses them, and `check_symbols` rejects every
math-using program.

Fix: `macho_load_dll_inner` was extended to:
- Collect LC_LOAD_DYLIB paths during the load-command walk.
- At top level (`as_subdylib == 0`), recurse into each one with
  `as_subdylib = 1`.
- In the sub-library call, skip `tcc_add_dllref` so the
  sub-library does NOT end up in the EXE's `LC_LOAD_DYLIB` list.

The resulting EXE depends ONLY on libSystem (matching gcc-4.0 +
ld's default behavior on Tiger and preserving every existing
demo's `otool -L` invariant), but its dynsymtab contains the
union of libSystem's + libmathCommon's exports for validation
purposes.

Tiger libSystem only references one sub-library. macOS umbrellas
on later releases reference more (libobjc, libcorecrypto, ...).
The recursion is bounded by `MAX_SUB_DYLIBS = 16` and the
`as_subdylib` flag prevents transitive recursion — sub-libraries
themselves don't recurse — so even a deeply-nested umbrella tree
stops at depth 1.

## 3. dyld-only helper symbols aren't in libSystem's nlist

Three names that crt1.o and our exe writer reference but
libSystem.B.dylib doesn't export:

- `dyld_stub_binding_helper` — defined by crt1.o (when crt1 is
  loaded; defined symbol, not UNDEF; not flagged by
  check_symbols), or synthesized inline by the exe writer (which
  hardcodes the Tiger dyld address `0x8fe01008`).
- `__dyld_func_lookup` — same.
- `__keymgr_dwarf2_register_sections` — IS in libSystem (T
  90189bec), so naturally available.

To keep `check_symbols` from rejecting `dyld_stub_binding_helper`
or `__dyld_func_lookup` in the rare case they appear as UNDEFs in
symtab (e.g. -nostdlib programs that don't load crt1.o but still
reference them through assembly), `tcc_add_macos_sdkpath`
pre-seeds them directly into `s1->dynsymtab_section`. The exe
writer's special-case emission at the runtime end is unchanged.

## 4. The check is EXE-only

`ppc_macho_check_symbols` is called from `macho_output_file`
only on the `TCC_OUTPUT_EXE` path. DLL and OBJ outputs retain
the loose pre-091 behavior. Rationale:

- A `.o` file's nlist is supposed to have UNDEF entries — they
  get resolved at the next link step. Validating against
  dynsymtab would reject every cross-TU reference.
- A `.dylib`'s UNDEFs are similarly intentional — they're resolved
  by the consuming exe's loader. The dylib writer's deferred-work
  list (089 HANDOFF #1) doesn't include this, but if a future
  session ports `check_symbols` to the dylib path, it should
  probably allow ANY UNDEF that's a plausible consumer-provided
  name (i.e., not reject things like `printf` even if libSystem
  isn't explicitly linked).

## 5. Side observation: `_log2` is UNDEF in libSystem's own nlist

```
$ nm -gP /usr/lib/libSystem.B.dylib | grep '^_log2 '
_log2 U 0 0
_log2 U 0 0
_log2 U 0 0
```

Three UNDEF references and zero defs — they're internal calls from
inside libSystem.B.dylib to its libmathCommon sub-library, exactly
mirroring the umbrella structure. The PPC `macho_load_dll` reader
correctly classifies these as UNDEF (skipping them with the
`(n_type & N_TYPE) != N_SECT` filter) and adds them to dynsymtab
only via the libmathCommon recursive load.

## 6. The session 090 `-Werror=implicit-function-declaration`
       workaround is now obsolete

Verified by re-configuring expat 2.5.0 on imacg3 WITHOUT the
workaround:

```
HAVE_ARC4RANDOM 1
/* #undef HAVE_ARC4RANDOM_BUF */
/* #undef HAVE_GETRANDOM */
```

These are the correct answers for Tiger libSystem (arc4random
exists; arc4random_buf and getrandom do not). AC_CHECK_FUNCS's
link-and-check probe now fails cleanly for the missing symbols
(because tcc rejects the link), so autoconf records the absence.

The expat demo at `demos/v0.2.66-expat.sh` is left as-is — it's a
historical snapshot of the v0.2.66 milestone, and the workaround
still works (it just operates at compile time instead of link
time, which is a different mechanism that's harmless to keep).
Any new autoconf-driven demo (or the existing one if a future
session updates it) can drop the CFLAGS line.
