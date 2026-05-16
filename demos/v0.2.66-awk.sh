#!/usr/bin/env bash
# Demo for v0.2.66-g3 — one-true-awk builds + runs correctly with tcc
# as CC on Tiger PPC.
#
# The fix this milestone shipped: Tiger's <math.h> for PPC implements
# the isinf() / isnan() / isfinite() macros inline as
#
#     __builtin_fabs(x) == __builtin_inf()
#
# gcc treats __builtin_fabs / __builtin_inf as compiler intrinsics
# that inline; tcc emits a regular function call. libtcc1.a provides
# the symbols (lib-ppc.c), but tcc didn't predeclare their
# prototypes — so callers saw implicit-int return signatures. On
# PowerPC, doubles return in `f1` and ints in `r3`; the implicit-int
# caller reads `r3` (garbage), so isinf(0.0) returned true and
# isinf(+inf) returned false. Any program that #includes <math.h>
# and exercises isinf/isnan/isfinite on a numeric value silently
# went off the rails.
#
# awk's tran.c::get_inf_nan() is called from get_str_val() whenever
# a numeric Cell is stringified for output. So `print NF` on an
# empty record (NF=0) was printing "+inf" under tcc — and many
# other clean numeric values picked up similar nonsense. The fix:
# tcc/include/tccdefs.h adds extern declarations for the six
# math builtins (__builtin_fabs{,f,l}, __builtin_inf{,f,l}) under
# the __APPLE__ branch so tcc emits the correct calling convention.
#
# This demo:
# - Downloads one-true-awk tag 20260426.
# - Builds it with tcc as the C compiler.
# - Asserts only libSystem is linked.
# - Smoke-tests: empty-line NF (the smoking gun), arithmetic,
#   printf with float format, gsub.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0266-awk-build"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

# bison-3.x is required to regenerate awkgram.tab.{c,h}. Tiger ships
# bison-1.28 which doesn't grok one-true-awk's grammar. Install
# via tiger.sh if missing.
BISON=/opt/bison-3.8.2/bin/bison
if [[ ! -x "$BISON" ]]; then
    if command -v tiger.sh >/dev/null 2>&1; then
        echo "==> bison-3.8.2 not found; installing via tiger.sh..."
        tiger.sh bison-3.8.2
    else
        echo "ERROR: $BISON missing and tiger.sh unavailable." >&2
        exit 1
    fi
fi

rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

echo "==> Downloading one-true-awk tag 20260426..."
CURL=/opt/tigersh-deps-0.1/bin/curl
[[ -x "$CURL" ]] || CURL=curl
"$CURL" -fsSL \
    --cacert /Users/macuser/tmp/cacert-2026-03-19.pem \
    https://codeload.github.com/onetrueawk/awk/tar.gz/refs/tags/20260426 \
    -o awk-20260426.tar.gz
tar xzf awk-20260426.tar.gz
cd awk-20260426

echo "==> Regenerating awkgram.tab.{c,h} with bison-3.8.2..."
"$BISON" -d -b awkgram awkgram.y 2>/dev/null

echo "==> Building maketab with gcc-4.0 (host tool)..."
gcc-4.0 -O2 maketab.c -o maketab
./maketab awkgram.tab.h > proctab.c

echo "==> Compiling awk with tcc..."
OFILES="awkgram.tab b main parse proctab tran lib run lex"
for f in $OFILES; do
    "$TCC_ABS" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
        -c -o "$f.o" "$f.c"
done

echo "==> Linking awk..."
"$TCC_ABS" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
    -o awk-tcc \
    awkgram.tab.o b.o main.o parse.o proctab.o tran.o lib.o run.o lex.o \
    -lm

echo "==> Verifying tcc-only linkage (should be just libSystem)..."
LINKAGE=$(otool -L awk-tcc | tail -n +2 | awk '{print $1}')
if echo "$LINKAGE" | grep -vq libSystem; then
    echo "FAIL: linked against more than libSystem:"
    echo "$LINKAGE"
    exit 1
fi
echo "  links only against: $LINKAGE"

echo "==> awk --version..."
./awk-tcc --version

echo "==> Smoke test: NF=0 on empty line should print 0 (was '+inf' pre-fix)..."
NF_RESULT=$(printf "\n" | ./awk-tcc '{print NF}')
if [[ "$NF_RESULT" != "0" ]]; then
    echo "FAIL: NF on empty line gave '$NF_RESULT', expected '0'"
    exit 1
fi
echo "  PASS: NF on empty line = $NF_RESULT"

echo "==> Smoke test: integer sum..."
SUM=$(printf "1\n2\n3\n4\n5\n" | ./awk-tcc '{s+=$1} END {print s}')
if [[ "$SUM" != "15" ]]; then
    echo "FAIL: sum gave '$SUM', expected '15'"
    exit 1
fi
echo "  PASS: sum = $SUM"

echo "==> Smoke test: printf with float format..."
OUT=$(./awk-tcc 'BEGIN { printf "%.6g %.6g %.6g\n", 0, 1.5, 3.14159265 }')
if [[ "$OUT" != "0 1.5 3.14159" ]]; then
    echo "FAIL: printf gave '$OUT', expected '0 1.5 3.14159'"
    exit 1
fi
echo "  PASS: printf = $OUT"

echo "==> Smoke test: gsub..."
OUT=$(echo "hello world" | ./awk-tcc '{gsub(/o/, "O"); print}')
if [[ "$OUT" != "hellO wOrld" ]]; then
    echo "FAIL: gsub gave '$OUT', expected 'hellO wOrld'"
    exit 1
fi
echo "  PASS: gsub = $OUT"

echo "==> Smoke test: associative array..."
OUT=$(printf "apple\nbanana\napple\ncherry\nbanana\napple\n" | \
    ./awk-tcc '{c[$1]++} END {for (w in c) printf "%s=%d\n", w, c[w]}' | \
    sort | tr '\n' ' ' | sed 's/  *$//')
EXP="apple=3 banana=2 cherry=1"
if [[ "$OUT" != "$EXP" ]]; then
    echo "FAIL: histogram gave '$OUT', expected '$EXP'"
    exit 1
fi
echo "  PASS: histogram = $OUT"

echo
echo "PASS — one-true-awk 20260426 builds + runs correctly with tcc on Tiger PPC"
