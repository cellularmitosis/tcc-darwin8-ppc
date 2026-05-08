#!/usr/bin/env bash
# Demo for v0.2.38-g3 — DWARF debug info in linked Mach-O exe
#
# Pre-v0.2.38, `tcc -g foo.c -o foo` accepted -g and produced DWARF
# in the intermediate .o (since v0.2.37) but the linked exe had no
# __DWARF segment — macho_output_exe simply didn't enumerate the
# .debug_* sections.
#
# v0.2.38 closes the second half of the DWARF arc:
#
# 1. macho_output_exe walks s1->sections[] for any section whose
#    classify_section() result has S_ATTR_DEBUG, and lays them out
#    into a __DWARF segment placed after __LINKEDIT in the file.
#    The segment carries vmaddr=0/vmsize=0 (debug data is not
#    loaded), real file_offset/file_size, and per-section headers
#    with S_ATTR_DEBUG. dyld leaves it alone.
#
# 2. exe_resolve_section_relocs runs over each .debug_* section
#    just like .text/.rodata/.data. Cross-section R_PPC_ADDR32
#    references within DWARF resolve to "0 + addend = addend"
#    (the section's vmaddr stays 0), matching the .o convention
#    dwarfdump expects. R_PPC_ADDR32 from .debug_info to .text
#    resolves to "text_runtime_va + addend" — the actual runtime
#    VA, which lldb / dwarfdump expect for an exe.
#
# 3. exe_resolve_section_relocs grew an R_PPC_REL32 case (used by
#    .eh_frame FDEs to encode the function start as a PC-relative
#    32-bit displacement). ppc-link.c::relocate's lossy REL32 was
#    fixed in the same edit — both paths now honor the in-place
#    addend.
#
# Tiger's bundled dwarfdump fully understands DWARF-2 and partially
# DWARF-4. -gdwarf-2 keeps the demo legible across the toolchain.
#
# Note on gdb-on-Tiger: Apple's gdb 6.3 expects either OSO STAB
# entries pointing back at .o files OR a separate .dSYM bundle. It
# doesn't read embedded __DWARF segments out of the linked exe
# directly. dwarfdump does, lldb (on later OS releases) does, and
# any DWARF-2-aware tool reading the exe directly does. Generating
# OSO entries is a possible follow-up.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0238-dwarf.c"
EXE="$TMP/v0238-dwarf-exe"

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

echo "==> Linking with tcc -gdwarf-2..."
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" -gdwarf-2 \
    "$SRC" -o "$EXE"

echo "==> Verifying __DWARF segment + __debug_* sections present in exe..."
SECT_LIST=$(otool -l "$EXE" | grep -E "segname __DWARF|sectname __debug_" | wc -l)
if [[ "$SECT_LIST" -lt 4 ]]; then
    echo "FAIL: only $SECT_LIST DWARF section markers found in exe"
    otool -l "$EXE" | grep -E "segname|sectname" | head -25
    exit 1
fi
echo "  found $SECT_LIST DWARF section markers"

echo "==> Verifying exe still runs..."
OUT=$("$EXE")
if [[ "$OUT" != "sq(5) = 25 (argc=1)" ]]; then
    echo "FAIL: exe output unexpected:"
    echo "$OUT"
    exit 1
fi
echo "  exe runs, prints '$OUT'"

echo "==> Verifying dwarfdump parses compile unit + finds 'sq' subprogram..."
DUMP=$(dwarfdump "$EXE" 2>&1)
if ! grep -q "TAG_compile_unit" <<< "$DUMP"; then
    echo "FAIL: dwarfdump found no TAG_compile_unit"
    echo "$DUMP" | head -20
    exit 1
fi
if ! grep -q 'AT_name( "sq" )' <<< "$DUMP"; then
    echo "FAIL: subprogram 'sq' not in DWARF info"
    exit 1
fi
if ! grep -q 'AT_producer( "tcc ' <<< "$DUMP"; then
    echo "FAIL: missing AT_producer 'tcc' string"
    exit 1
fi
echo "  ok: compile unit, AT_producer, sq subprogram all parse"

echo "==> Verifying low_pc / high_pc match real runtime VA..."
# Pick the sq subprogram's low_pc, then check the .text symbol VA from nm.
SQ_LOWPC=$(grep -A 8 'TAG_subprogram' <<< "$DUMP" \
            | grep -A 4 'AT_name( "sq"' \
            | grep 'AT_low_pc' \
            | head -1 \
            | sed 's/.*AT_low_pc( \(0x[0-9a-f]*\) ).*/\1/')
SQ_NMVA=$(nm "$EXE" | awk '$3 == "_sq" { print "0x"$1 }' | head -1)
if [[ -z "$SQ_LOWPC" || -z "$SQ_NMVA" ]]; then
    echo "FAIL: could not extract low_pc=$SQ_LOWPC or nm va=$SQ_NMVA"
    exit 1
fi
if [[ "$SQ_LOWPC" != "$SQ_NMVA" ]]; then
    echo "FAIL: low_pc $SQ_LOWPC != nm VA $SQ_NMVA"
    exit 1
fi
echo "  ok: AT_low_pc($SQ_LOWPC) == nm VA($SQ_NMVA)"

echo "==> Verifying line table reads address ranges..."
LINEDUMP=$(dwarfdump --debug-line "$EXE" 2>&1)
if ! grep -q "DW_LNE_set_address" <<< "$LINEDUMP"; then
    echo "FAIL: line table has no DW_LNE_set_address entries"
    exit 1
fi
echo "  ok: line table has DW_LNE_set_address entries"

echo
echo "==> Sample of dwarfdump --debug-aranges output:"
dwarfdump --debug-aranges "$EXE" | sed -n '5,12p'

echo
echo "PASS — tcc -g -o exe emits a __DWARF segment dwarfdump reads"
