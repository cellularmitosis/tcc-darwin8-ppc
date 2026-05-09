#!/bin/sh
# v0.2.45-g3 milestone — BE bitfield ABI fix.
#
# Run on imacg3 (or any Tiger 10.4.11 PowerPC G3/G4):
#
#     ./demos/v0.2.45-be-bitfield-abi.sh
#
# Expected last line:
#     All PASS
#
# What this demonstrates:
#   tcc on PowerPC now lays out bitfields MSB-first within their
#   storage container (matching the SysV/AIX PowerPC ABI and gcc-4.0).
#   Pre-v0.2.45, tcc used LSB-first byte order for bitfield access,
#   which was internally consistent within a single tcc-built program
#   but ABI-incompatible with gcc-built code (and surfaced as wrong
#   results when a bitfield was initialized through a sibling union
#   member or a pointer cast).
#
# The bug was found via csmith differential testing (random program
# generation, compile both with gcc-4.0 and tcc, compare runtime
# output). Csmith seed 3 with default options exhibited the divergence
# at `g_55.f1` (a 14-bit bitfield in a union); that reduced to the
# minimal repro embedded here.
set -e
cd "$(dirname "$0")/.."
TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure || true
)
SRC="demos/v0.2.45-be-bitfield-abi.c"
OUT=/tmp/v0.2.45-be-bitfield-abi
$TCC -B./tcc -L./tcc -I./tcc/include -w "$SRC" -o "$OUT"
"$OUT"
