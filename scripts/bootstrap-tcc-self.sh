#!/bin/sh
# bootstrap-tcc-self.sh — compile tcc using the freshly-built `tcc`
# binary, producing a tcc-self executable that's been entirely codegen'd
# by our PowerPC backend.
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

echo "  CC tcc.c (ONE_SOURCE; via $TCC)"
"$TCC" -c "$SRC" -o "$WORK/tcc.o" -B./tcc -I./tcc

echo "  LINK $OUT"
LIBGCC=$(ls /Developer/SDKs/MacOSX10.4u.sdk/usr/lib/gcc/powerpc-apple-darwin8/4.0.1/libgcc.a 2>/dev/null || \
         ls /usr/lib/gcc/powerpc-apple-darwin8/4.0.1/libgcc.a)
gcc-4.0 -arch ppc "$WORK/tcc.o" "$LIBTCC1" -o "$OUT" \
    -lm -ldl -lpthread -flat_namespace "$LIBGCC"

ls -la "$OUT"
echo "  done"
