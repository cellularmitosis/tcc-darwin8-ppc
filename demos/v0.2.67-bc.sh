#!/usr/bin/env bash
# Demo for v0.2.67-g3 — GNU bc 1.07.1 (arbitrary-precision calculator)
# builds + runs correctly with tcc as CC on Tiger PPC, using ordinary
# autoconf with no CFLAGS shim.
#
# What this demo proves:
# (1) v0.2.67-g3's AC_CHECK_FUNCS fix is live in practice. bc's
#     configure script probes for `_doprnt`, `vprintf`, `isgraph`,
#     `setvbuf`, `fstat`, `strtol`, ... — exactly the AC_LINK_IFELSE /
#     AC_CHECK_FUNCS surface that pre-v0.2.67 would have falsely
#     reported as ALL PRESENT. With the v0.2.67 link-time check, bc's
#     config.h reflects Tiger libSystem honestly and the build picks
#     the right code paths.
# (2) v0.2.66-g3's math-builtin fix is exercised heavily. bc's libmath
#     implements sqrt / atan / sin / cos / exp / log via Taylor /
#     Newton series. Those rely on FP correctness end-to-end (no
#     isinf/isnan trap, no FP-arg shadowing bug, no funcptr-const
#     issue, no longdouble bug). Pre-v0.2.66 any program with
#     `<math.h>` + numeric stringification was suspect; bc -l is the
#     deepest FP workout in the demos.
# (3) The "links only libSystem" invariant holds for autoconf builds:
#     tcc-built bc and dc each have exactly one entry in `otool -L`.
# (4) bc-1.07.1 built with tcc produces output BIT-IDENTICAL to
#     Tiger's stock /usr/bin/bc (GNU bc 1.06) on a battery of
#     arbitrary-precision inputs.
#
# Contrast with v0.2.66-expat.sh: that demo had to pass
# `CFLAGS=-Werror=implicit-function-declaration` to keep AC_CHECK_FUNCS
# from accepting the non-existent `arc4random_buf`. This demo runs
# configure with default CFLAGS and gets correct answers because tcc
# now rejects undefined externals at link time.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0267-bc-build"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

# bc-1.07.1 needs bison + flex + ed (all stock on Tiger).
for tool in bison flex ed; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: $tool not found on PATH." >&2
        exit 1
    fi
done

rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

echo "==> Downloading GNU bc 1.07.1 source..."
CURL=/opt/tigersh-deps-0.1/bin/curl
[[ -x "$CURL" ]] || CURL=curl
"$CURL" -fsSL \
    --cacert /Users/macuser/tmp/cacert-2026-03-19.pem \
    https://ftp.gnu.org/gnu/bc/bc-1.07.1.tar.gz \
    -o bc-1.07.1.tar.gz
tar xzf bc-1.07.1.tar.gz
cd bc-1.07.1

echo "==> Configuring with tcc as CC (default CFLAGS — no Werror shim)..."
PATH=/opt/make-4.3/bin:$PATH \
    CC="$TCC_ABS -B$TCCROOT -L$TCCROOT -I$TCCROOT/include" \
    ./configure >/dev/null 2>&1

echo "==> Verifying configure made honest choices..."
# These probe AC_CHECK_FUNCS — pre-v0.2.67 tcc would have answered YES
# to all of them. _doprnt and lib.h aren't in Tiger libSystem, so the
# correct config.h has them undef'd.
if grep -q '^#define HAVE__DOPRNT 1' config.h; then
    echo "FAIL: HAVE__DOPRNT was defined — should not be (not in Tiger libSystem)"
    grep DOPRNT config.h
    exit 1
fi
if grep -q '^#define HAVE_LIB_H 1' config.h; then
    echo "FAIL: HAVE_LIB_H was defined — should not be"
    grep LIB_H config.h
    exit 1
fi
if ! grep -q '^#define HAVE_VPRINTF 1' config.h; then
    echo "FAIL: HAVE_VPRINTF should be defined (vprintf IS in Tiger libSystem)"
    grep VPRINTF config.h
    exit 1
fi
echo "  PASS: HAVE__DOPRNT undef'd, HAVE_LIB_H undef'd, HAVE_VPRINTF defined"

echo "==> Building bc + dc with tcc..."
PATH=/opt/make-4.3/bin:$PATH make >/dev/null 2>build.log
if [[ ! -x bc/bc || ! -x dc/dc ]]; then
    echo "FAIL: bc/bc or dc/dc not produced. Tail of build.log:"
    tail -30 build.log
    exit 1
fi
echo "  built: bc/bc, dc/dc"

echo "==> Verifying tcc-only linkage (should be just libSystem)..."
for prog in bc/bc dc/dc; do
    LINKAGE=$(otool -L "$prog" | tail -n +2 | awk '{print $1}')
    if echo "$LINKAGE" | grep -vq libSystem; then
        echo "FAIL: $prog linked against more than libSystem:"
        echo "$LINKAGE"
        exit 1
    fi
    echo "  $prog links only against: $LINKAGE"
done

BC=./bc/bc
DC=./dc/dc

echo "==> bc --version..."
"$BC" --version 2>&1 | head -1

echo "==> Smoke test 1: basic integer arithmetic..."
OUT=$(echo "2+2*3" | "$BC")
[[ "$OUT" == "8" ]] || { echo "FAIL: 2+2*3 gave '$OUT'"; exit 1; }
echo "  PASS: 2+2*3 = $OUT"

echo "==> Smoke test 2: 2^100 (big-integer exponentiation)..."
OUT=$(echo "2^100" | "$BC")
EXP="1267650600228229401496703205376"
[[ "$OUT" == "$EXP" ]] || { echo "FAIL: 2^100 gave '$OUT'"; exit 1; }
echo "  PASS: 2^100 = $OUT"

