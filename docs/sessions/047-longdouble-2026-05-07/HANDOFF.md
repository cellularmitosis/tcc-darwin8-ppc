# Handoff — end of session 047 (2026-05-07)

## TL;DR

One release shipped this session: **v0.2.32-g3**. Headline:

* **`long double` is now real IBM double-double** on tcc-Apple-PPC
  (was an 8-byte double impostor since v0.2.0).
* **abitest-cc 22/24 → 24/24** — the last two persistent failures
  (`ret_longdouble_test`, `arg_align_test`) are gone.
* abitest-tcc still 24/24, tests2 still 111/111, dlltest still
  passes, bootstrap fixpoint still holds.

* HEAD: `2910ab3`.
* All commits and `v0.2.32-g3` tag on `origin/main`.

## What landed since v0.2.31 (start of this session)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.32-g3 | `6da373b`..`2910ab3` (5) | 111/111 | **`long double` = IBM double-double on Apple PPC.** `LDOUBLE_SIZE 8 → 16, ALIGN 4`; `R2_RET(VT_LDOUBLE)=REG_FRE2`; gv()/vstore() take 8-byte stride for LD halves. Codegen routes LD `+ - * /` through `__gcc_q{add,sub,mul,div}` and compares through `__{eq,ne,lt,le,gt,ge}tf2`. ABI: LD args spill to outgoing param area then `lfd` into ABI FPR pair (decouples regalloc from ABI placement); LD return = (f1,f2). `gen_cvt_itof` zero-fills LD low half; `gen_cvt_ftof` handles LD↔double inline. Predefines `__LDBL_MANT_DIG__=106` and `__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=1040` so Tiger's `<sys/cdefs.h>` redirects `printf`/`scanf` to `*$LDBL128` (the 16-byte LD libSystem entries). `lib-ppc.c` grew long-double helpers (lossy double-fallback) plus LL↔LD conversion stubs. **abitest-cc now 24/24.** Verified by [`v0.2.32-longdouble.sh`](../../../demos/v0.2.32-longdouble.sh). |

## Real-world programs

