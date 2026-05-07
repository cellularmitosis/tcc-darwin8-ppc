#!/usr/bin/env bash
# Demo for v0.2.34-g3 — precision-correct double-double arithmetic
#
# Pre-v0.2.34, tcc-on-PPC's `__gcc_q{add,sub,mul,div}` and `*tf2`
# helpers in lib-ppc.c were LOSSY: they compared/computed the high
# halves only and zeroed the low halves of the result. Programs
# whose long-double values needed >53-bit mantissa (the canonical
# "why does dd exist" case — values larger than 2^53 with subunit
# precision) silently lost the low half.
#
# v0.2.34 replaces the lossy stubs with proper Knuth Two-Sum +
# Veltkamp split + Dekker product implementations, matching the
# standard libgcc soft-fp double-double algorithms. No fma
# dependency (PPC's hardware fmadd would be faster but tcc-on-PPC
# doesn't emit it).

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0234-dd.c"
TCC_BIN="$TMP/v0234-dd-tcc"
GCC_BIN="$TMP/v0234-dd-gcc"

cat > "$SRC" <<'EOF'
#include <stdio.h>

int main(void) {
    int fail = 0;

    /* Test 1: 2^53 + 1 must round-trip exactly. */
    long double x = 9007199254740992.0L;  /* 2^53 */
    long double y = 1.0L;
    long double z = x + y;
    long double w = z - x;
    if (w != 1.0L) {
        printf("FAIL test1: (2^53 + 1) - 2^53 = %.0Lf, expected 1\n", w);
        fail++;
    }

    /* Test 2: precision survives larger sums. */
    long double a = 1.0e16L;
    long double b = 1.0L;
    long double s = a + b;
    long double r = s - a;
    if (r != 1.0L) {
        printf("FAIL test2: (1e16 + 1) - 1e16 = %.20Lf, expected 1\n", r);
        fail++;
    }

    /* Test 3: comparison through the low half.
     * 1e20 + 1 and 1e20 + 2 compare equal as plain doubles
     * (both round to the same value), but as dd they're distinct. */
    long double c = 1.0e20L + 1.0L;
    long double d = 1.0e20L + 2.0L;
    if (c == d) {
        printf("FAIL test3: 1e20+1 and 1e20+2 compare equal\n");
        fail++;
    }
    if (c >= d) {
        printf("FAIL test3b: 1e20+1 >= 1e20+2\n");
        fail++;
    }

    /* Test 4: multiplication keeps precision in the low half.
     * Pi to 20 digits times itself, then take the difference from
     * the squared rounded-double result. */
    long double pi = 3.14159265358979323846L;
    long double pi2_dd = pi * pi;
    double pi_d = (double)pi;
    double pi2_d = pi_d * pi_d;
    long double diff = pi2_dd - (long double)pi2_d;
    /* The difference should be tiny but NONZERO (the low half captures
     * the rounding error of the double multiplication). */
    if (diff == 0.0L) {
        printf("FAIL test4: dd_mul lost the low half (pi*pi)\n");
        fail++;
    }

    /* Test 5: division round-trips for representable values. */
    long double n = 100.0L;
    long double m = 7.0L;
    long double q = n / m;
    long double back = q * m;
    /* Don't expect bit-exact (rounding accumulates) but should be very close. */
    long double err = back - n;
    if (err > 1e-30L || err < -1e-30L) {
        printf("FAIL test5: 100/7*7 = %.30Lf, |err|=%.30Le\n", back, err);
        fail++;
    }

    if (fail == 0)
        printf("PASS — all 5 dd precision tests\n");
    return fail;
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
"$TCC_BIN"
TCC_RC=$?

echo "==> gcc output:"
"$GCC_BIN"
GCC_RC=$?

if [[ $TCC_RC -eq 0 && $GCC_RC -eq 0 ]]; then
    echo
    echo "PASS — both tcc and gcc see all 5 dd precision invariants"
    exit 0
fi
echo "FAIL — tcc=$TCC_RC gcc=$GCC_RC"
exit 1
