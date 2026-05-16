#!/usr/bin/env bash
# Demo for v0.2.64-g3 — funcptr-in-__const works across .o roundtrip
#
# Originally asserted v0.2.64's stub-trick layout (slot points into
# __TEXT,__symbol_stub). v0.2.65 changed the layout to gcc-4.0's
# convention (rodata with undef-ADDR32 moves to __DATA,__const +
# extrel), so the slot now reads as 0 on disk and is bound to the
# libSystem function VA at load time. Both versions fix the
# pre-v0.2.64 SIGSEGV by making the table entries callable; the
# demo now does the functional check (program runs end-to-end via
# the .o roundtrip path that triggers the STT_FUNC loss) without
# pinning the file-layout detail of which segment __const lives in.

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

echo "==> Inspect __const placement (for the curious; not a hard assert)..."
if otool -l "$WORK/funcptr_app" | awk '/sectname __const/{getline; print}' \
        | grep -q "segname __DATA"; then
    echo "  __const placed in __DATA (v0.2.65 layout — dyld binds via extrel)"
elif otool -l "$WORK/funcptr_app" | awk '/sectname __const/{getline; print}' \
        | grep -q "segname __TEXT"; then
    echo "  __const placed in __TEXT (v0.2.64 layout — stub trick fills the slot)"
fi

echo "==> Running (the canonical sed-style sufferer)..."
"$WORK/funcptr_app"

echo
echo "PASS — funcptr-in-__const tables work end-to-end across .o roundtrip"
