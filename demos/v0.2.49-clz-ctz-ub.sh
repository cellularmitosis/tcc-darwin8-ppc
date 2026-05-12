#!/bin/sh
# v0.2.49-g3 milestone — libtcc1.a __builtin_clz/ctz match gcc-PPC at x==0.
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.49-clz-ctz-ub.sh
#
# Expected last line:
#     All PASS
#
# What this demonstrates:
#   tcc's libtcc1.a runtime implementations of __builtin_clz / __builtin_ctz
#   (and the long / long-long variants) now return the same values as
#   gcc-4.0 on PPC when x==0 — clz(0)=32, ctz(0)=-1, clzll(0)=64,
#   ctzll(0)=31. The gcc spec marks these inputs as undefined behavior,
#   so both pre-fix tcc (de-Bruijn fallback) and post-fix tcc (special-
#   cased) are technically conformant — but matching gcc-PPC removes a
#   long-standing csmith differential-testing nuisance and retires most
#   of the docs/sessions/062 `builtin_compat.h` shim. clrsb(0) still
#   returns 31/63 per spec because clrsb continues to use the unmodified
#   CLZI/CLZL macros.
set -e
cd "$(dirname "$0")/.."
TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure || true
)
SRC="demos/v0.2.49-clz-ctz-ub.c"
OUT=/tmp/v0.2.49-clz-ctz-ub
$TCC -B./tcc -L./tcc -I./tcc/include -w "$SRC" -o "$OUT"
"$OUT"
