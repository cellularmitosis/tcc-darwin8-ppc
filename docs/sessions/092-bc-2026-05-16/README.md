# Session 092 — GNU bc with tcc on Tiger PPC (2026-05-16)

## Arrival state

- HEAD: `fd52d81` — end of session 091, post-v0.2.67-g3.
- v0.2.67-g3 closed roadmap item #12: PPC EXE writer validates
  undefined external symbols against linked dylibs at link time.
  Autoconf's `AC_LINK_IFELSE` / `AC_CHECK_FUNCS` machinery now
  returns honest answers — confirmed in 091 by re-running expat's
  configure without the `-Werror=implicit-function-declaration`
  shim and observing `HAVE_ARC4RANDOM_BUF` correctly undef'd.
- Session 091's HANDOFF recommended trying another real-world
  autoconf-driven program to validate the fix in the wild.
  Candidate list: `bc` / `m4` / `libpng` / `vim` / `gawk`.
- Picked **GNU bc 1.07.1** — directly exercises both recent fixes:
  - v0.2.66-g3's math-builtin prototype fix (bc -l does
    arbitrary-precision Newton/Taylor iteration for
    sqrt/atan/sin/cos/exp/log; FP correctness end-to-end is
    critical).
  - v0.2.67-g3's `AC_CHECK_FUNCS` fix (bc uses autoconf and probes
    `_doprnt`, `vprintf`, `isgraph`, `setvbuf`, `fstat`, ... — the
    exact surface that pre-v0.2.67 would have rubber-stamped).

## Goals

1. Build GNU bc 1.07.1 with tcc as CC, **default CFLAGS** (no
   `-Werror=implicit-function-declaration` shim — the v0.2.67 fix
   should make it unnecessary). This is the load-bearing
   "ordinary autoconf works" claim.
2. Confirm bc + dc each link only against `libSystem.B.dylib`
   (the "no `__libgcc_s_*` parasite, no `libmathCommon` leakage"
   invariant from prior real-world demos).
3. Cross-validate tcc-built bc against Tiger's stock
   `/usr/bin/bc` 1.06 on a battery of arbitrary-precision
   inputs (sqrt / atan / exp / log / sin / cos at scale 50, big
   integer, factorial, gcd) — the strongest correctness statement
   we can make without writing a reference oracle from scratch.
4. Land a demo `demos/v0.2.67-bc.sh`, a row in
   `demos/README.md`, and a follow-up mention in roadmap.md's
   v0.2.67-g3 row.

## Work log

### Step 1 — verify v0.2.67-g3 active on imacg3

`/Users/macuser/tcc-darwin8-ppc/tcc/tcc` dated 2026-05-16 03:03
(session 091 build). Sanity-checked with the HANDOFF's reproducer:

```sh
extern void definitely_not_a_real_function_qwerty(void);
int main(void) { definitely_not_a_real_function_qwerty(); return 0; }
```

```
tcc: error: undefined symbol '_definitely_not_a_real_function_qwerty'
tcc exit: 1
```

Pre-v0.2.67 this would have exit 0 with a runnable-but-doomed
binary; the v0.2.67 link-time check fires correctly. ✓

### Step 2 — download bc-1.07.1 on imacg3

```
/opt/tigersh-deps-0.1/bin/curl -fsSL \
    --cacert /Users/macuser/tmp/cacert-2026-03-19.pem \
    -o bc-1.07.1.tar.gz \
    https://ftp.gnu.org/gnu/bc/bc-1.07.1.tar.gz
```

420 KB tarball, unpacks to `/Users/macuser/tmp/v092-bc/bc-1.07.1/`.
Build deps already present on Tiger: bison (3.0.4 in /usr/local),
flex, ed.

### Step 3 — configure + build, default CFLAGS

```
PATH=/opt/make-4.3/bin:$PATH \
  CC="$TCC -B$TCCROOT -L$TCCROOT -I$TCCROOT/include" \
  ./configure
```

No `CFLAGS=-Werror=implicit-function-declaration` — this is the
v0.2.67 unlock we wanted to verify.

`make` ran clean (the only "noise" was makeinfo regenerating
.info files in `doc/`, which works because Tiger has makeinfo
4.7 stock).

