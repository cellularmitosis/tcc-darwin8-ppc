# Real-world program build investigation — late session 053

## TL;DR

Late in session 053 we tried to extend the demos with new
real-world programs (gzip 1.11, sed 4.8, Python 2.7.18). The
attempts surfaced **two correctness issues with the demo
methodology** and **three real tcc codegen / runtime gaps**.
The demo scripts that initially "passed" were silently using
gcc rather than tcc; with tcc actually enforced as CC, all
three programs hit distinct bugs. The misleading demos have
been removed; two small tcc improvements (`__VERSION__`
predefine, `__builtin_isnan`/`isinf`/`isfinite` stubs) landed
because they're real fixes that came out of the investigation.

## What was misleading about the demos

The earlier `v0.2.39-gzip.sh` and (briefly added) `v0.2.39-sed.sh`
demos invoked configure as
```
CC=tcc CFLAGS="-B... -L... -I..." ./configure -C
```
with a `config.cache` derived from leopard.sh's pre-built binpkg.
The strip pattern removed the obvious env-locked entries
(`ac_cv_env_*`, `ac_cv_build*`, `ac_cv_host*`,
`ac_cv_prog_CC`, `ac_cv_prog_cc_*`) but **missed
`ac_cv_prog_ac_ct_CC=gcc`**.

`ac_cv_prog_ac_ct_CC` is autoconf's cached "actual cross tool"
program name. With it set to `gcc` and not stripped, autoconf
silently overrode the `CC=tcc` env value and the resulting
`Makefile` had `CC = gcc -std=gnu99`. The build worked because
**gcc was doing the work**. The demos' "PASS — builds with tcc"
output was wrong; they verified gcc-built binaries.

Confirmed by re-reading the resulting Makefile and the linkage of
the produced binaries (`/usr/lib/libgcc_s.dylib` on the python
case, `___builtin_clz` etc. absent on the gzip case — gcc inlines
these intrinsics; tcc-built binaries pull them from libtcc1.a).

## Stricter strip pattern + env-pin

To actually enforce CC=tcc through autoconf, the strip pattern
needs to also remove `^ac_cv_prog_ac_ct_CC|^ac_cv_prog_CPP|
^ac_cv_prog_ac_ct_CPP|^ac_cv_path_CC|^ac_cv_sys_largefile_CC`.
Once those go, configure picks up our env CC=tcc.

But that exposes the next problem: with CC=tcc, AC_CHECK_FUNC
mis-detects functions like `__freading`, `__fseterr`,
`fdopendir` as **available** because tcc's permissive
implicit-declaration handling lets the AC_CHECK_FUNC test
program link successfully (tcc treats the undeclared name as
`int (*)()` and emits a `bl __freading` reloc that defers to
link time; the link "succeeds" even though Tiger libc has no
such function). configure then writes back
`ac_cv_func___freading=yes` to the cache and the build fails
later when gnulib's `fseterr.h` includes the missing
`stdio_ext.h`.

Fix: also pin the known-bad ac_cv_func values via env:
```sh
ac_cv_func___freading=no \
ac_cv_func___fseterr=no \
ac_cv_header_stdio_ext_h=no \
ac_cv_func_fdopendir=no \
ac_cv_func_utimensat=no \
ac_cv_func_futimens=no \
ac_cv_func_lutimes=no \
ac_cv_func_futimesat=no \
./configure -C ...
```

(The cleanest long-term tcc fix is to add a strict implicit-decl
warning level so AC_CHECK_FUNC fails compile, mirroring
gcc -Werror=implicit-function-declaration. That's a bigger tcc
project — not done in this session.)

## Real bugs surfaced (and partially fixed)

### Fixed: `__VERSION__` not predefined

tcc predefines `__GNUC__` (claiming gcc compatibility) but not
`__VERSION__`. Python's `Python/getcompiler.c` does
```c
#ifdef __GNUC__
#define COMPILER "\n[GCC " __VERSION__ "]"
#endif
```
which expects both. The `_VERSION__` use is invalid token without
the predefine.

**Fix landed:** `tcc/include/tccdefs.h` now predefines
`__VERSION__ = "4.0.1 (tcc-darwin8-ppc)"` for `__APPLE__` builds.

### Fixed: `__builtin_isnan` / `__builtin_isinf` / `__builtin_isfinite` stubs missing

