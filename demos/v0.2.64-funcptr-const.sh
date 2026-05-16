#!/usr/bin/env bash
# Demo for v0.2.64-g3 — funcptr-in-__const works across .o roundtrip
#
# Verifies the fix described in docs/sessions/087-stt-func-roundtrip-...
# compiles via .o roundtrip (forces the STT_FUNC loss path that the
# fix reconstructs), inspects the linked exe to confirm the table
# holds stub VAs in __TEXT,__symbol_stub rather than slot addresses
# in __DATA,__nl_symbol_ptr, then runs it.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0264-funcptr-const"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
SRC="$(dirname "$0")/v0.2.64-funcptr-const.c"

rm -rf "$WORK"
mkdir -p "$WORK"

echo "==> Compile via .o roundtrip (forces STT_FUNC loss path)..."
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
    -c -o "$WORK/funcptr.o" "$SRC"
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
    -o "$WORK/funcptr_app" "$WORK/funcptr.o"

echo "==> Verifying table entries point into __symbol_stub, not __nl_symbol_ptr..."
# Parse otool -l for the section addresses. Tiger awk lacks strtonum,
# so use printf "%d" 0x... via bash arithmetic.
get_sect_addr() {
    otool -l "$1" | awk -v want="$2" '
        $1=="sectname" && $2==want {found=1; next}
        found && $1=="addr" {print $2; exit}'
}
get_sect_size() {
    otool -l "$1" | awk -v want="$2" '
        $1=="sectname" && $2==want {found=1; next}
        found && $1=="size" {print $2; exit}'
}
STUB_ADDR_HEX=$(get_sect_addr "$WORK/funcptr_app" __symbol_stub)
STUB_SIZE_HEX=$(get_sect_size "$WORK/funcptr_app" __symbol_stub)
NLPTR_ADDR_HEX=$(get_sect_addr "$WORK/funcptr_app" __nl_symbol_ptr)
STUB_START=$((STUB_ADDR_HEX))
STUB_END=$((STUB_ADDR_HEX + STUB_SIZE_HEX))
NLPTR_START=$((NLPTR_ADDR_HEX))

# Read first table entry: in __TEXT,__const, words are
#   W0=name_ptr W1=fn_ptr W2=name_ptr W3=fn_ptr ...
DUMP=$(otool -s __TEXT __const "$WORK/funcptr_app" | head -3 | tail -1)
FN0_HEX="0x$(echo "$DUMP" | awk '{print $3}')"
FN1_HEX="0x$(echo "$DUMP" | awk '{print $5}')"
FN0=$((FN0_HEX))
FN1=$((FN1_HEX))

printf '  __symbol_stub:   [0x%x, 0x%x)\n' "$STUB_START" "$STUB_END"
printf '  __nl_symbol_ptr: 0x%x\n' "$NLPTR_START"
printf '  prednames[0].fn = 0x%x\n' "$FN0"
printf '  prednames[1].fn = 0x%x\n' "$FN1"

if (( FN0 < STUB_START || FN0 >= STUB_END )); then
    printf 'FAIL: prednames[0].fn (0x%x) is outside __symbol_stub [0x%x, 0x%x)\n' \
        "$FN0" "$STUB_START" "$STUB_END"
    if (( FN0 >= NLPTR_START )); then
        echo "       (it points at __nl_symbol_ptr — the pre-fix bug)"
    fi
    exit 1
fi
echo "  table entries lie in __symbol_stub ✓"

echo "==> Running..."
"$WORK/funcptr_app"
echo
echo "PASS — funcptr-in-__const tables resolve via stub VAs across .o roundtrip"
