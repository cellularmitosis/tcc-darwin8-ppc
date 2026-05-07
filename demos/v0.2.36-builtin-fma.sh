#!/usr/bin/env bash
# Demo for v0.2.36-g3 — `__builtin_fma` / `__builtin_fmaf`
# emit PPC's hardware fmadd / fmadds inline.
#
# Pre-v0.2.36, calls to `__builtin_fma()` failed with "Symbol not
# found" at runtime — tcc treated the name as an external function
# and there was no library-side stub. Even if there had been a
# stub (libm has fma()), the cost would be a function call.
#
# v0.2.36 adds TOK_builtin_fma / TOK_builtin_fmaf, dispatches them
# in tccgen.c's expression parser, and emits PPC's `fmadd FRT, FRA,
# FRC, FRB` (or `fmadds` for single) directly. One instruction,
# fused (single rounding step).
#
# Side benefit: lib-ppc.c's `dd_two_prod` (used by all the
# `__gcc_q*` helpers from v0.2.34) collapses from a 10-flop
# Veltkamp+Dekker product to 2 flops:
#
#   p = a * b
#   e = fma(a, b, -p)
#
# All v0.2.34 dd-precision invariants still hold.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0236-fma.c"
TCC_BIN="$TMP/v0236-fma-tcc"
TCC_OBJ="$TMP/v0236-fma-tcc.o"

cat > "$SRC" <<'EOF'
#include <stdio.h>

double f(double a, double b, double c) {
    return __builtin_fma(a, b, c);
}
float ff(float a, float b, float c) {
    return __builtin_fmaf(a, b, c);
}

int main(void) {
    int fail = 0;
    if (f(2.0, 3.0, 1.0) != 7.0)         { printf("FAIL 1\n"); fail++; }
    if (f(2.0, 3.0, -10.0) != -4.0)      { printf("FAIL 2\n"); fail++; }
    if (ff(2.0f, 3.0f, 1.0f) != 7.0f)    { printf("FAIL 3\n"); fail++; }

    /* Cancellation: a*b is computed at extended precision before
     * adding c, so values that would round differently in the
     * naive form match the algebraic identity. */
    double r = f(0.1, 10.0, -1.0);
    if (r < -1e-15 || r > 1e-15)         { printf("FAIL 4: %.20g\n", r); fail++; }

    if (fail == 0) printf("PASS\n");
    return fail;
}
EOF

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)

echo "==> Compiling with tcc..."
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" -c "$SRC" -o "$TCC_OBJ"
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" "$SRC" -o "$TCC_BIN"

echo "==> Verifying tcc emitted fmadd/fmadds inline (no fma function call)..."
DISASM=$(otool -tv "$TCC_OBJ")
if ! grep -q "fmadd[s]\?" <<< "$DISASM"; then
    echo "FAIL — tcc did not emit fmadd"
    echo "$DISASM"
    exit 1
fi
if grep -qE "bl\s+_fma" <<< "$DISASM"; then
    echo "FAIL — tcc fell back to a fma() function call"
    exit 1
fi
echo "  ok: fmadd / fmadds present, no bl _fma"

echo "==> Running..."
"$TCC_BIN"

echo
echo "==> Confirming libtcc1.a's dd_two_prod uses fmadd..."
if otool -tv "$TCCROOT/libtcc1.a" | grep -q "fmadd"; then
    echo "  ok: libtcc1.a contains fmadd instructions"
else
    echo "FAIL — libtcc1.a has no fmadd (dd_two_prod didn't pick up __builtin_fma)"
    exit 1
fi

echo
echo "PASS — __builtin_fma / __builtin_fmaf emit hardware fmadd inline"
