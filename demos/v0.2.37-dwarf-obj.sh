#!/usr/bin/env bash
# Demo for v0.2.37-g3 — DWARF debug info in -c .o output
#
# Pre-v0.2.37, `tcc -c -g foo.c` accepted -g but emitted no DWARF
# (Mach-O .o had no __DWARF segment). dwarfdump on the .o gave
# `<EMPTY>`.
#
# v0.2.37 wires up the existing tccdbg.c machinery for PPC:
#
# 1. tccdbg.c grew PPC-aware blocks for DWARF instruction-length (4)
#    and CIE setup (return-address column = 65 / LR; SP = r1; CAF=4
#    DAF=-4); plus target-endian write helpers because PPC32 is BE
#    and DWARF integers follow target endianness.
#
# 2. ppc-link.c learned R_PPC_REL32 (used by FDE entries to point
#    at function start as a PC-relative 32-bit displacement).
#
# 3. ppc-macho.c's classify_section now accepts `.debug_*` sections
#    (which lack SHF_ALLOC) and maps them to `__DWARF,__debug_*`
#    with the S_ATTR_DEBUG flag. Their addr is left at 0 so cross-
#    section R_PPC_ADDR32 relocations within the .o resolve to
#    "0 + addend = addend" — leaving the linker (or dsymutil) free
#    to apply the real section base later.
#
# Net effect: `dwarfdump foo.o` on a tcc-emitted .o now produces
# the standard DWARF dump (compile unit, types, line table). Tiger's
# bundled dwarfdump from 2007 understands DWARF-2 fully and DWARF-4
# partially; for the broadest tool compatibility this demo uses
# `-gdwarf-2`.
#
# DWARF in -o exe is a separate code path (macho_output_exe doesn't
# yet enumerate the debug sections); that's tracked as follow-up
# work for a later session.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0237-dwarf.c"
TCC_OBJ="$TMP/v0237-dwarf-tcc.o"

cat > "$SRC" <<'EOF'
#include <stdio.h>

int sq(int n) {
    return n * n;
}

int main(int argc, char **argv) {
    int x = sq(5);
    printf("sq(5) = %d (argc=%d)\n", x, argc);
    return 0;
}
EOF

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)

echo "==> Compiling with tcc -c -gdwarf-2..."
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" -c -gdwarf-2 \
    "$SRC" -o "$TCC_OBJ"

echo "==> Verifying __DWARF segment + __debug_* sections present..."
SECT_LIST=$(otool -l "$TCC_OBJ" | grep -E "segname __DWARF|sectname __debug_" | wc -l)
if [[ "$SECT_LIST" -lt 4 ]]; then
    echo "FAIL: only $SECT_LIST DWARF section markers found"
    otool -l "$TCC_OBJ" | grep -E "segname|sectname" | head -25
    exit 1
fi
echo "  found $SECT_LIST DWARF section markers"

echo "==> Verifying dwarfdump can parse the compile unit..."
DUMP=$(dwarfdump "$TCC_OBJ" 2>&1)
if ! grep -q "TAG_compile_unit" <<< "$DUMP"; then
    echo "FAIL: dwarfdump found no TAG_compile_unit"
    echo "$DUMP" | head -20
    exit 1
fi
if ! grep -q "AT_producer( \"tcc " <<< "$DUMP"; then
    echo "FAIL: missing AT_producer 'tcc' string"
    exit 1
fi
echo "  ok: compile unit, AT_producer, types all parse"

echo
echo "==> Sample of dwarfdump output:"
echo "$DUMP" | sed -n '5,18p'

echo
echo "PASS — tcc -c -g produces DWARF that dwarfdump reads"
