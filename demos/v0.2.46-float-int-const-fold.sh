#!/bin/sh
# v0.2.46-g3 milestone — float-to-int constant-fold saturation.
#
# Run on imacg3 (or any Tiger 10.4.11 PowerPC G3/G4):
#
#     ./demos/v0.2.46-float-int-const-fold.sh
#
# Expected last line:
#     All PASS
#
# What this demonstrates:
#   tcc on PowerPC now saturates float-to-int constant-fold the same
#   way the runtime `fctiwz` instruction does — INT_MAX/INT_MIN on
#   overflow, 0x80000000 on NaN, round-toward-zero in range.
#   Pre-v0.2.46, the const folder went through the host's
#   `(int64_t)long_double` cast, which doesn't saturate to int32
#   range, so a source line like `int x = (int)1e10f` folded to
#   1410065408 (low 32 bits of (int64_t)1e10) instead of 0x7FFFFFFF
#   like gcc-4.0 and runtime tcc both produce.
#
# The bug was found via csmith differential testing — `--float
# --builtins` seed 92 had a chained `*(int*)p = (...., huge_float)`
# pattern in func_65 that hit the constant-fold path and disagreed
# with gcc-4.0. Reduced to the minimal cases embedded here.
set -e
cd "$(dirname "$0")/.."
TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure || true
)
SRC="demos/v0.2.46-float-int-const-fold.c"
OUT=/tmp/v0.2.46-float-int-const-fold
$TCC -B./tcc -L./tcc -I./tcc/include -w "$SRC" -o "$OUT"
"$OUT"
