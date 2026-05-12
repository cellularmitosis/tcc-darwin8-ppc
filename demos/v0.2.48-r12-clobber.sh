#!/bin/sh
# v0.2.48-g3 milestone — r12 reg-allocator clobber fix.
#
# Run on any Tiger 10.4 PowerPC G3/G4 host with tcc built AND the
# csmith runtime headers available (e.g., imacg3 / ibookg37 /
# ibookg38 keep them at /Users/macuser/tmp/csmith):
#
#     ./demos/v0.2.48-r12-clobber.sh
#
# Override the runtime path:
#     CSMITH_INC=/path/to/csmith/runtime ./demos/v0.2.48-r12-clobber.sh
#
# Expected last line:
#     PASS  checksum = 76F5DB56  (gcc/tcc match)
#
# What this demonstrates:
#   The unmodified csmith default-opts seed 732 program. Pre-fix,
#   tcc parked a live vstack value (the int8_t `*l_505` byte
#   inside func_8) in r12 under high register pressure (r3..r12
#   all live at expression peak); the next `lis r12, ha(g_26)`
#   for the rhs of `(*l_505) |= g_26.f0` silently clobbered r12,
#   producing `0x20000 | 7 = 0x20007` stored as byte 0x07 instead
#   of the expected 0xA4 | 7 = 0xA7. End-of-run checksum:
#   `7F5F6E2D` pre-fix vs `76F5DB56` post-fix (matching gcc-4.0
#   -O0).
#
# Fix (session 066): removed r12 from tcc's int register
# allocator pool — `reg_classes[9] = 0` in tcc/ppc-gen.c. Tcc
# now has 9 int allocator slots (r3..r11) instead of 10; r12 is
# pure scratch and the lis-clobber-class is closed.
set -e
cd "$(dirname "$0")/.."
TCC=./tcc/tcc
[ -x "$TCC" ] || { echo "ERROR: $TCC not found; run scripts/build-tiger.sh first" >&2; exit 1; }

CSMITH_INC="${CSMITH_INC:-/Users/macuser/tmp/csmith}"
[ -f "$CSMITH_INC/csmith.h" ] || \
    CSMITH_INC=/Users/macuser/tmp/csmith-runtime
[ -f "$CSMITH_INC/csmith.h" ] || {
    echo "ERROR: csmith runtime headers not found." >&2
    echo "Tried CSMITH_INC + /Users/macuser/tmp/csmith{,-runtime}." >&2
    echo "Set CSMITH_INC=/path/to/csmith/runtime explicitly." >&2
    exit 1
}

SRC="demos/v0.2.48-r12-clobber.c"
OUT=/tmp/v0.2.48-r12-clobber

"$TCC" -B./tcc -L./tcc -I./tcc/include -I"$CSMITH_INC" -w "$SRC" -o "$OUT"

result=$("$OUT" 1 | tail -1)
expected="checksum = 76F5DB56"

if [ "$result" = "$expected" ]; then
    echo "PASS  $result  (gcc/tcc match)"
    exit 0
else
    echo "FAIL  expected: $expected"
    echo "FAIL  got:      $result"
    echo "FAIL  bug back? this is what session 066 fixed."
    exit 1
fi
