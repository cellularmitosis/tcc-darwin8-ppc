# Findings — session 092 (GNU bc with tcc, no source change)

## 1. Cross-validating tcc-built code against Tiger's stock-bc-1.06 is unusually strong on this box

Tiger's `/usr/bin/bc` is GNU bc 1.06 (2005, compiled by Apple's
gcc-3.3). It's the same upstream codebase as bc-1.07.1 — the 1.07
series adds a couple of minor language extensions and bug fixes,
but every arithmetic primitive, the libmath series implementations
for `sqrt`/`atan`/`sin`/`cos`/`exp`/`log`, the arbitrary-precision
representation, the I/O are identical.

Building bc-1.07.1 with tcc and diffing its output against
`/usr/bin/bc` 1.06 on the same input therefore isolates the
**compiler** as the only variable. Same source, same libmath
constants, same FP runtime (Tiger libSystem's `<math.h>`), same
expectation of what each line should print.

Concretely: 30 lines of arbitrary-precision output, byte-identical.
Any tcc codegen bug in the int / long / double / call / loop /
recursion / pointer-deref / arbitrary-precision math paths would
show up as at least one digit's divergence somewhere in those 30
lines. None did. This is the deepest end-to-end correctness
statement for tcc on Tiger PPC to date.

**How to apply:** when picking future real-world demos, lean
toward programs Tiger already ships a binary of (bc, awk, sed,
gzip, ...). Cross-validating tcc-built `<thing>` against
Apple-gcc-built `/usr/bin/<thing>` on shared inputs is a strictly
stronger correctness signal than smoke tests against author-
provided expected values, and zero code to write — just diff.

## 2. `_doprnt` is a load-bearing autoconf-honesty canary on Tiger

`_doprnt` is a System V Release 3-era variadic printf backstop.
Apple's Darwin libc does NOT ship it (it goes through `vfprintf`
internally, exposed via `vprintf`). Autoconf's
`AC_FUNC_VPRINTF` macro probes both `vprintf` AND `_doprnt`,
defining `HAVE_VPRINTF` or `HAVE__DOPRNT` to drive a code path
choice in programs that want maximum portability across very
old Unixes.

Before v0.2.67-g3, tcc on Tiger said yes to `_doprnt` (because
the conftest binary linked successfully — undef went into the
nlist with no resolution check). Post-v0.2.67, tcc correctly
rejects the conftest link because `_doprnt` isn't in libSystem's
dynsymtab, so AC_FUNC_VPRINTF picks the (correct) vprintf path.

**How to apply:** when verifying a new autoconf-driven demo
actually exercises the v0.2.67 fix, grepping its `config.h` for
`/* #undef HAVE__DOPRNT */` is the fastest single test. If
HAVE__DOPRNT is defined, either (a) the demo isn't running tcc
through configure correctly, or (b) v0.2.67's fix has regressed.

## 3. The "config.h sanity" pattern for autoconf-driven demos

The v0.2.67-bc.sh demo includes an assertion block right after
configure:

```sh
grep '^#define HAVE_VPRINTF 1' config.h   # must be present
grep '^#define HAVE__DOPRNT 1' config.h   # must NOT be present
grep '^#define HAVE_LIB_H 1'  config.h    # must NOT be present
```

This catches the "demo ran with -Werror or some other shim that
forced the right answers" regression mode separately from the
"build worked and tests pass" path. If a future contributor
adds a CFLAGS shim back, the demo PASSes the smoke tests but
FAILs the config.h sanity block — making the regression visible.

**How to apply:** every future autoconf-driven demo should
include at least one positive and one negative `config.h`
assertion, sourced from `configure`'s detection of symbols that
are genuinely present vs. genuinely absent on Tiger libSystem.
Otherwise the demo can silently regress to using a shim.

## 4. Tolerance checking for floating-point transcendentals in bc

For `e(l(7))` (should round-trip to 7) and `s(x)² + c(x)² = 1`,
the exact last digit at a given `scale` depends on bc's internal
rounding convention — it's "correct up to the requested scale"
but not "correctly rounded at the boundary." So an exact-string
match is fragile (off by 1 in the last digit on different inputs).

Workaround: compute `|result - expected| < 10⁻³⁰` *in bc itself*
inside the same heredoc, asserting the difference is small:

```sh
echo "scale=40; r = e(l(7)); if (r > 7) d = r - 7 else d = 7 - r;
      if (d < 0.000000000000000000000000000001) 1 else 0" | bc -l
```

Returns `1` on success, `0` on failure. Trivially compared as
shell-string `[[ "$CLOSE" == "1" ]]`.

**How to apply:** for any future transcendental-comparison
demo, prefer in-bc tolerance assertions over exact-digit string
matches. The 10⁻³⁰ tolerance is appropriate for scale-40
calculations; widen to 10⁻⁵⁰ for scale-60 calculations.

## 5. The bc Test/ directory isn't a self-validating suite

GNU bc ships a `Test/` subdir with files like `sqrt.b`, `sine.b`,
`exp.b`, etc., but they are **timing inputs**, not test inputs.
`Test/timetest` runs each .b file through bc and prints the final
value — there's no expected-output file, no PASS/FAIL counter,
no `make check` target ("Nothing to be done for 'check'").

This is in contrast to expat's 345-test internal suite, which is
a real testsuite. The bc demo's smoke tests had to be hand-rolled
plus the cross-validation against /usr/bin/bc — which turned out
to be a feature, not a bug (see finding #1).

**How to apply:** when probing a new autoconf program, don't
assume Test/ or tests/ subdirs are PASS/FAIL suites. Inspect
the Makefile's `check` target before relying on `make check`.

## 6. v0.2.66-expat's `-Werror=implicit-function-declaration` workaround is now demonstrably superfluous

Session 091's HANDOFF predicted this; session 092 confirms it
on a fresh autoconf project. The existing `v0.2.66-expat.sh`
demo still works (because `-Werror=implicit-function-declaration`
operates at compile time, not link time — orthogonal to the
v0.2.67 link-time check), so it's harmless to leave in place as
a historical snapshot. The expat workaround is now belt-and-
braces, not load-bearing.

**How to apply:** new autoconf demos do not need the
`-Werror=implicit-function-declaration` flag. If a future
maintainer is touching v0.2.66-expat.sh for unrelated reasons,
dropping the flag + updating the header comment to point at
v0.2.67-g3 is a clean one-line edit.

## 7. No tcc source change needed for the 11th real-world program

bc-1.07.1 was the first real-world program in the post-v0.2.66
series to build cleanly with no tcc source change required. Each
previous one (sed, gzip, Python, awk, expat) surfaced at least one
tcc bug — either pre-existing-but-newly-tripped (gnu_inline, PIC
SECTDIFF, math builtin prototypes) or genuinely new
(`isinf`/`isnan` calling convention, AC_CHECK_FUNCS false
positives).

bc-1.07.1 just works. This is itself information: the recent
fixes have stabilized the autoconf-driven-C-program surface
on Tiger PPC enough that not every new program brings a new bug.

**How to apply:** when picking the next real-world program,
expect heterogeneity. bc passing on the first try doesn't
imply m4 / libpng / vim / gawk will. But it does mean the
"baseline failure modes" (math builtin prototypes, AC_CHECK_FUNCS
false positives) have been swept up — the next bug is more
likely to be a fresh class.
