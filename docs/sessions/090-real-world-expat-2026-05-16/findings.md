# Session 090 findings

## 1. tcc's PPC writer doesn't validate undefined externals

**Symptom.** Any `tcc -o exe foo.c` where `foo.c` calls an extern
function not present in any linked dylib produces a working binary
file. The binary's nlist table has `_funcname U`. `otool -L` shows
only `libSystem.B.dylib` (the implicit dependency). dyld fails at
program startup with `Symbol not found: _funcname`. There is no link
error. `tcc` returns exit 0.

**Reduced repro** (2 lines):

```c
extern void definitely_not_a_real_function_qwerty(void);
int main(void) { definitely_not_a_real_function_qwerty(); return 0; }
```

```
$ tcc foo.c -o foo                          # exit 0
$ gcc-4.0 foo.c -o foo                      # exit 1, ld: Undefined symbols
$ nm -gP foo | grep qwerty
_definitely_not_a_real_function_qwerty U 0 0
```

**Code archaeology.** `tccmacho.c::check_symbols` (lines 1003–1050 as
of HEAD `cf4fc45`) walks `symtab_section`, classifies each
`SHN_UNDEF` symbol, and:

```c
if (ELFW(ST_BIND)(sym->st_info) == STB_WEAK
    || s1->output_type != TCC_OUTPUT_EXE
    || find_elf_sym(s1->dynsymtab_section, name)) {
    sym->st_shndx = SHN_FROMDLL;
    continue;
}
tcc_error_noabort("undefined symbol '%s'", name);
ret = -1;
```

That is correct behavior — weak / non-EXE / known-from-dylib pass,
unknown fail.

But **`tcc/Makefile` line 224** routes the PPC target to
`ppc-macho.c`, not `tccmacho.c`:

```
ppc-osx_FILES = $(ppc_FILES) ppc-macho.c
```

(while `x86_64-osx_FILES` and `arm64-osx_FILES` use `tccmacho.c`).

`ppc-macho.c` has no equivalent function. `grep "find_elf_sym\|dynsymtab"
tcc/ppc-macho.c` returns three hits — none of them check undefined
externals against the dylib symbol table. The classifier (`classify_sym`
near line 1145) sees `sym->st_shndx == SHN_UNDEF` and unconditionally
emits the symbol as `N_EXT | N_UNDF`. dyld is left to fend for
itself at load time.

**Why this didn't bite earlier.** Every prior real-world demo
(lua, sqlite, sed, gzip, awk, ...) called functions that DO exist in
Tiger libSystem, so the loose check happened to do the right thing.
Hand-rolled demos compiled symbols they themselves defined, plus
libSystem symbols — same. AC_CHECK_FUNCS is the first machinery to
*probe* unknown symbols by intentionally trying to link against
them and checking the exit code; that's where the gap shows.

**Side note: tcc's behavior is not "obviously buggy."** Mach-O lazy
binding lets you produce a binary referencing a symbol that may be
provided by a different dylib at load time (the historical
`-undefined dynamic_lookup` mode). But for the default case
(two-level namespace, no flat-lookup opt-in, ordinary `-lFoo`
links), Apple's `ld` errors at link time on unresolvable externs.
That's the behavior `AC_LINK_IFELSE` was designed against, and that
tcc's PPC writer should match by default.

**Proper fix sketch** (now item #12 on the roadmap):

1. Auto-load libSystem.B.dylib's exports into `s1->dynsymtab_section`
   for PPC EXE output, even when `-lSystem` isn't explicit
   (libSystem is always emitted as an LC_LOAD_DYLIB by the writer
   regardless, so this just brings the symbol-resolution side in
   line with the load-command side).
2. After all input is processed but before writing the macho exe,
   walk `symtab_section`, find every `SHN_UNDEF + STB_GLOBAL` sym
   that isn't `STB_WEAK`, look it up in `s1->dynsymtab_section`,
   error out if not found.
3. Add an opt-out flag — `-undefined dynamic_lookup` is gcc/ld's
   convention — for programs that legitimately rely on flat-namespace
   runtime lookup. (Probably rare in tcc's actual user base, but
   worth having.)

**Workaround until #12 lands.**
`CFLAGS=-Werror=implicit-function-declaration`. tcc already emits
an implicit-declaration warning when a configure conftest calls
an undeclared function; turning that warning into an error breaks
the conftest *at compile time*, before the link step, so
AC_LINK_IFELSE sees the failure it should have seen.

This works because autoconf's `AC_LINK_IFELSE` macros write conftest
sources of the form:

```c
#include <stdlib.h>  /* or whatever header the macro hints at */
int main(void) { the_function_under_test(...); return 0; }
```

with no explicit declaration of `the_function_under_test`. If the
header *doesn't* declare it (because it doesn't exist on the
platform), the implicit-declaration warning fires and Werror makes
it fatal.

Caveat: this is opt-in (the user has to know to pass the CFLAGS).
And it can produce false negatives for older AC_CHECK_FUNCS
implementations that emit `char funcname();` explicit-declaration
conftests (no implicit warning to catch). expat 2.5.0 uses
AC_LINK_IFELSE with hand-written sources, so it works.

## 2. expat's C++ test driver (`runtestspp`) won't link with tcc-built .o + gcc-4.0 g++

When `make check` runs in `tests/`, it tries to build both `runtests`
(C, fine) and `runtestspp` (C++, links against tcc-built
`libexpatinternal.a`). The C++ link fails:

```
ld: libexpatinternal.a(xmlparse.o) section's (__TEXT,__eh_frame)
    type S_REGULAR does not match previous objects type S_COALESCED
```

tcc emits `__eh_frame` as `S_REGULAR`; gcc-4.0 emits it as
`S_COALESCED`. The C++ link merges objects from both compilers and
trips Apple ld's section-type-consistency check.

This is a real difference (gcc's `__eh_frame` participates in
COMDAT-style deduplication; tcc's is a plain section). Not in scope
for session 090 — the C testsuite passes, which is the meaningful
signal. Would matter only if we needed expat's `runtestspp` to run.

Possible fix would be to mark tcc's `__eh_frame` as `S_COALESCED`
in `classify_section` (or `macho_output_exe`'s segment-emission
code). Filed as a side-note here, not currently on the roadmap —
it's only observable when mixing tcc-built `.o` with gcc-built `.o`
in a C++ link, which is not a use case the project actively
supports.

## 3. expat's full build is autoconf-driven and trivially clean

For the record (so a future session doesn't redo the diligence): the
fully-AC-driven build with our workaround flag produces:

- `lib/.libs/libexpat.a` — the static library (~140 KB).
- `xmlwf/xmlwf` — the validator binary (~80 KB), links only libSystem.
- `tests/runtests` — the test driver, passes 345/345.
- `examples/elements` and `examples/outline` — small example programs.
- `tests/benchmark/benchmark` — micro-benchmark.

`make -j` works. `make check` works (modulo the C++ test driver
above). `make install` not exercised. Dylib (`--enable-shared`) not
exercised in this session — kept `--disable-shared` because dylib
emission has its own deferred work (089 handoff #1).
