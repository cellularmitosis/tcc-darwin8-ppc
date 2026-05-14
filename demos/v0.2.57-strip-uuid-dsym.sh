#!/bin/sh
# v0.2.57-strip-uuid-dsym.sh — verify the strip-and-debug-via-dSYM
# polish landed in v0.2.57-g3 (session 078).  Roadmap follow-up to
# #7.5 Phase 2 item 1b.
#
# What v0.2.57 added (all macho_output_exe-side, all PPC32-narrow):
#
#   1. __DWARF segment file-laid before __LINKEDIT (was: after) and
#      page-padded so __LINKEDIT.fileoff lands page-aligned.  This
#      lets cctools `strip -S` accept the image — pre-fix it bailed
#      with "the __LINKEDIT segment does not cover the end of the
#      file (can't be processed)" on every -gdwarf-2 build.  Side
#      effect: __LINKEDIT's interior order also changed to
#      symtab → indirect → strtab (gcc-4.0's order, what Tiger strip
#      enforces — "symbol table out of place" otherwise).
#
#   2. LC_UUID emitted on every linked exe with a deterministic
#      16-byte content fingerprint (FNV-1a 64-bit twice over post-
#      relocation text/rodata/data/dwarf bytes, RFC 4122 v4 marker
#      bits set).  dsymutil propagates the same UUID into the
#      .dSYM bundle, so Tiger gdb 6.3 pairs the stripped exe with
#      its companion .dSYM by UUID match — no more "no debug info"
#      after `strip -S`.
#
#   3. N_SOL stab inserted between N_FUN(name) and N_FUN(size) in
#      each per-function debug-map bracket, matching gcc-4.0's
#      chain shape.  This brings the chain format to gcc-4.0
#      parity but does NOT yet fix dsymutil's full .debug_info
#      consolidation (still empty in the .dSYM, only pubnames
#      survive — pre-existing limitation; see HANDOFF for v0.2.58).
#
# The demo asserts each piece end-to-end.  Multi-TU build (main +
# helpers) so the OSO chain has more than one entry.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}

DEMODIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_SRC="$DEMODIR/v0.2.57-strip-uuid-dsym.c"
HELP_SRC="$DEMODIR/v0.2.57-strip-uuid-dsym-helpers.c"

MAIN_O="$TMP/v0257-main.o"
HELP_O="$TMP/v0257-helpers.o"
EXE="$TMP/v0257-exe"
DSYM="$TMP/v0257-exe.dSYM"

if [ ! -x "$TCC" ]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCCFLAGS="-B$TCCROOT -L$TCCROOT -I$TCCROOT/include"

rm -rf "$DSYM"

echo "==> [build] -gdwarf-2 two-step compile + link..."
"$TCC" $TCCFLAGS -gdwarf-2 -c "$MAIN_SRC" -o "$MAIN_O"
"$TCC" $TCCFLAGS -gdwarf-2 -c "$HELP_SRC" -o "$HELP_O"
"$TCC" $TCCFLAGS -gdwarf-2 "$MAIN_O" "$HELP_O" -o "$EXE"

echo "==> [build] Smoke run..."
out=$("$EXE")
if [ "$out" != "sum=7 prod=35 argc=1" ]; then
    echo "FAIL: linked exe output mismatch: got '$out'"
    exit 1
fi
echo "  $out  ok"

echo "==> [layout] Asserting __DWARF appears before __LINKEDIT..."
seg_order=$(otool -l "$EXE" | awk '/segname __DWARF/ {print "DWARF"} /segname __LINKEDIT/ {print "LINKEDIT"}' \
            | tr '\n' ' ' | sed 's/ *$//')
case "$seg_order" in
    *"DWARF LINKEDIT"*) echo "  __DWARF before __LINKEDIT  ok" ;;
    *)
        echo "FAIL: expected __DWARF before __LINKEDIT, got: $seg_order"
        exit 1
        ;;
esac

