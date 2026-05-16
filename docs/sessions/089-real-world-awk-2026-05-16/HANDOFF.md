# Handoff — end of session 089 (2026-05-16)

## TL;DR

**v0.2.66-g3 shipped.** Brian Kernighan's `awk` (one-true-awk)
now builds and runs correctly with tcc as CC on Tiger PPC —
ninth real-world program after lua, zlib, bzip2, cJSON, sqlite,
sed, gzip, Python.

The fix: Tiger's `<math.h>` implements `isinf()` / `isnan()` /
`isfinite()` as macros that inline to `__builtin_fabs(x) ==
__builtin_inf()` (etc.). tcc emits a regular function call into
libtcc1.a's wrappers, but never predeclared the prototypes — so
callers saw implicit-int return and on PPC read `r3` instead of
`f1`. Every `isinf` / `isnan` / `isfinite` in user code was
silently lying. awk's `tran.c::get_inf_nan()` is called from
`get_str_val()` whenever a numeric Cell is stringified, so
`printf "\n" | awk '{print NF}'` printed `+inf` under tcc-built
awk (gcc: `0`).

Fix: six `extern` declarations in `tcc/include/tccdefs.h`'s
`__APPLE__` branch for `__builtin_fabs{,f,l}` / `__builtin_inf{,f,l}`.
The libtcc1.a wrappers from v0.2.21 keep providing the symbols;
this just tells tcc the right calling convention.

- **HEAD at session start:** `e4f2402` (end of session 088).
- **Tag:** `v0.2.66-g3`.
- **tests2:** 111 / 111 (unchanged).
- **abitest:** cc 24 + tcc 24 (unchanged).
- **Bootstrap fixpoint:** holds.
- **REGRESS gcc-vs-tcc on one-true-awk:** 22 `t.*` / `p.*`
  divergences → 0. Identical output on every comparison the
  test corpus performs.
- **Real-world demos verified:** lua, bzip2, cjson, sed (incl
  `[[:class:]]`), gzip, dylib-slide, alacarte, gdebug-extern,
  gdebug-link, v0.2.63-extern-data-ref, v0.2.64-funcptr-const,
  v0.2.65-rodata-data-const, v0.2.66-awk (new). All PASS.

## What landed

### Source

[`tcc/include/tccdefs.h`](../../../tcc/include/tccdefs.h) —
under the `__APPLE__` branch, right next to the existing
`__builtin_huge_val()` / `__builtin_nanf` / `__builtin_bzero`
macro predefs:

```c
extern double __builtin_fabs(double);
extern float __builtin_fabsf(float);
extern long double __builtin_fabsl(long double);
extern double __builtin_inf(void);
extern float __builtin_inff(void);
extern long double __builtin_infl(void);
```

The wrapper functions in `lib/lib-ppc.c` (untouched this
session — added in v0.2.21) keep providing the link symbols.
`tccdefs_.h` regenerates automatically via `c2str.exe`.

### Docs

* [`docs/roadmap.md`](../../../docs/roadmap.md) — new v0.2.66-g3
  row, bump "shipped through" + release counter, add awk to the
  "Real-world program builds" #10 item.
* [`docs/sessions/089-real-world-awk-2026-05-16/README.md`](README.md) —
  full narrative including the `+inf`-from-`NF=0` smoking gun
  and the implicit-int-return ABI trap diagnosis.
* This `HANDOFF.md`.

### Demos

* [`demos/v0.2.66-awk.sh`](../../../demos/v0.2.66-awk.sh) —
  new. Downloads one-true-awk tag `20260426`, installs bison-3.8.2
  via `tiger.sh` if missing, regenerates `awkgram.tab.{c,h}`,
  builds awk with tcc as CC, asserts only `libSystem.B.dylib`
  is linked, runs five smoke tests including the smoking gun
  (`printf "\n" | awk '{print NF}'` should print `0`, was `+inf`
  pre-fix).
* [`demos/README.md`](../../../demos/README.md) — table row
  added for v0.2.66.

### Memory updates

None.

## Status of session 088's open items

