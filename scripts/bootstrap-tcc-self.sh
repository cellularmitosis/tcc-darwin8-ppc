#!/bin/sh
# bootstrap-tcc-self.sh — compile and link tcc entirely with itself,
# producing a tcc-self executable codegen'd and linked solely by our
# PowerPC backend (no gcc-4.0 involvement).
#
# After it lands a working tcc-self at $OUT, optionally runs the
# self-host fixpoint test (set FIXPOINT=1 to enable; off by default
# because gen-2 → gen-3 takes ~30s on a G3 / iBookG4 fleet host).
#
# Run from the repo root after build-tiger.sh has produced ./tcc/tcc.

set -e

cd "$(dirname "$0")/.."
TCC=./tcc/tcc
LIBTCC1=./tcc/libtcc1.a
SRC=./tcc/tcc.c
OUT="${OUT:-/tmp/tcc-self}"

if [ ! -x "$TCC" ]; then
    echo "ERROR: $TCC not found. Run scripts/build-tiger.sh first." >&2
    exit 1
fi
if [ ! -f "$LIBTCC1" ]; then
    echo "ERROR: $LIBTCC1 not found. Run scripts/build-tiger.sh first." >&2
    exit 1
fi

WORK=${WORK:-/tmp/tcc-self-build}
mkdir -p "$WORK"
rm -f "$WORK"/tcc.o "$OUT"

# `-w`: tcc currently has a codegen issue where the warning-print
# path crashes inside fflush(stdout) when tcc-self compiles tcc.c.
# Suppressing warnings sidesteps it. Tracked separately; the one
# warning we'd otherwise hit is `#warning add arch specific
# rt_get_caller_pc()` in tccrun.c, harmless.
echo "  CC tcc.c (ONE_SOURCE; via $TCC)"
"$TCC" -c -w "$SRC" -o "$WORK/tcc.o" -B./tcc -I./tcc

echo "  LINK $OUT (via $TCC, no gcc-4.0)"
"$TCC" -B./tcc -I./tcc -o "$OUT" "$WORK/tcc.o" "$LIBTCC1"

ls -la "$OUT"

# Smoke-test: tcc-self launches and prints its version.
echo "  TEST $OUT -v"
"$OUT" -v

# Optional fixpoint: tcc-self compiles tcc.c, link tcc-self2; verify
# tcc-self2 -> tcc-self3 produces byte-identical output (the canonical
# self-host fixpoint).
if [ "${FIXPOINT:-0}" = "1" ]; then
    OUT2=${OUT2:-/tmp/tcc-self2}
    OUT3=${OUT3:-/tmp/tcc-self3}
    OBJ2=${OBJ2:-$WORK/tcc-self2.o}
    OBJ3=${OBJ3:-$WORK/tcc-self3.o}
    rm -f "$OUT2" "$OUT3" "$OBJ2" "$OBJ3"

    echo "  CC tcc.c (via $OUT) -> $OBJ2"
    "$OUT" -c -w "$SRC" -o "$OBJ2" -B./tcc -I./tcc
    echo "  LINK $OUT2 (via $OUT)"
    "$OUT" -B./tcc -I./tcc -o "$OUT2" "$OBJ2" "$LIBTCC1"

    echo "  CC tcc.c (via $OUT2) -> $OBJ3"
    "$OUT2" -c -w "$SRC" -o "$OBJ3" -B./tcc -I./tcc
    echo "  LINK $OUT3 (via $OUT2)"
    "$OUT2" -B./tcc -I./tcc -o "$OUT3" "$OBJ3" "$LIBTCC1"

    if cmp "$OBJ2" "$OBJ3"; then
        echo "  FIXPOINT HOLDS: $(basename $OBJ2) == $(basename $OBJ3)"
    else
        echo "  FIXPOINT BROKEN: $(basename $OBJ2) != $(basename $OBJ3)" >&2
        exit 1
    fi
fi

echo "  done"
