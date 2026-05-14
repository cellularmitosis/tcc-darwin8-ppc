#!/bin/sh
# v0.2.58-dsym-source-lines.sh — verify the source-line-debugging-via-
# `.dSYM` polish landed in v0.2.58-g3 (session 079).  Roadmap follow-up
# to #7.5 Phase 2 item 1b (last piece).
#
# What v0.2.58 closes:
#
# Pre-v0.2.58 (v0.2.57 baseline), `dsymutil exe -o exe.dSYM` on a tcc-
# built `-gdwarf-2` exe produced a `.dSYM` whose `.debug_info`,
# `.debug_abbrev`, `.debug_line`, and `.debug_str` were all reported as
# empty by `dwarfdump`.  Only `.debug_pubnames` was populated, so gdb
# could `info functions main` and `break *main` but `gdb list main` /
# `gdb break file:LINE` failed with "No line number known for main".
#
# Root cause: tcc emits a `<string>` link-time compile-unit for the
# keymgr stub (the dyld-init-order fix) into the linker's `.debug_*`
# sections.  Those sections were then baked into a `__DWARF` segment
# on the linked exe.  When `dsymutil` ran, it preserved the exe's
# (small) `__DWARF` AND appended a second `__DWARF` segment carrying
# the consolidated content from the OSO chain.  `dwarfdump` and Tiger
# `gdb` iterate segments in load-command order and read the FIRST
# `__DWARF` they find — the small keymgr one, with `.debug_info`
# bytes that don't cover the user CUs.  They reported `< EMPTY >` and
# never looked at the second segment.
#
# v0.2.58 fix (tccelf.c `tcc_output_file`): save and clear
# `s->do_debug` around the keymgr-stub `tcc_compile_string` call, so
# the stub compiles without writing a CU.  The user's own `.o` files
# don't contribute to the exe's `__DWARF` (the loader drops
# `__DWARF,__debug_*` sections; debug info flows via the OSO chain
# instead), so with the keymgr stub silenced too, the linker's
# `.debug_*` sections stay empty in a two-step build.
# `macho_output_exe`'s `s->data_offset == 0` filter then drops the
# `__DWARF` segment entirely — gcc-4.0 parity.  `dsymutil` adds a
# single `__DWARF` to the `.dSYM`, and gdb / dwarfdump can read it.
#
# Single-step builds (`tcc -gdwarf-2 hello.c -o exe`) still ship the
# user CU in the exe's `__DWARF`, because the in-process compile
# writes it directly into the linker's `.debug_*` sections (no .o
# intermediary).  That keeps `dwarfdump exe` working for one-shot
# tests.  The dsymutil flow is two-step territory.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}

DEMODIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_SRC="$DEMODIR/v0.2.58-dsym-source-lines.c"
HELP_SRC="$DEMODIR/v0.2.58-dsym-source-lines-helpers.c"

MAIN_O="$TMP/v0258-main.o"
HELP_O="$TMP/v0258-helpers.o"
EXE="$TMP/v0258-exe"
DSYM="$TMP/v0258-exe.dSYM"

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

echo "==> [layout] Asserting NO __DWARF segment in the linked exe..."
n_dwarf=$(otool -l "$EXE" | grep -c "segname __DWARF" || true)
if [ "$n_dwarf" -ne 0 ]; then
    echo "FAIL: linked exe still has __DWARF segment ($n_dwarf segname __DWARF lines):"
    otool -l "$EXE" | grep -E "cmd LC_SEGMENT|segname"
    exit 1
fi
echo "  no __DWARF segment in exe  ok"

echo "==> [dsymutil] Producing .dSYM..."
dsymutil "$EXE" -o "$DSYM"
if [ ! -d "$DSYM" ]; then
    echo "FAIL: dsymutil did not produce $DSYM"
    exit 1
fi
echo "  $DSYM  ok"

INNER="$DSYM/Contents/Resources/DWARF/$(basename "$EXE")"

