# Session 090 — Real-world program sweep: libexpat builds clean; AC_CHECK_FUNCS trap surfaced

**Date:** 2026-05-16
**Arrival HEAD:** `cf4fc45` (end of session 089)
**Exit HEAD:** (this session, no tcc source change — demo + docs only)

## TL;DR

Picked option (b) from 089's handoff: try another real-world
program. Started with **libexpat 2.5.0**, the C XML parser used by
Python, Apache, libxml, WebKit, and many others. **expat builds
cleanly with tcc as CC on Tiger PPC; its embedded 345-test internal
suite passes 345/345.** Tenth real-world program after lua, zlib,
bzip2, cJSON, sqlite, sed, gzip, Python, awk.

Along the way the build surfaced a meaningful tcc finding that
deserves a focused follow-up session:

> **`ppc-macho.c`'s EXE writer doesn't validate undefined external
> symbols against the linked dylibs' exports**, so autoconf's
> `AC_LINK_IFELSE` / `AC_CHECK_FUNCS` macros give false positives for
> every function it probes. Concrete victim in expat:
> `arc4random_buf` (not in Tiger libSystem; `arc4random` IS) was
> detected as available, the build produced `xmlwf`, dyld failed at
> startup with `Symbol not found: _arc4random_buf`.

The workaround that ships in the v0.2.66-expat demo is
`CFLAGS=-Werror=implicit-function-declaration`: it turns tcc's
existing implicit-declaration warning into a hard error, breaking the
configure conftest exactly where the test program calls a symbol
without a header declaration. With the flag, configure correctly
detects `HAVE_ARC4RANDOM_BUF` undef and `HAVE_ARC4RANDOM=1`, and the
build is clean. **No tcc source change in this session.** The proper
tcc fix (item #12 on the roadmap) is queued for a focused next
session.

## What landed

- [`demos/v0.2.66-expat.sh`](../../../demos/v0.2.66-expat.sh):
  new demo. Downloads expat-2.5.0.tar.gz, configures with `CC=tcc`
  and the `-Werror=implicit-function-declaration` workaround, runs
  `make`, asserts `xmlwf` links only `libSystem.B.dylib`, smoke-tests
  the parser on well-formed / malformed / numeric-char-ref XML, and
  runs the full embedded `runtests` C testsuite (the C++ `runtestspp`
  is skipped — we have no PPC g++ that can ingest tcc-built .o files,
  separate concern).
- [`demos/README.md`](../../../demos/README.md): new row for
  v0.2.66-expat with the AC_CHECK_FUNCS-finding writeup.
- [`docs/roadmap.md`](../../../docs/roadmap.md):
  - #10 (Real-world program builds) bumped from "Nine" to "Ten"
    programs, with expat added to the list.
  - New #12 (PPC undefined-external symbol validation) added,
    capturing the proper-fix sketch.
- This README + [`findings.md`](findings.md) + [`HANDOFF.md`](HANDOFF.md).

**No tcc source change.** No version bump. tests2 111/111
(unchanged), bootstrap fixpoint holds (unchanged), all v0.2.12..v0.2.66
demos still PASS.

## How the AC_CHECK_FUNCS trap was found

The first configure-and-build pass — done without the `-Werror`
workaround — looked like it worked end-to-end. `make` succeeded,
`xmlwf/xmlwf` was produced, and `otool -L` showed only
`libSystem.B.dylib`. Then:

```sh
$ ./xmlwf/xmlwf -v
dyld: Symbol not found: _arc4random_buf
  Referenced from: ./xmlwf/xmlwf
  Expected in: /usr/lib/libSystem.B.dylib
```

Tiger's libSystem has `_arc4random`, `_arc4random_stir`,
`_arc4random_addrandom` — but not `_arc4random_buf`. (`_arc4random_buf`
arrived in Apple's BSD layer somewhere around 10.7+.)

Backed up to the build log: the only warning was

```
xmlparse.c:908: warning: implicit declaration of function 'arc4random_buf'
```

That line in `xmlparse.c`:

```c
#if defined(HAVE_ARC4RANDOM_BUF)
  arc4random_buf(&entropy, sizeof(entropy));
  return ENTROPY_DEBUG("arc4random_buf", entropy);
#elif defined(HAVE_ARC4RANDOM)
  ...
```

So `expat_config.h` had `HAVE_ARC4RANDOM_BUF=1` defined — even though
Tiger doesn't have the function. Why?

Configure's log (`config.log`) said the test for `arc4random_buf`
*did* succeed:

```
configure:19241: checking for arc4random_buf (BSD or libbsd)
configure:19256: tcc ... -o conftest -g -O2 conftest.c >&5
conftest.c:32: warning: implicit declaration of function 'arc4random_buf'
configure:19256: $? = 0
configure:19261: result: yes
```

`$? = 0` from tcc. The link succeeded. Reduced to a 2-line repro:

```c
extern void nonexistent_function_xyzzy(void);
int main(void) { nonexistent_function_xyzzy(); return 0; }
```

`tcc -o p p.c` → exit 0, produces a binary with `_nonexistent_function_xyzzy U`
in its nlist. `gcc-4.0` on the same file errors with `Undefined
symbols: _nonexistent_function_xyzzy`.

**tcc emits undefined external symbols into the binary without
checking that any linked dylib provides them.** AC_LINK_IFELSE
relies on the link failing when a symbol is missing; tcc lets it pass.

`tccmacho.c::check_symbols` (the writer for x86_64 / arm64 macOS) does
this check — walks `symtab_section`, looks each `SHN_UNDEF` symbol
up in `s1->dynsymtab_section`, errors if it's not weak and not found.
The PPC writer (`ppc-macho.c`, separate file since it was a
from-scratch rewrite for this project) never had the equivalent
ported in. See [`findings.md`](findings.md) for the full code-archaeology.

## How the workaround was found

The build warning that fired *was* "implicit declaration of function
'arc4random_buf'" — and tcc supports `-Werror=implicit-function-declaration`.

```sh
$ CFLAGS="-Werror=implicit-function-declaration" ./configure ...
```

The arc4random_buf conftest's source is:

```c
#include <stdlib.h>  /* for arc4random_buf on BSD, for NULL */
int main() {
    arc4random_buf(NULL, 0U);
    return 0;
}
```

`<stdlib.h>` on Tiger doesn't declare `arc4random_buf` (because Tiger
doesn't have it). With the Werror flag, tcc rejects the compile;
configure sees `$? != 0`; reports "no"; tries the next branch
(arc4random); finds it; sets `HAVE_ARC4RANDOM=1`; expat falls through
the `#elif defined(HAVE_ARC4RANDOM)` branch instead. Clean build,
working binary.

This is the generic workaround for autoconf-on-tcc-on-Tiger until
item #12 lands.

## What expat exercises that the prior demos didn't

- **XML tokenizer / parser** with table-driven state machines
  (~13K lines total across `xmlparse.c`, `xmltok.c`, `xmlrole.c`).
- **String-interning hash tables** with custom comparators —
  exercises function-pointer / struct-pointer patterns slightly
  different from the existing tests.
- **`<math.h>::isnan`** in the entropy-overflow path — this is what
  v0.2.66-g3's math-builtin prototype fix enables. (Without
  v0.2.66, the `isnan()` calls in xmlparse.c's hash entropy code
  would silently return wrong values via the ABI-coincidence trap;
  the parser would still appear to work but with degraded entropy
  diversity. Out of scope to characterize here — but pinned by
  v0.2.66 either way.)
- **Recursive descent / state stacks** — distinct from
  awk's table-driven parser.
- **Conditional inclusion of headers (`HAVE_*` macros) driven by
  autoconf** — first demo that relies on `./configure` having
  given the right answer about libSystem's contents.

## Open work for next session

See [`HANDOFF.md`](HANDOFF.md). Summary: the proper tcc fix for
the AC_CHECK_FUNCS trap (roadmap #12) is the cleanest known-target
follow-up; trying another real-world program (bc, libpng, m4, vim)
remains a viable high-variance bet.