| # | Item | Status |
|---|---|---|
| (088 #1, carried 086 #2 / 087 #2) | Dylib writer extrel emission | unchanged — still deferred |
| (088 #2, carried 067 #3) | Sibling r11 watch | unchanged |
| (088 #3, carried) | AltiVec intrinsics | unchanged |
| (088 #4, carried) | bcheck.c port | unchanged |
| (088 #5, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (088 #6, optional, carried) | `rsync` exclude-list polish | unchanged |
| (089 NEW) | Math-builtin prototypes for `<math.h>` inline path | **CLOSED → v0.2.66-g3** |

## Open work for next session

### 1. Dylib writer extrel emission (carried, 088 #1)

`macho_output_dylib` still falls back to slot-address or stub-VA
for undef ADDR32 refs in dylib-resident `__const`. The dylib
path didn't pick up v0.2.63's `.data` extrel emission or
v0.2.65's `__const`-to-`__DATA` routing. Same harder-than-it-
sounds set of issues:
* Dylibs are slid by dyld; `r_address` semantics differ.
* The bind would happen relative to the dylib's load address.
* Tiger's dyld treats `VANILLA` in `MH_DYLIB` with slide-aware
  semantics — needs empirical verification.

No in-tree dylib trips this today. Easiest way to make a repro
is to put a function-pointer table in a dylib's `static const`
and try to call through it from a `dlopen`'d driver.

### 2. Sibling r11 watch (carried, 067 #3)

PPC sibling-register concern. csmith hasn't surfaced a repro yet.

### 3. Try another real-world program (suggested follow-up)

v0.2.66's math-builtin fix touches a code path that lots of
programs use (`<math.h>` inline `isinf`/`isnan`/`isfinite`).
Worth a sweep through programs that exercise floats more than
the current demo set does — candidates:

* **`bc`** (GNU bc, arbitrary-precision calculator) — heavy
  floating-point and integer arithmetic via libgmp or libdecnumber.
* **`expat`** — XML parser, small and self-contained, exercises
  a different shape of C than the current demos.
* **`libpng`** — image library, exercises lots of integer +
  endian + table-driven code.
* **`one-true-awk` with the full REGRESS suite enabled** —
  v0.2.66's smoke tests are not exhaustive; running the full
  `./REGRESS` (~15 minutes on a G3) gives a comprehensive
  regression baseline.

### 4. AltiVec intrinsics (carried)

PowerPC SIMD. Multi-session lift, low priority.

### 5. `bcheck.c` port (carried)

PowerPC port of tcc's bounds-check runtime. Multi-session lift.

### 6. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3

### 7. (optional, low priority) `rsync` exclude-list polish

## How to pick up

### Quick sanity check on imacg3

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.66-awk.sh
./demos/v0.2.65-rodata-data-const.sh
./demos/v0.2.40-sed.sh
```

### Verify the fix end-to-end

```sh
ssh imacg3
T=/Users/macuser/tcc-darwin8-ppc/tcc
cat > /tmp/isinf-check.c <<'EOF'
#include <stdio.h>
#include <math.h>
int main(void) {
    double zero = 0.0;
    double inf = 1.0 / 0.0;
    printf("isinf(0.0) = %d (expect 0)\n", isinf(zero));
    printf("isinf(inf) = %d (expect 1)\n", isinf(inf));
    return 0;
}
EOF
$T/tcc -B$T -L$T -I$T/include -o /tmp/isinf-check /tmp/isinf-check.c
/tmp/isinf-check
# Expected: isinf(0.0) = 0, isinf(inf) = 1.
```

### Pick a direction

(a) **Dylib writer extrel emission** — the long-deferred
parallel work to v0.2.63 / v0.2.65. Cleanest follow-up but
harder than EXE because dylibs slide; needs empirical Tiger
dyld verification. No in-tree dylib trips this today.

(b) **Try another real-world program** — `bc`, `expat`,
`libpng`, `m4`, `make`, `vim`. v0.2.66 expanded the
known-working set; more programs might surface new bugs OR
confirm broad correctness.

(c) **Full one-true-awk REGRESS as a recurring check** — wire
the REGRESS testsuite into a script under `scripts/` so future
sessions can re-baseline against awk. ~15 minutes on a G3.

(d) **AltiVec intrinsics** — PowerPC SIMD. Multi-session lift,
low priority unless a specific program needs it.

(a) is the cleanest known-target follow-up. (b) is the
high-variance bet. Neither blocks the other.

## Subagent log

None this session — direct edits with iterative verification on
imacg3.

## Closing notes

Three takeaways worth carrying forward (also in the session
README):

1. **Implicit-int return is a silent footgun on PPC.** When a
   double-returning function is called without a prototype, tcc
   emits the int calling convention. The callee writes the
   double to `f1`; the caller reads `r3` (untouched). No link
   error, no warning past the implicit-declaration one, just
   garbage values that propagate downstream. Worth keeping an
   eye out for analogous patterns: any system header that
   inlines via `__builtin_*` math intrinsics could trip the
   same shape of bug. Particular places to audit: `<sys/types.h>`,
   `<float.h>`, `<complex.h>`, anything that uses
   `__builtin_choose_expr` or `__builtin_classify_type`.

2. **gcc-vs-tcc direct comparison is high-value.** A REGRESS
   run with `oldawk=./awk-gcc awk=./awk-tcc` (both built from
   identical source) produces an unambiguous divergence count
   — no noise from awk-implementation differences vs Tiger
   system awk. Worth using as the default shape for future
   real-world-program-on-tcc validation when an alternative
   compiler exists. Going forward: for any program whose own
   testsuite supports it, run the testsuite with the new
   compiler's binary substituted for a known-good binary and
   compare outputs directly.

3. **The "appears to work by ABI coincidence" trap.** The
   v0.2.21 commit added libtcc1.a wrappers for `__builtin_isnan`
   / `__builtin_isinf` (int return — happens to match tcc's
   implicit-int assumption) but missed `__builtin_fabs` /
   `__builtin_inf` (double return — ABI mismatch on PPC). The
   isnan/isinf path *appeared* to work because int callers got
   correct values from `r3` by ABI coincidence; the
   double-returning ones waited five months for a real program
   (awk) to pipe their output through a printf-style stringifier
   and produce a visibly-wrong result. **Take-away:** when
   adding wrapper functions for compiler builtins, always
   predeclare prototypes alongside; don't rely on implicit-int.

Next session: [docs/sessions/089-real-world-awk-2026-05-16/HANDOFF.md](docs/sessions/089-real-world-awk-2026-05-16/HANDOFF.md)
