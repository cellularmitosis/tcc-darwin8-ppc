#!/bin/sh
# Smoke test for session 066's r12 reg-allocator fix.
#
# Usage:
#   ssh <ppc-host> 'cd /Users/macuser/tcc-darwin8-ppc && bash docs/sessions/066-seed-732-2026-05-11/smoke.sh'
#
# Or from uranium:
#   ssh imacg3 'bash -s' < docs/sessions/066-seed-732-2026-05-11/smoke.sh
#
# Asserts:
#   - tcc compiles seed-732.c without error
#   - gcc-4.0 -O0 and tcc produce byte-identical per-global checksum
#     streams (specifically agreeing at g_359.f0, the original
#     divergence point pre-fix).

set -u

TCC=${TCC:-/Users/macuser/tcc-darwin8-ppc/tcc/tcc}
GCC=${GCC:-/usr/bin/gcc-4.0}
INC=${INC:-/Users/macuser/tmp/csmith-runtime}

SRC=/Users/macuser/tmp/seed-732.c
if [ ! -f "$SRC" ]; then
    SRC=/Users/macuser/tmp/csmith-default-1k/seed-732.c
fi
if [ ! -f "$SRC" ]; then
    echo "ERROR: cannot find seed-732.c (checked /Users/macuser/tmp/ and csmith-default-1k/)" >&2
    exit 1
fi

TMP=$(mktemp -d /tmp/smoke066.XXXXXX)
trap 'rm -rf $TMP' EXIT

"$GCC" -O0 -w -I"$INC" "$SRC" -o "$TMP/gcc.exe" 2>/dev/null || {
    echo "ERROR: gcc-4.0 compile failed"; exit 1; }
"$TCC" -B/Users/macuser/tcc-darwin8-ppc/tcc \
       -I/Users/macuser/tcc-darwin8-ppc/tcc/include \
       -I"$INC" "$SRC" -o "$TMP/tcc.exe" 2>/dev/null || {
    echo "ERROR: tcc compile failed"; exit 1; }

"$TMP/gcc.exe" 1 > "$TMP/g.out"
"$TMP/tcc.exe" 1 > "$TMP/t.out"

if diff -q "$TMP/g.out" "$TMP/t.out" >/dev/null 2>&1; then
    g=$(grep '^checksum = ' "$TMP/g.out")
    echo "PASS  $g  (gcc/tcc byte-identical)"
    exit 0
fi

echo "FAIL  gcc/tcc output diverges:"
diff "$TMP/g.out" "$TMP/t.out" | head -10
exit 1