| Program | Demo | Status |
|---|---|---|
| Lua 5.4.7 | [`v0.2.12-lua.sh`](../../../demos/v0.2.12-lua.sh) | ✅ |
| zlib 1.3.1 | (no demo) | ✅ |
| bzip2 1.0.8 | [`v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) | ✅ |
| cJSON 1.7.18 | [`v0.2.21-cjson.sh`](../../../demos/v0.2.21-cjson.sh) | ✅ |
| HTTP server | [`v0.2.21-httpd.c`](../../../demos/v0.2.21-httpd.c) | ✅ |
| sqlite3 (file) | [`v0.2.23-sqlite-file.sh`](../../../demos/v0.2.23-sqlite-file.sh) | ✅ |
| Tiger libz via tcc -lz | [`v0.2.26-link-dylib.sh`](../../../demos/v0.2.26-link-dylib.sh) | ✅ |
| libtcc as dylib + slide | manual | ✅ |
| **LD ABI tcc-vs-gcc match** | [`v0.2.32-longdouble.sh`](../../../demos/v0.2.32-longdouble.sh) | ✅ |

## New demos this session

* [`v0.2.32-longdouble.sh`](../../../demos/v0.2.32-longdouble.sh) —
  builds an LD-stress program (LD param/return; LD interleaved with
  ints; 4 LD args via printf with `%Lf`; LD comparisons) with both
  tcc and gcc-4.0 and verifies byte-identical output.

## Upstream `make test` snapshot

| Stage | Status | Note |
|---|---|---|
| version | ✅ | |
| hello-exe | ✅ | |
| hello-run | ✅ | |
| libtest | ❌ | `R_PPC_REL24 out of range` between JIT'd code and loaded helpers — **pre-existing**, reproduces with current session's diff stashed. Session 046 HANDOFF mistakenly listed this as passing. |
| libtest_mt | ❌ | "running fib in threads" — multi-threaded JIT compilation. tcc's libtcc thread safety is disabled at build time (CLAUDE.md: out of scope). |
| test3 | ❌ | Now has *different* known content diffs (LD subnormal output) since LD codegen is real. tcctest itself runs to completion. |
| abitest-cc | ✅ | **24/24** ← this session's win. |
| abitest-tcc | ✅ | 24/24 |
| vla_test-run | ✅ | |
| tests2-dir | ✅ | 111/111 |
| pp-dir | ✅ | 24/24 |
| memtest | ✅ | |
| dlltest | ✅ | |
| cross-test | ❌ | Needs `dispatch/dispatch.h`, not on Tiger. Out of scope. |
| btest / test1b / tccb | ❌ | Need real `bcheck.c` port. Multi-session. |

## Open work (in roughly decreasing impact)

### A. Closed this session

* ~~**B1 — Apple PPC IBM double-double long double**~~ ✅ Done in v0.2.32.

### B. Bugs / quality items not addressed this session

1. **libtest `R_PPC_REL24 out of range`**. NOT new — pre-existed
   our LD work. The `libtcc_test` JIT-compile-of-tcc.c path mmaps
   the JIT region too far from the libtcc1.a helper region for the
   24-bit `bl` to reach. Need to bisect against history; the
   session 046 HANDOFF claim that libtest was passing appears to
   have been wrong. Likely related to tcc's `collect_extern_stubs`
   not covering some symbol or some address-layout drift after
   recent changes.

2. **Precision-correct double-double arithmetic.** Our `__gcc_q*`
   and tf2 helpers are LOSSY (high-double only). Real
   precision-preserving versions would do the standard "compensated"
   Knuth-style double-double algorithms. abitest values fit in
   double's 53-bit mantissa so this isn't visible there.

3. **libtcc thread safety** (was OOS per CLAUDE.md; would unblock
   libtest_mt's "running fib in threads").

### C. Structural items

4. **DWARF debug info for PPC** (item #8). `tccdbg.c` has no PPC
   blocks. Multi-target plumbing. Unblocks `gdb`/`lldb` debugging.

5. **Real bcheck.c port** (item #11). Stubs satisfy link but don't
   actually bounds-check. Unblocks `-b` instrumented builds and the
   `btest`/`test1b`/`tccb` upstream stages. Multi-session.

6. **AltiVec intrinsics** (item #9). None today; tcc emits scalar
   code.

7. **Self-link diagnostics** (item #7). When the EXE writer fails
   the user gets `dyld` errors with no context.

### D. Two-level namespace polish

When extra dylibs are loaded, v0.2.26's writer switches to flat
namespace (clears MH_TWOLEVEL). Strict two-level requires per-symbol
ordinal tracking (side-table mapping each undef sym → source dll).
Future polish.

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.32-longdouble.sh                 # PASS
./tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc  # 24/24
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-tcc # 24/24
```

### Verify the LD redirect works for a more elaborate case

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
cat > /tmp/ldhog.c << 'EOF'
#include <stdio.h>
int main(void) {
    long double a = 0x1.fffffffffffffp+50L; // edge of double precision
    long double b = a + 1.0L;
    long double c = a * a;
    printf("a    = %.30Lf\n", a);
    printf("a+1  = %.30Lf\n", b);
    printf("a*a  = %.30Lf\n", c);
    return 0;
}
EOF
./tcc/tcc -B./tcc -L./tcc -I./tcc/include /tmp/ldhog.c -o /tmp/ldhog
/tmp/ldhog
gcc-4.0 /tmp/ldhog.c -o /tmp/ldhog_gcc && /tmp/ldhog_gcc
# Output diverges — gcc gets full double-double precision; tcc's
# lossy helpers truncate. Acceptable until item B2 lands.
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents used in session 047. The work was a single tightly-
coupled investigation (LD ABI on PPC) that benefited from
accumulated context, especially around the `$LDBL128`
symbol-redirect discovery — that bug took chasing several layers of
indirection (parent-to-child stack layout, va_arg behavior, then
finally Tiger's ABI versioning of printf) and would have been hard
to brief into a fresh subagent.

## Closing notes

This release closes the last persistent abitest-cc failure —
a 26-month-old item that was tagged as "multi-session lift" in the
session 043 + 046 HANDOFFs. Single session in the end, but only
because the right design choice (option A: register pair via
`USING_TWO_WORDS`) fit cleanly into existing tcc machinery and the
two non-obvious bugs (the `save_reg_upstack` stride mismatch and
the `$LDBL128` symbol redirect) were chased the moment they
surfaced rather than worked around.

The lossy `__gcc_q*` and `tf2` helpers are the obvious next polish
target — they're functionally correct for any LD value that fits in
double and they pass abitest, but real codebases that genuinely use
double-double precision (rare but they exist) will see truncation.
A proper port can come from the standard libgcc soft-float
double-double routines.