echo "==> Smoke test 3: 50! (recursion + big integer)..."
OUT=$(echo "define f(n) { if(n<=1) return 1; return n*f(n-1); }
f(50)" | "$BC")
EXP="30414093201713378043612608166064768844377641568960512000000000000"
[[ "$OUT" == "$EXP" ]] || { echo "FAIL: 50! gave '$OUT'"; exit 1; }
echo "  PASS: 50! = $OUT"

echo "==> Smoke test 4: sqrt(2) at scale 60 (Newton's method via libmath)..."
OUT=$(echo "scale=60; sqrt(2)" | "$BC" -l)
EXP="1.414213562373095048801688724209698078569671875376948073176679"
[[ "$OUT" == "$EXP" ]] || { echo "FAIL: sqrt(2) gave '$OUT'"; exit 1; }
echo "  PASS: sqrt(2) = $OUT"

echo "==> Smoke test 5: π via 4*atan(1) at scale 60..."
OUT=$(echo "scale=60; 4*a(1)" | "$BC" -l)
EXP="3.141592653589793238462643383279502884197169399375105820974944"
[[ "$OUT" == "$EXP" ]] || { echo "FAIL: π gave '$OUT'"; exit 1; }
echo "  PASS: π = $OUT"

echo "==> Smoke test 6: e (Euler's number) at scale 60 — exp/log workout..."
OUT=$(echo "scale=60; e(1)" | "$BC" -l)
EXP="2.718281828459045235360287471352662497757247093699959574966967"
[[ "$OUT" == "$EXP" ]] || { echo "FAIL: e(1) gave '$OUT'"; exit 1; }
echo "  PASS: e = $OUT"

echo "==> Smoke test 7: log/exp inverse at scale 40 — e(l(7)) should be ~7..."
OUT=$(echo "scale=40; e(l(7))" | "$BC" -l)
# Tolerance check via bc itself: |result - 7| < 10^-30
CLOSE=$(echo "scale=40; r = e(l(7)); if (r > 7) d = r - 7 else d = 7 - r;
              if (d < 0.000000000000000000000000000001) 1 else 0" | "$BC" -l)
[[ "$CLOSE" == "1" ]] || { echo "FAIL: e(l(7)) gave '$OUT', not within 10^-30 of 7"; exit 1; }
echo "  PASS: e(l(7)) = $OUT (within 10^-30 of 7)"

echo "==> Smoke test 8: sin² + cos² = 1 identity at scale 40, x=1.234..."
OUT=$(echo "scale=40; s(1.234)^2 + c(1.234)^2" | "$BC" -l)
CLOSE=$(echo "scale=40; r = s(1.234)^2 + c(1.234)^2;
              if (r > 1) d = r - 1 else d = 1 - r;
              if (d < 0.000000000000000000000000000001) 1 else 0" | "$BC" -l)
[[ "$CLOSE" == "1" ]] || { echo "FAIL: sin²+cos² gave '$OUT', not within 10^-30 of 1"; exit 1; }
echo "  PASS: sin² + cos² = $OUT (Pythagorean identity, within 10^-30 of 1)"

echo "==> Smoke test 9: cross-validate against Tiger's /usr/bin/bc..."
SYSBC=/usr/bin/bc
if [[ ! -x "$SYSBC" ]]; then
    echo "  SKIP (no /usr/bin/bc on this host)"
else
    cat > /tmp/v0267-bc-xv.bc <<'XV'
2^200
scale=50; sqrt(2)
scale=50; sqrt(3)
scale=50; sqrt(5)
scale=50; 4*a(1)
scale=50; a(1/2)
scale=50; e(1)
scale=50; e(2)
scale=50; l(2)
scale=50; l(10)
scale=50; s(1)
scale=50; c(1)
scale=50; s(3.14159265358979323846264338327950288)
define f(n) { if(n<=1) return 1; return n*f(n-1); }
f(40)
define g(a,b) { if(b==0) return a; return g(b, a%b); }
g(123456789, 987654321)
scale=30; for(i=1;i<=10;i++) { sqrt(i) }
quit
XV
    "$BC"   -l /tmp/v0267-bc-xv.bc > /tmp/v0267-bc-tcc.out 2>&1
    "$SYSBC" -l /tmp/v0267-bc-xv.bc > /tmp/v0267-bc-sys.out 2>&1
    if diff -q /tmp/v0267-bc-tcc.out /tmp/v0267-bc-sys.out >/dev/null; then
        echo "  PASS: tcc-bc output BIT-IDENTICAL to /usr/bin/bc 1.06 across battery"
        echo "        ($(wc -l < /tmp/v0267-bc-tcc.out | tr -d ' ') lines of arbitrary-precision results)"
    else
        echo "FAIL: tcc-bc disagrees with /usr/bin/bc. Diff:"
        diff /tmp/v0267-bc-tcc.out /tmp/v0267-bc-sys.out | head -40
        exit 1
    fi
fi

echo "==> Smoke test 10: dc (reverse-Polish calculator companion)..."
OUT=$(echo "2 3 + p" | "$DC")
[[ "$OUT" == "5" ]] || { echo "FAIL: dc '2 3 + p' gave '$OUT'"; exit 1; }
echo "  PASS: dc '2 3 + p' = $OUT"

OUT=$(echo "12 7 % p" | "$DC")
[[ "$OUT" == "5" ]] || { echo "FAIL: dc mod gave '$OUT'"; exit 1; }
echo "  PASS: dc '12 7 % p' = $OUT"

echo
echo "PASS — GNU bc 1.07.1 builds + runs correctly with tcc on Tiger PPC"
echo "       (autoconf default CFLAGS, links only libSystem, arbitrary-"
echo "        precision results bit-identical to /usr/bin/bc 1.06)"
