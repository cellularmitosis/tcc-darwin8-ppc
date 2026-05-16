#!/usr/bin/env bash
# Demo for v0.2.65-g3 — `.rodata` with undef-ADDR32 relocs is routed
# to `__DATA,__const` so dyld can bind slot values at load time.
# Pointer equality with `&extern_fn` then holds across the table
# slot, a writable static, and a local-var init — gcc-4.0 parity.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0265-rodata-data-const"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
SRC="$(dirname "$0")/v0.2.65-rodata-data-const.c"

rm -rf "$WORK"
mkdir -p "$WORK"

echo "==> Compile via .o roundtrip..."
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
    -c -o "$WORK/dataconst.o" "$SRC"
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
    -o "$WORK/dataconst_app" "$WORK/dataconst.o"

echo "==> __const should sit in __DATA (not __TEXT)..."
CONST_SEG=$(otool -l "$WORK/dataconst_app" \
    | awk '/sectname __const/{getline; print $2; exit}')
if [[ "$CONST_SEG" != "__DATA" ]]; then
    echo "FAIL: expected __const in __DATA, got $CONST_SEG"
    exit 1
fi
echo "  __const segname = $CONST_SEG"

echo "==> An extrel against _isalpha should target __DATA,__const..."
# Find the address+size of __DATA,__const.
CONST_ADDR_HEX=$(otool -l "$WORK/dataconst_app" \
    | awk '/sectname __const/{found=1; next} found && $1=="addr"{print $2; exit}')
CONST_SIZE_HEX=$(otool -l "$WORK/dataconst_app" \
    | awk '/sectname __const/{found=1; next} found && $1=="size"{print $2; exit}')
CONST_ADDR=$(( CONST_ADDR_HEX ))
CONST_END=$(( CONST_ADDR + CONST_SIZE_HEX ))
# Walk external relocs in pure bash (Tiger awk has no strtonum).
HIT=no
while read -r ADDR_HEX REST; do
    case "$REST" in *_isalpha*) ;; *) continue ;; esac
    case "$ADDR_HEX" in [0-9a-f]*) ;; *) continue ;; esac
    ADDR=$((16#$ADDR_HEX))
    if (( ADDR >= CONST_ADDR && ADDR < CONST_END )); then
        HIT=yes
        printf '  extrel at 0x%08x → _isalpha (inside __DATA,__const [0x%x,0x%x))\n' \
            "$ADDR" "$CONST_ADDR" "$CONST_END"
        break
    fi
done < <(otool -rv "$WORK/dataconst_app" 2>/dev/null \
          | awk 'NF>=7 {print $1, $0}')
if [[ "$HIT" != "yes" ]]; then
    echo "FAIL: no _isalpha extrel found targeting __DATA,__const"
    otool -rv "$WORK/dataconst_app" 2>&1 | head -20
    exit 1
fi

echo "==> Running..."
"$WORK/dataconst_app"

echo
echo "PASS — moved __DATA,__const + extrel gives gcc-4.0 pointer-equality parity"
