#!/bin/sh
# v0.2.18-g3 (post-v0.2.17, session 043) milestone — bzip2 1.0.8
# builds + round-trips end-to-end with tcc-darwin8-ppc.
#
# Run on ibookg37 (or any Tiger 10.4.11 PowerPC):
#
#     ./demos/v0.2.18-bzip2.sh
#
# Expected last lines: two identical MD5 sums (input file == output
# of compress | decompress).
#
# Why this matters: bzip2 is the third non-trivial third-party
# program (after lua and zlib) to build and run end-to-end with
# tcc-darwin8-ppc. It exercises a different mix of codegen paths
# than lua/zlib (heavy bitwise arithmetic, large lookup tables,
# Burrows-Wheeler transform, Huffman trees). Round-trip
# correctness on a non-trivial input is a strong end-to-end test.

set -e
cd "$(dirname "$0")/.."

BZIP2_VER=1.0.8
BZIP2_SRC=/tmp/bzip2-${BZIP2_VER}

if [ ! -d "$BZIP2_SRC" ]; then
    echo "==> fetching bzip2-${BZIP2_VER}.tar.gz"
    cd /tmp
    if [ ! -f "bzip2-${BZIP2_VER}.tar.gz" ]; then
        curl -sLO "https://sourceware.org/pub/bzip2/bzip2-${BZIP2_VER}.tar.gz" \
            || { echo "Need a tarball at /tmp/bzip2-${BZIP2_VER}.tar.gz"; exit 1; }
    fi
    tar xzf "bzip2-${BZIP2_VER}.tar.gz"
    cd - >/dev/null
fi

echo "==> compiling bzip2 with tcc"
cd "$BZIP2_SRC"
TCC=$OLDPWD/tcc/tcc
B=$OLDPWD/tcc
"$TCC" -B"$B" -DPLATFORM_UNIX=1 -c \
    blocksort.c huffman.c crctable.c randtable.c \
    compress.c decompress.c bzlib.c bzip2.c

echo "==> linking bzip2"
"$TCC" -B"$B" -o /tmp/bzip2 \
    blocksort.o huffman.o crctable.o randtable.o \
    compress.o decompress.o bzlib.o bzip2.o

echo "==> round-trip test on /etc/services"
ls -la /etc/services
/tmp/bzip2 -c /etc/services > /tmp/services.bz2
ls -la /tmp/services.bz2
/tmp/bzip2 -dc /tmp/services.bz2 > /tmp/services.out

md5 /etc/services /tmp/services.out
if [ "$(md5 -q /etc/services)" = "$(md5 -q /tmp/services.out)" ]; then
    echo "PASS"
else
    echo "FAIL: round-trip md5 mismatch"
    exit 1
fi
