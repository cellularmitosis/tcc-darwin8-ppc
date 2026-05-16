# Session 089 — one-true-awk on Tiger PPC, surfaced a math-builtin ABI bug (2026-05-16)

## Arrival state

- HEAD on uranium: `e4f2402` (end of 088 docs commit), tag
  `v0.2.65-g3` shipped.
- imacg3 mirror in sync, `tcc` built, session 088 changes
  (`rodata_in_data`) present.
- Sanity check passed: `demos/v0.2.65-rodata-data-const.sh` PASS
  (three matching libSystem VAs at `0x900b13dc`, extrel for
  `_isalpha` inside `__DATA,__const`).
- tests2: 111/111, abitest cc 24 + tcc 24, FIXPOINT holds (per 088).

## Plan

Pick option (b) from 088's handoff: try another real-world
program — [one-true-awk](https://github.com/onetrueawk/awk),
Brian Kernighan's `awk`. Small (~8k lines), self-contained,
exercises a different shape of C than the lua / bzip2 / cjson /
sed / gzip / zlib stack already passing. Two outcomes equally
interesting: passes → validates v0.2.65 for a non-trivial
program with lots of static tables; breaks → a real bug to
triage.

## Tldr

It broke. Found a long-latent tcc bug nobody had tripped before:
**Tiger's `<math.h>` for PPC implements the `isinf` / `isnan` /
`isfinite` macros inline as `__builtin_fabs(x) == __builtin_inf()`
(etc.), but tcc never predeclared the prototypes for these
builtins.** Callers saw implicit-int return signatures, so on PPC
they read `r3` (garbage) instead of `f1` (the real double result).
Net effect: `isinf(0.0)` returned 1, `isinf(+inf)` returned 0,
every `isinf`/`isnan`/`isfinite` in user code was silently lying.

awk's `tran.c::get_inf_nan()` is called from `get_str_val()`
whenever a numeric Cell is stringified for output. So
`printf "\n" | awk '{print NF}'` printed `+inf` instead of `0`
under tcc-built awk — a clean smoking gun.

Fix: six lines in `tcc/include/tccdefs.h` adding `extern`
declarations for `__builtin_fabs` / `__builtin_fabsf` /
`__builtin_fabsl` / `__builtin_inf` / `__builtin_inff` /
`__builtin_infl` under the `__APPLE__` branch.

Shipped as **v0.2.66-g3**. tests2 111/111, abitest 24+24,
bootstrap FIXPOINT holds, all v0.2.12..v0.2.65 demos still
green; gcc-built and tcc-built one-true-awk now produce
byte-identical output on the REGRESS testsuite (22 `t.*` / `p.*`
divergences → 0).

## Work log

### 1. Baseline: build one-true-awk with gcc-4.0 on Tiger

Cloned `https://github.com/onetrueawk/awk` (tag `20260426`, HEAD
`5739fd7`) on uranium, pre-generated `awkgram.tab.{c,h}` with
bison-2.3 (host bison; Tiger ships bison-1.28 which doesn't grok
modern grammar features), rsync'd to imacg3
`/Users/macuser/tmp/awk-otb/`.

Build with `gcc-4.0 -O2`: clean, zero warnings. `./a.out --version`
prints `awk version 20260426`. Saved as `awk-gcc`. Ran the REGRESS
testsuite against Tiger's system awk as oldawk — to characterize
the inherent "one-true-awk vs Tiger BSD awk" divergences and have
a noise-floor baseline.

### 2. Build with tcc

Wrote a tiny `build-awk-tcc.sh` that builds `maketab` (host tool)
with gcc-4.0, runs it to generate `proctab.c`, then `tcc -c` each
of the 9 C files and links with `-lm`. Result:

- Zero compile errors.
- Two warnings on `tran.c`: `implicit declaration of function
  '__builtin_fabs'` and `'__builtin_inf'`. (Noted at the time;
  didn't immediately suspect them.)
- Resulting `awk-tcc` linked only against `libSystem.B.dylib`.
- `awk --version` works.

Saved as `awk-tcc`.

### 3. Direct gcc-vs-tcc REGRESS comparison

This is the gold-standard test: run REGRESS with
`oldawk=./awk-gcc awk=./awk-tcc`. The two awks are built from
identical source — only the compiler differs, so any output
divergence is a tcc codegen issue.

22 `t.*` / `p.*` BAD lines surfaced:

```
t.6.x:    BAD ...  2c2
t.NF:     BAD ...  6c6
t.arith:  BAD ...  1,8c1,8
t.builtins: BAD ... 8c8
t.cat:    BAD ...  8c8
t.coerce2: BAD ... 1c1
t.cum:    BAD ...  1,2c1,2
t.d.x:    BAD ...  2c2
t.getval: BAD ...  2c2
t.i.x:    BAD ...  130,138c130,138
t.incr2:  BAD ...  170,172c170,172
t.incr3:  BAD ...  1c1
t.j.x:    BAD ...  1,2c1,2
t.makef:  BAD ...  1,2c1,2
t.ofmt:   BAD ...  1,2c1,2
t.rec:    BAD ...  1,2c1,2
t.set0:   BAD ...  9c9
t.set0a:  BAD ...  2c2
t.set3:   BAD ...  1,2c1,2
t.split8: BAD ...  2c2
t.vf2:    BAD ...  1c1
p.44:     BAD ...  1,20c1,20
```

Real signal. Killed the long-running REGRESS (tt.big was eating
minutes per iteration) and zoomed in.

### 4. Reduce: `t.NF` minimal repro

`t.NF`'s program is just
`{ OFS = "|"; print NF; NF = 2; print NF; print; $5 = "five"; print NF; print }`.
Line 6 of output (for the empty record in `test.data`):

```
gcc:  0
tcc:  +inf
```

So for an empty record (NF=0), tcc-built awk prints NF as
`+inf`. Even smaller repro:

```
printf "\n" | awk '{print NF}'
```

gcc → `0`. tcc → `+inf`.

### 5. Locate: `get_inf_nan` in tran.c

`tran.c::get_str_val` calls a helper `get_inf_nan(vp->fval)` that
returns `"+inf"` if `isinf(d)`, `"-inf"` if `isinf(d) && d<0`,
`"+nan"` / `"-nan"` for nan, or NULL otherwise. So `isinf(0.0)`
must be returning true under tcc.

### 6. Confirm: `isinf(0.0)` returns 1 under tcc, 0 under gcc

Tiny test program:

```c
#include <stdio.h>
#include <math.h>
int main(void) {
    double zero = 0.0;
    double inf = 1.0 / 0.0;
    printf("isinf(0.0) = %d (expect 0)\n", isinf(zero));
    printf("isinf(inf) = %d (expect 1)\n", isinf(inf));
    return 0;
}
```

Built with tcc:
```
isinf(0.0) = 1 (expect 0)
isinf(inf) = 0 (expect 1)
```

Built with gcc-4.0:
```
isinf(0.0) = 0 (expect 0)
isinf(inf) = 1 (expect 1)
```

**Both inverted under tcc.** Plus the warnings about
`__builtin_fabs` / `__builtin_inf` from compile-time fire again.

### 7. Diagnose: `<math.h>` inline + implicit-int return

`/usr/include/architecture/ppc/math.h`:

```c
#define isinf(x) \
    (sizeof(x) == sizeof(float)  ? __inline_isinff((float)(x))  \
   : sizeof(x) == sizeof(double) ? __inline_isinfd((double)(x)) \
                                 : __inline_isinf((long double)(x)))

static __inline__ int __inline_isinfd(double __x) {
    return __builtin_fabs(__x) == __builtin_inf();
}
```

gcc-4.0 treats `__builtin_fabs` / `__builtin_inf` as compiler
intrinsics — inlines them, no function call. tcc emits a regular
function call, and libtcc1.a `lib-ppc.c` (since v0.2.21)
provides:

```c
double __builtin_fabs(double x) { return x < 0 ? -x : x; }
double __builtin_inf(void) {
    union { double d; unsigned long long u; } u;
    u.u = 0x7FF0000000000000ULL;
    return u.d;
}
```

So the implementations exist and are correct. **The bug is that
tcc didn't predeclare their prototypes.** The user's TU sees
`__builtin_fabs(__x)` called without a declaration in scope;
tcc treats undeclared functions as `int (*)()`. The Apple PPC
calling convention puts doubles in `f1` and ints in `r3` — the
caller, expecting an int, reads `r3` (garbage), and compares
that garbage to whatever `__builtin_inf()` returned (also read
from `r3`). Coincidence makes the result stable per call site
but completely wrong.

Verified by writing a version with explicit prototypes:

```c
double __builtin_fabs(double);
double __builtin_inf(void);
```

— with these declared, the comparison works correctly.

The v0.2.21 commit `2eca269` added the libtcc1.a wrappers for
`__builtin_{isnan,isinf,isfinite}` but missed the
double-returning `__builtin_{fabs,inf}` family. The `isnan`/
`isinf` wrappers happen to return `int`, which matches tcc's
implicit-int assumption, so they appeared to work by ABI
coincidence; the double-returning ones didn't get the same
luck.

### 8. Fix: six lines in `tccdefs.h`

`tcc/include/tccdefs.h`, in the `__APPLE__` branch right after
the existing `__builtin_bzero` macro definition:

```c
/* Predeclare math builtins used by Tiger's <math.h> inline
 * isinf/isnan/isfinite path. gcc treats these as intrinsics
 * with FP return types; tcc emits a regular call. Without an
 * explicit prototype, tcc assumes implicit-int return — on PPC,
 * callers then read r3 instead of f1 and get garbage, causing
 * isinf(0.0) → true and similar nonsense. The symbols are
 * provided by libtcc1.a (lib-ppc.c). */
extern double __builtin_fabs(double);
extern float __builtin_fabsf(float);
extern long double __builtin_fabsl(long double);
extern double __builtin_inf(void);
extern float __builtin_inff(void);
extern long double __builtin_infl(void);
```

Built `tcc` fresh on imacg3 (the `tccdefs_.h` regen via
`c2str.exe` worked transparently). Retested the isinf program:

```
isinf(0.0) = 0 (expect 0)
isinf(inf) = 1 (expect 1)
```

Rebuilt awk-tcc with the patched tcc. `printf "\n" | awk-tcc
'{print NF}'` now prints `0`. ✓

### 9. Verify: REGRESS gcc-vs-tcc parity

Reran REGRESS with `oldawk=./awk-gcc awk=./awk-tcc` and the
fixed awk-tcc. **Zero `t.*` / `p.*` BAD lines** (was 22). The
70 remaining "test N.M failed" lines (from T.utf, T.regexp,
etc.) are identical to the gcc-baseline-vs-Tiger-system-awk run
— inherent test issues (T.utf locale, T.overflow ulimit, T.argv
hardcoded `../a.out` path), not tcc-related.

### 10. Run tcc's own regression suite

| Check | Result |
|---|---|
| tests2 | 111/111 PASS |
| FIXPOINT (tcc-self2.o == tcc-self3.o) | HOLDS |
| abitest-cc | 24/24 success |
| abitest-tcc | 24/24 success |
| demos: v0.2.65-rodata-data-const | PASS |
| demos: v0.2.64-funcptr-const | PASS |
| demos: v0.2.40-sed | PASS |
| demos: v0.2.41-gzip | PASS |

No regressions.

### 11. Demo

`demos/v0.2.66-awk.sh` — downloads one-true-awk tag `20260426`
from GitHub, installs bison-3.8.2 via `tiger.sh` if missing,
regenerates `awkgram.tab.{c,h}`, builds with tcc as CC, asserts
only `libSystem.B.dylib` is linked, runs five smoke tests:

- `printf "\n" | awk '{print NF}'` → `0` (was `+inf` pre-fix)
- integer sum (`{s+=$1} END {print s}`) → `15`
- `printf "%.6g %.6g %.6g\n", 0, 1.5, 3.14159265` → `0 1.5 3.14159`
- gsub regex → `hellO wOrld`
- associative array (`c[$1]++`) → `apple=3 banana=2 cherry=1`

All PASS.

## What landed

- [`tcc/include/tccdefs.h`](../../../tcc/include/tccdefs.h) —
  six `extern` declarations under `__APPLE__` for the math
  builtins. `tccdefs_.h` regenerates automatically.
- [`demos/v0.2.66-awk.sh`](../../../demos/v0.2.66-awk.sh) — new
  real-world demo.
- [`demos/README.md`](../../../demos/README.md) — row for
  v0.2.66.
- [`docs/roadmap.md`](../../../docs/roadmap.md) — v0.2.66-g3
  row, count bumped to forty-four patch releases, "Real-world
  program builds" item updated to include awk.

## Takeaways

1. **Implicit-int return is a silent footgun on PPC.** When a
   double-returning function is called without a prototype, tcc
   emits the int calling convention. The callee writes the
   double to `f1`; the caller reads `r3` (untouched). No link
   error, no warning past the implicit-declaration one, just
   garbage values that propagate downstream. Worth keeping an
   eye out for analogous patterns: any system header that
   inlines via `__builtin_*` math intrinsics could trip the
   same shape of bug.

2. **gcc-vs-tcc direct comparison is high-value.** A REGRESS
   run with `oldawk=./awk-gcc awk=./awk-tcc` (rather than vs
   Tiger system awk) produced an unambiguous 22 vs 0 divergence
   count before and after the fix — no noise from
   awk-implementation differences. Worth using as the default
   shape for future real-world-program-on-tcc tests when an
   alternative compiler exists.

3. **`+inf` from "print 0" is a memorable signature.** When
   awk's tran.c calls `get_inf_nan(0.0)` and gets back "+inf",
   the only way that's possible is if `isinf(0.0)` lies. From
   there the trail was short. Generalizable: noticing a
   nonsensical value in stringified output is often a faster
   path to a numeric/ABI bug than chasing through the call
   stack.

4. **The "appears to work by ABI coincidence" trap.** The
   v0.2.21 commit added wrappers for `__builtin_isnan` /
   `__builtin_isinf` (int return) but not `__builtin_fabs` /
   `__builtin_inf` (double return). The session 053 sed-and-gzip
   investigation never tripped this because the
   isnan/isinf-from-gnulib path returns int either way — the
   int callers got correct values from `r3` because
   `__builtin_isnan` actually returns an int. The
   double-returning math builtins waited five months for a real
   program (awk) to actually pipe their output through a
   printf-style stringifier and produce a visibly-wrong result.
