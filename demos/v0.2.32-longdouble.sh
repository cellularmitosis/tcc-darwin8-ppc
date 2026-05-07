#!/usr/bin/env bash
# Demo for v0.2.32-g3 — IBM double-double `long double` ABI
#
# Pre-v0.2.32, tcc-on-PPC treated `long double` as an 8-byte double
# (LDOUBLE_SIZE = 8). Apple PPC32's actual ABI uses 16-byte IBM
# double-double (a pair of doubles). Programs that mix tcc-compiled
# functions with gcc-compiled callers/callees over the LD ABI ended
# up with corrupted values.
#
# Specifically broken (and now fixed) cases in upstream `abitest-cc`:
#   - ret_longdouble_test:   `LD f(LD x) { return x + x; }`
#   - arg_align_test:        `LD f(LD,int,LD,int,LD)` summing 3 LDs
#
# This demo writes the pieces that were broken to a small stress
# program, builds it with tcc, and verifies output matches gcc's
# reference output.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0232-ld.c"
TCC_BIN="$TMP/v0232-ld-tcc"
GCC_BIN="$TMP/v0232-ld-gcc"

cat > "$SRC" <<'EOF'
#include <stdio.h>

/* (1) LD param + LD result, gcc-built caller -> tcc-built callee */
long double doublize(long double x) { return x + x; }

/* (2) Mixed LD/int signature with multiple LDs (Apple PPC ABI:
 *     each LD takes 2 FPRs and 4 GPR-shadow slots) */
long double sum3_with_pads(long double a, int b, long double c,
                           int d, long double e)
{
    (void)b; (void)d;
    return a + c + e;
}

/* (3) Variadic printf("%Lf %Lf %Lf %Lf", ...) — exercises the
 *     `__DARWIN_LDBL_COMPAT` redirect to printf$LDBL128 (the Tiger
 *     16-byte LD printf), driven by tcc now defining
 *     __LDBL_MANT_DIG__ > __DBL_MANT_DIG__. */
static long double ld1 = 12.34L, ld2 = 56.78L;

int main(void)
{
    long double r1 = doublize(378943892.0L);
    if (r1 != 757887784.0L) { printf("FAIL r1=%Lf\n", r1); return 1; }
    printf("doublize(378943892.0L) = %.0Lf\n", r1);

    long double r2 = sum3_with_pads(12.0L, 0, 25.0L, 0, 37.0L);
    if (r2 != 74.0L) { printf("FAIL r2=%Lf\n", r2); return 1; }
    printf("sum3_with_pads(12, 0, 25, 0, 37) = %.0Lf\n", r2);

    printf("%Lf %Lf %Lf %Lf\n",
           ld1 + ld2, ld1 - ld2, ld1 * ld2, ld1 / ld2);

    long double a = 12.34L, b = 56.78L;
    if (a < b)  printf("a < b OK\n");
    if (b > a)  printf("b > a OK\n");
    if (a != b) printf("a != b OK\n");

    return 0;
}
EOF

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

echo "==> Building with tcc..."
TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" "$SRC" -o "$TCC_BIN"

echo "==> Building reference with gcc-4.0..."
gcc-4.0 -O0 -w "$SRC" -o "$GCC_BIN"

echo "==> tcc output:"
TCC_OUT=$("$TCC_BIN")
echo "$TCC_OUT"

echo "==> gcc output:"
GCC_OUT=$("$GCC_BIN")
echo "$GCC_OUT"

if [[ "$TCC_OUT" == "$GCC_OUT" ]]; then
    echo
    echo "PASS — tcc and gcc agree on long-double ABI behavior"
    exit 0
fi

echo
echo "FAIL — outputs diverge"
diff <(echo "$TCC_OUT") <(echo "$GCC_OUT") || true
exit 1
