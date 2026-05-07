#!/usr/bin/env bash
# Demo for v0.2.35-g3 — libm LDBL128 redirect for math functions
#
# Pre-v0.2.35, tcc-on-PPC's `<math.h>` calls (ldexpl, expl, sinl,
# cosl, etc.) silently routed to the LEGACY 8-byte-LD libm entries
# even though our LD ABI is 16-byte. The legacy entries treat the
# LD as a plain double and discard the precision (and worse: ldexpl
# with a tiny scale factor returned the input unchanged because its
# 8-byte-LD code path wasn't built to handle our 16-byte payload).
#
# Symptom: tcctest's "subnormal" sub-tests showed
#   `da subnormal = 0x1.1p+7` (= 136)
# instead of the expected
#   `da subnormal = 0x1.1p-1023` (≈ 1.18e-308)
# because parse_number's `ldexpl(mantissa, exp_val - frac_bits)` was
# returning the mantissa unscaled.
#
# v0.2.35 predefines `__LONG_DOUBLE_128__` so Tiger's
# `<architecture/ppc/math.h>` macro `__LIBMLDBL_COMPAT(sym)` expands
# to `__asm("_" #sym "$LDBL128")` — the 16-byte-LD libm entries
# (which Tiger ships in libSystem). v0.2.32 had already done the
# equivalent for stdio.h via `__LDBL_MANT_DIG__` + the OSX-version-min
# define; this is the math.h analog.
#
# Net effect: `make test3` content-diffs drop from 45 lines to 13
# (the 13 remaining are structural — Apple ABI alignof, gcc-4.0
# _Bool size, UB casts).

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0235-libm.c"
TCC_BIN="$TMP/v0235-libm-tcc"
GCC_BIN="$TMP/v0235-libm-gcc"

cat > "$SRC" <<'EOF'
#include <stdio.h>
#include <math.h>

int main(void) {
    int fail = 0;

    /* Test 1: ldexpl with negative scale producing subnormal. */
    long double a = 136.0L;
    long double r = ldexpl(a, -1030);
    if (r > 1.5e-308L || r < 1e-308L) {
        printf("FAIL test1: ldexpl(136, -1030) = %.6Lg (expect ~1.18e-308)\n", r);
        fail++;
    }

    /* Test 2: ldexpl with positive scale staying in normal range. */
    long double s = ldexpl(1.0L, 100);
    long double expected = 1.2676506002282294e30L;
    long double diff = s - expected;
    if (diff < -1e15L || diff > 1e15L) {
        printf("FAIL test2: ldexpl(1, 100) = %.6Lg (expect ~1.27e30)\n", s);
        fail++;
    }

    /* Test 3: scalbnl (a sibling of ldexpl). */
    long double t = scalbnl(2.0L, 10);
    if (t != 2048.0L) {
        printf("FAIL test3: scalbnl(2, 10) = %.0Lf (expect 2048)\n", t);
        fail++;
    }

    /* Test 4: fabsl preserves precision through the LD pair. */
    long double u = -3.14159265358979323846L;
    long double v = fabsl(u);
    if (v != 3.14159265358979323846L) {
        printf("FAIL test4: fabsl(-pi) = %.20Lf\n", v);
        fail++;
    }

    if (fail == 0)
        printf("PASS — all 4 libm LDBL128 redirects work\n");
    return fail;
}
EOF

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
echo "==> Building with tcc..."
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" "$SRC" -lm -o "$TCC_BIN"

echo "==> Building reference with gcc-4.0..."
gcc-4.0 -O0 -w "$SRC" -lm -o "$GCC_BIN"

echo "==> Verifying tcc references the LDBL128 libm entries..."
TCC_LDEXPL=$(nm "$TCC_BIN" | grep ldexpl || echo "")
GCC_LDEXPL=$(nm "$GCC_BIN" | grep ldexpl || echo "")
echo "  tcc: $TCC_LDEXPL"
echo "  gcc: $GCC_LDEXPL"
if [[ "$TCC_LDEXPL" != *"\$LDBL128"* ]]; then
    echo "FAIL — tcc still references the legacy ldexpl entry"
    exit 1
fi

echo "==> tcc output:"
"$TCC_BIN"
TCC_RC=$?

echo "==> gcc output:"
"$GCC_BIN"
GCC_RC=$?

if [[ $TCC_RC -eq 0 && $GCC_RC -eq 0 ]]; then
    echo
    echo "PASS — tcc-emitted math.h calls reach the right libm entries"
    exit 0
fi
echo "FAIL — tcc=$TCC_RC gcc=$GCC_RC"
exit 1