echo "==> [dsymutil] Asserting .dSYM has exactly ONE __DWARF segment..."
n_dsym_segs=$(otool -l "$INNER" | grep -c "cmd LC_SEGMENT")
# Count distinct __DWARF SEGMENTS (load command lines), not section listings.
n_dwarf_segs=$(otool -l "$INNER" | awk '
    /^Load command/ { in_seg=0 }
    /cmd LC_SEGMENT/ { in_lc=1 }
    in_lc && /segname __DWARF/ && !in_seg { n++; in_seg=1; in_lc=0 }
    END { print n+0 }
')
if [ "$n_dwarf_segs" -ne 1 ]; then
    echo "FAIL: expected exactly one __DWARF segment in .dSYM, got $n_dwarf_segs"
    otool -l "$INNER" | grep -E "cmd LC_SEGMENT|segname __DWARF|sectname __debug"
    exit 1
fi
echo "  exactly one __DWARF segment  ok"

echo "==> [dsymutil] Asserting .dSYM .debug_info is non-empty (dwarfdump can read it)..."
if ! dwarfdump --debug-info "$INNER" 2>&1 | grep -q "TAG_compile_unit"; then
    echo "FAIL: dwarfdump found no TAG_compile_unit in .dSYM .debug_info"
    dwarfdump --debug-info "$INNER" 2>&1 | head -10
    exit 1
fi
echo "  dwarfdump reads TAG_compile_unit from .dSYM  ok"

echo "==> [dsymutil] Asserting both .o source files appear as AT_name in .dSYM..."
dump=$(dwarfdump --debug-info "$INNER")
for src in v0.2.58-dsym-source-lines.c v0.2.58-dsym-source-lines-helpers.c; do
    # tcc records the absolute path in AT_name (DWARF DW_AT_name on the
    # compile-unit DIE), so match by suffix.
    if ! grep -q "AT_name( \".*$src\" )" <<EOF
$dump
EOF
    then
        echo "FAIL: source file '$src' not in .dSYM .debug_info"
        echo "$dump" | grep "AT_name" | head -10
        exit 1
    fi
done
echo "  main + helpers compile-units both present  ok"

echo "==> [strip] strip -S accepts the layout..."
strip -S "$EXE"
echo "  strip -S exited 0  ok"

echo "==> [strip] Stripped exe still runs..."
out=$("$EXE")
if [ "$out" != "sum=7 prod=35 argc=1" ]; then
    echo "FAIL: stripped exe output mismatch: got '$out'"
    exit 1
fi
echo "  $out  ok"

echo "==> [gdb] gdb list main from .dSYM (post-strip)..."
cat > "$TMP/v0258-gdb-list.cmds" <<EOF
file $EXE
list main
quit
EOF
gdb_out=$(gdb -batch -x "$TMP/v0258-gdb-list.cmds" 2>&1)
# "list main" prints the source lines around the function definition.
# We assert the function signature line shows up.
case "$gdb_out" in
    *"int main(int argc"*)
        echo "  gdb list main resolves source from .dSYM  ok"
        ;;
    *)
        echo "FAIL: gdb list main did not show main's source.  Output:"
        echo "$gdb_out"
        exit 1
        ;;
esac

echo "==> [gdb] gdb list helper_add from .dSYM (post-strip)..."
cat > "$TMP/v0258-gdb-list2.cmds" <<EOF
file $EXE
list helper_add
quit
EOF
gdb_out=$(gdb -batch -x "$TMP/v0258-gdb-list2.cmds" 2>&1)
case "$gdb_out" in
    *"int helper_add"*)
        echo "  gdb list helper_add resolves source from .dSYM  ok"
        ;;
    *)
        echo "FAIL: gdb list helper_add did not show helper_add's source.  Output:"
        echo "$gdb_out"
        exit 1
        ;;
esac

echo "==> [gdb] gdb break <file>:<line> resolves to the right function (post-strip)..."
cat > "$TMP/v0258-gdb-break.cmds" <<EOF
file $EXE
break v0.2.58-dsym-source-lines.c:14
break v0.2.58-dsym-source-lines-helpers.c:4
info breakpoints
quit
EOF
gdb_out=$(gdb -batch -x "$TMP/v0258-gdb-break.cmds" 2>&1)
# Line 14 of the main source is the helper_add call inside main.
# Line 4 of the helpers source is the return inside helper_add.
case "$gdb_out" in
    *"in main at"*"v0.2.58-dsym-source-lines.c:14"*"in helper_add at"*"v0.2.58-dsym-source-lines-helpers.c:4"*)
        echo "  break file:LINE lands in the right function  ok"
        ;;
    *)
        echo "FAIL: break file:LINE did not resolve.  Output:"
        echo "$gdb_out"
        exit 1
        ;;
esac

# Cleanup
rm -rf "$DSYM"
rm -f "$EXE" "$MAIN_O" "$HELP_O" \
    "$TMP/v0258-gdb-list.cmds" "$TMP/v0258-gdb-list2.cmds" "$TMP/v0258-gdb-break.cmds"

echo
echo "OK: keymgr-stub debug suppression keeps the linker's .debug_*"
echo "    sections empty in a two-step build, so the linked exe has no"
echo "    __DWARF segment; dsymutil produces a single-__DWARF .dSYM that"
echo "    Tiger gdb can use for list / break file:LINE source-line debug"
echo "    of a stripped exe."