Configure detected the right symbols:
- `_doprnt`: not present (correct — Tiger's libSystem only has the
  variadic `vprintf` family; `_doprnt` is a SysVR3 backstop that
  Darwin doesn't ship).
- `vprintf`: present (correct).
- `lib.h`: not present (correct — this is the `<lib.h>` from the
  BSD `liber.a` family, not on Tiger).
- `isgraph`, `setvbuf`, `fstat`, ...: present (correct).

This is the load-bearing finding — pre-v0.2.67 EVERY one of these
would have returned "present" because tcc emitted any undef into
the nlist regardless of resolution. The v0.2.67 link-time check
now makes the conftest binary fail to link for unresolved symbols,
which is exactly what autoconf's `AC_LINK_IFELSE` is testing.

### Step 4 — verify built binaries

```
bc/bc: Mach-O executable ppc, 678 KB
dc/dc: Mach-O executable ppc, 449 KB

otool -L bc/bc: /usr/lib/libSystem.B.dylib only
otool -L dc/dc: /usr/lib/libSystem.B.dylib only

bc --version: bc 1.07.1
```

Both binaries hit the "links only libSystem" invariant — the
LC_LOAD_DYLIB sub-library mechanism from 091 (`as_subdylib=1`
recursion suppressing `tcc_add_dllref`) holds for autoconf-driven
builds too.

### Step 5 — arithmetic smoke tests

All values match well-known references:

| Computation | Result | Notes |
|---|---|---|
| `2+2*3` | `8` | Precedence |
| `2^100` | `1267650600228229401496703205376` | Big-integer exponentiation |
| `50!` (recursion) | `30414093201713378043612608166064768844377641568960512000000000000` | Recursion + big int multiplication |
| `sqrt(2)` @ scale 60 | `1.414213562373095048801688724209698078569671875376948073176679` | Newton's method via libmath |
| `4*a(1)` @ scale 60 | `3.141592653589793238462643383279502884197169399375105820974944` | π via Machin-style atan series |
| `e(1)` @ scale 60 | `2.718281828459045235360287471352662497757247093699959574966967` | exp Taylor series |
| `e(l(7))` @ scale 40 | `6.9999999999999999999999999999999999999994` | log/exp round-trip; within 10⁻³⁰ of 7 |
| `s(1.234)² + c(1.234)²` @ scale 40 | `.9999999999999999999999999999999999999998` | Pythagorean identity; within 10⁻³⁰ of 1 |

Last two are validated by computing `|result - expected|` in bc
itself and asserting < 10⁻³⁰, since the exact last digit depends
on bc's internal rounding convention (which is allowed to be
"correct up to the requested scale, not necessarily correctly
rounded at the boundary").

### Step 6 — cross-validation against /usr/bin/bc 1.06

The strongest test. Tiger's `/usr/bin/bc` is GNU bc 1.06 (from
2005, compiled by Apple's gcc-3.3 on Tiger), the same upstream
codebase. Feeding both bc's the same 17-input script:

```
2^200
scale=50; sqrt(2)        ;  scale=50; sqrt(3)        ;  scale=50; sqrt(5)
scale=50; 4*a(1)         ;  scale=50; a(1/2)
scale=50; e(1)           ;  scale=50; e(2)
scale=50; l(2)           ;  scale=50; l(10)
scale=50; s(1)           ;  scale=50; c(1)
scale=50; s(π_value)     # tests sin(π) ≈ 0
define f(n) { if(n<=1) return 1; return n*f(n-1); }
f(40)
define g(a,b) { if(b==0) return a; return g(b, a%b); }
g(123456789, 987654321)  # Euclid gcd
scale=30; for(i=1;i<=10;i++) { sqrt(i) }
```

Output: **30 lines, bit-identical between tcc-built bc-1.07.1 and
stock /usr/bin/bc-1.06**. `diff -q` returns clean.

This is the deepest correctness statement to date for tcc on
Tiger PPC. The two bc's:
- Use the same upstream code paths (1.06 is a strict subset of
  1.07.1 for these functions).
- Use the same libmath constants and series implementations.
- Use the same FP runtime (Tiger libSystem's `<math.h>`).
- Differ only in their C compiler: `gcc-3.3` (Apple) vs. tcc
  (us).

Byte-identical output across 200-digit integers, 50-digit
transcendentals, big factorial, recursive gcd, and a loop. Any
codegen bug in tcc's int / long / double / call / loop /
recursion / pointer paths would show up as at least a one-digit
divergence somewhere in the 30 lines.

### Step 7 — wire in the demo

Files touched:
- New: [`demos/v0.2.67-bc.sh`](../../../demos/v0.2.67-bc.sh) — full
  demo script with the structure used by v0.2.66-expat: download
  → configure → make → linkage invariant → smoke tests → cross-
  validation. 10 sub-tests.
- [`demos/README.md`](../../../demos/README.md) — new row for
  v0.2.67-bc.sh slotted above v0.2.67-undef-check (newest-at-top
  convention).
- [`docs/roadmap.md`](../../roadmap.md) — appended a "Companion
  demo" mention to v0.2.67-g3's milestone-table row.
- This session's `README.md` / `findings.md` / `HANDOFF.md`.

End-to-end demo run on imacg3 (synced via tiger-rsync.sh): PASS
all 10 sub-tests.

## Exit state

- HEAD: post this session's docs commit.
- **No tcc source change in this session** — pure demo + docs
  work. Regression risk is zero by construction.
- v0.2.67-g3 remains the current release.
- bc-1.07.1 is now the 11th real-world program with a green
  demo in the tree (after lua, zlib, bzip2, cJSON, sqlite, sed,
  gzip, Python, awk, expat).
- The v0.2.66-expat demo's `-Werror=implicit-function-declaration`
  workaround is now demonstrably superfluous on a fresh autoconf
  project (this demo configures without it and gets honest answers).
  Cosmetic cleanup of v0.2.66-expat.sh is still optional / open
  (carried as item #7 in 091's HANDOFF; this session intentionally
  did not touch it — the existing demo serves as a historical
  snapshot).

## Findings worth carrying forward

See [`findings.md`](findings.md) — covers:

1. The cross-validation pattern: tcc-built bc-1.07.1 vs.
   /usr/bin/bc-1.06 on Tiger is unusually strong because it's
   the same upstream code, same libmath, same FP runtime —
   any divergence is unambiguous evidence of a tcc codegen bug.
2. `_doprnt` is a useful autoconf-honesty canary on Tiger
   (genuinely missing from libSystem; pre-v0.2.67 tcc would
   have accepted it).
3. The "what to look for" list for new autoconf-driven demos
   to confirm the v0.2.67 fix is live: a config.h with at
   least one `/* #undef HAVE_<X> */` for a probed function
   that is genuinely absent on Tiger.

## Subagent log

None this session — direct work + verification on imacg3.