echo "==> [layout] Asserting __LINKEDIT covers end of file..."
le_off=$(otool -l "$EXE" | awk '
    /segname __LINKEDIT/   {in_le=1; next}
    in_le && /fileoff/     {fo=$2}
    in_le && /filesize/    {fs=$2; printf "%d\n", fo+fs; exit}
')
file_size=$(wc -c < "$EXE" | tr -d ' ')
if [ "$le_off" != "$file_size" ]; then
    echo "FAIL: __LINKEDIT end ($le_off) != file size ($file_size)"
    exit 1
fi
echo "  __LINKEDIT covers bytes [..,$file_size)  ok"

echo "==> [uuid] Asserting LC_UUID present..."
uuid_block=$(otool -l "$EXE" | awk '/cmd LC_UUID/ {f=1; next} f && /uuid/ {print; getline; print; exit}')
if [ -z "$uuid_block" ]; then
    echo "FAIL: LC_UUID load command missing"
    exit 1
fi
uuid_exe=$(echo "$uuid_block" | tr -s ' \t\n' ' ' | sed 's/^ *//;s/ *$//' | sed 's/uuid //')
echo "  exe LC_UUID = $uuid_exe  ok"

echo "==> [uuid] Asserting LC_UUID is non-zero (real content fingerprint)..."
zero="0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00"
if [ "$uuid_exe" = "$zero" ]; then
    echo "FAIL: LC_UUID is all zeros"
    exit 1
fi
echo "  non-zero  ok"

echo "==> [uuid] Asserting LC_UUID is deterministic (same source -> same UUID)..."
"$TCC" $TCCFLAGS -gdwarf-2 -c "$MAIN_SRC" -o "$MAIN_O"
"$TCC" $TCCFLAGS -gdwarf-2 -c "$HELP_SRC" -o "$HELP_O"
"$TCC" $TCCFLAGS -gdwarf-2 "$MAIN_O" "$HELP_O" -o "$EXE.bis"
uuid_bis=$(otool -l "$EXE.bis" | awk '/cmd LC_UUID/ {f=1; next} f && /uuid/ {print; getline; print; exit}' \
           | tr -s ' \t\n' ' ' | sed 's/^ *//;s/ *$//' | sed 's/uuid //')
if [ "$uuid_exe" != "$uuid_bis" ]; then
    echo "FAIL: rebuild produced different UUID:"
    echo "  first:  $uuid_exe"
    echo "  second: $uuid_bis"
    exit 1
fi
rm -f "$EXE.bis"
echo "  rebuild reproduces same UUID  ok"

echo "==> [dsymutil] Producing .dSYM..."
dsymutil "$EXE" -o "$DSYM"
if [ ! -d "$DSYM" ]; then
    echo "FAIL: dsymutil did not produce $DSYM"
    exit 1
fi
echo "  $DSYM  ok"

echo "==> [uuid] Asserting .dSYM carries the same LC_UUID..."
uuid_dsym=$(otool -l "$DSYM/Contents/Resources/DWARF/v0257-exe" \
            | awk '/cmd LC_UUID/ {f=1; next} f && /uuid/ {print; getline; print; exit}' \
            | tr -s ' \t\n' ' ' | sed 's/^ *//;s/ *$//' | sed 's/uuid //')
if [ "$uuid_exe" != "$uuid_dsym" ]; then
    echo "FAIL: UUID mismatch between exe and .dSYM:"
    echo "  exe:  $uuid_exe"
    echo "  dSYM: $uuid_dsym"
    exit 1
fi
echo "  exe UUID == .dSYM UUID  ok"

echo "==> [chain] N_SOL stab in OSO bracket (gcc-4.0 chain-format parity)..."
sol_count=$(nm -ap "$EXE" | grep -c " SOL " || true)
if [ "$sol_count" -lt 3 ]; then
    echo "FAIL: expected >=3 N_SOL records (one per function), got $sol_count"
    nm -ap "$EXE" | grep " SOL " | head
    exit 1
fi
echo "  $sol_count SOL records (>=3 = main + helper_add + helper_mul)  ok"

echo "==> [strip] strip -S accepts the layout..."
# Strip the exe in place so its basename keeps matching the .dSYM
# inner DWARF filename (gdb pairs by `<exe>.dSYM/Contents/Resources/
# DWARF/<basename>` lookup, where <basename> is the exe's filename).
strip -S "$EXE"
echo "  strip -S exited 0  ok"

echo "==> [strip] Stripped exe still runs..."
out=$("$EXE")
if [ "$out" != "sum=7 prod=35 argc=1" ]; then
    echo "FAIL: stripped exe output mismatch: got '$out'"
    exit 1
fi
echo "  $out  ok"

echo "==> [strip] LC_UUID survives strip..."
uuid_after=$(otool -l "$EXE" | awk '/cmd LC_UUID/ {f=1; next} f && /uuid/ {print; getline; print; exit}' \
             | tr -s ' \t\n' ' ' | sed 's/^ *//;s/ *$//' | sed 's/uuid //')
if [ "$uuid_exe" != "$uuid_after" ]; then
    echo "FAIL: LC_UUID changed after strip:"
    echo "  before: $uuid_exe"
    echo "  after:  $uuid_after"
    exit 1
fi
echo "  unchanged across strip  ok"

echo "==> [gdb] Tiger gdb pairs stripped exe with .dSYM via UUID..."
# gdb pairs by `<exe>.dSYM/Contents/Resources/DWARF/<basename(exe)>`
# lookup, then matches LC_UUID.  The .dSYM was produced from the
# pre-strip exe, but its inner DWARF file is named after the exe's
# basename — and we stripped in place, so basename still matches.
cat > "$TMP/v0257-gdb.cmds" <<EOF
file $EXE
info functions main
quit
EOF
gdb_out=$(gdb -batch -x "$TMP/v0257-gdb.cmds" 2>&1)
case "$gdb_out" in
    *"int main"*)
        echo "  gdb reads symbols from .dSYM (info functions main)  ok"
        ;;
    *)
        echo "FAIL: gdb did not resolve 'main' from .dSYM. Output:"
        echo "$gdb_out"
        exit 1
        ;;
esac

# Cleanup
rm -rf "$DSYM"
rm -f "$EXE" "$MAIN_O" "$HELP_O" "$TMP/v0257-gdb.cmds"

echo
echo "OK: __DWARF reorder + page-padding makes strip -S work; LC_UUID"
echo "    round-trips through dsymutil, survives strip, and lets gdb"
echo "    pair the stripped exe with its .dSYM."