Tiger's `<math.h>` `isnan(x)` macro expands to
`__builtin_isnan(x)`. gcc inlines this; tcc emits a regular call.
Without a stub, programs `#include <math.h>` and use `isnan`
fail to link with "Symbol not found: ___builtin_isnan".

**Fix landed:** `tcc/lib/lib-ppc.c` now provides
`__builtin_{isnan,isinf,isfinite}` for double, plus
`f`/`l` overloads. Bootstrap fixpoint and tests2 (111/111) hold.

### NOT FIXED: gzip 1.11 — large-input codegen bug

With strict tcc-only enforcement:
- gzip configures correctly (with the env pin above).
- gzip builds correctly (`__VERSION__` and `__builtin_is*` stubs
  resolve).
- gzip runs and round-trips inputs **up to ~4 KB** byte-identically.
- gzip **breaks at ~64 KB+** with CRC and length errors on
  decompression.

Some path inside DEFLATE (likely `bits.c` / `trees.c` / `deflate.c`)
hits a tcc PPC codegen bug that surfaces only on inputs large
enough to exercise the full Huffman / dynamic-block machinery.
Needs bisection (compile each .o with gcc and tcc alternately,
narrow to the offending TU, then to the offending function).

### NOT FIXED: sed 4.8 — `ppc-gen: store via ptr of bt 0xb (VT_LDOUBLE)`

gnulib's `lib/stddef.h` defines a `max_align_t`-style struct with
a `long double __ld _GL_STDDEF_ALIGNAS(long double)` field. Some
sed source includes this and triggers a struct copy whose generated
code does a long-double store via a GPR-pair pointer (rather than
the FP-register path). `ppc-gen.c::store()`'s GPR branch does not
handle `bt == VT_LDOUBLE` and aborts with the diagnostic above.

The fix is local to `ppc-gen.c::store()`: the LDOUBLE-via-GPR-pair
case should emit a 16-byte memcpy (four `lwz`/`stw` pairs, or two
`stfd`s after re-loading into FP regs). One session of careful
work.

### NOT FIXED: Python 2.7.18 — `Fatal Python error: Can't initialize type type`

With strict tcc-only enforcement and the above fixes (plus
`-DPython/mactoolboxglue.o` removed from MACHDEP_OBJS, `-u
_PyMac_Error` and `-framework CoreFoundation` stripped from the
link command, and `LDFLAGS=-B<tcc>` set so libtcc1.a auto-links):

- All ~150 .c files compile with tcc.
- `python.exe` links against `libpython2.7.a -ldl` (no libgcc_s).
- `./python.exe -V` fails immediately with `Fatal Python error:
  Can't initialize type type`.

This is a tcc codegen bug deep inside `Objects/typeobject.c`'s
`PyType_Ready`. Needs the same bisection methodology as the gzip
case. Multi-session investigation likely.

## What we DID get from Python

When the original gzip-style demo's strip pattern was used (i.e.
`ac_cv_prog_ac_ct_CC=gcc-4.9` was retained in the cache),
`python.exe` was built entirely by gcc-4.9 — which works fine and
runs Conway's Game of Life beautifully. So:

* Python 2.7.18 builds + runs on Tiger PPC with the leopard.sh
  binpkg-cache pattern (using gcc-4.9 from tigersh-deps).
* The Game of Life script (R-pentomino, 80 generations on a
  60×30 toroidal grid) runs correctly on the gcc-built python.
* tcc's role in that build, post-strip-fix, is to compile and
  link... with one of the existing tcc bugs blocking
  Python's runtime startup.

The gcc-built artifact does demonstrate the leopard.sh dependency
stack works on imacg3 and that big things like Python can be
brought up there — but it's not a tcc demo.

## Why these demos were removed

The incorrect "tcc-built" claim is the kind of thing that erodes
trust in the rest of the demos table. Better to delete and have
the table reflect reality than to ship a misleading demo.

The HANDOFF for the next session lists these as concrete bugs to
chase:
1. Strict implicit-decl warning for tcc (so AC_CHECK_FUNC fails
   compile when the function is undeclared, matching gcc's
   `-Werror=implicit-function-declaration`).
2. `ppc-gen.c::store()` LDOUBLE-via-GPR-pair handling.
3. gzip 1.11 large-input codegen bug — bisect to TU/function.
4. Python 2.7.18 PyType_Ready codegen bug — bisect to TU/function.
