#!/bin/sh
# v0.2.56-n-oso-dsymutil.sh — verify N_OSO debug-map emission +
# dsymutil-on-Tiger consumption (roadmap #7.5 Phase 2 item 1b).
#
# Two TUs (main + helpers).  The session's debug-mode story is now:
#
#   -gstabs (or post-v0.2.55 bare -g):
#       Stab chain in the linked exe's LC_SYMTAB carries
#       SO/SO/SLINE/FUN/PSYM/LSYM/BNSYM/ENSYM directly — Tiger gdb 6.3
#       reads file:line, source listing, params/locals out of the exe
#       without any companion .o or .dSYM.  We DO NOT emit N_OSO here:
#       Tiger gdb tries to re-read stabs from the .o via the OSO
#       record, and tcc places .o stabs in `__DWARF,__stab` rather
#       than `LC_SYMTAB` (the placement gcc-4.0 uses), so gdb's .o
#       reader SIGBUSes if pointed at one.  The stabs-only flow is
#       unchanged from v0.2.51-v0.2.55.
#
#   -gdwarf-2:
#       Linker synthesizes a freestanding debug-map chain in
#       `LC_SYMTAB` for each loaded .o — one
#           N_SO source / N_OSO objpath /
#           {N_BNSYM N_FUN(name) N_FUN(""=size) N_ENSYM} per function /
#           N_SO ""
#       sequence.  `dsymutil` walks that chain, follows each N_OSO
#       to the on-disk .o (mtime-verified), reads the .o's DWARF, and
#       writes a consolidated .dSYM bundle.  Pre-v0.2.56 dsymutil
#       crashed with `failed assertion '!"Unknown one-operand"'`
#       because tcc emitted DW_OP_call_frame_cfa (a DWARF-3 op) under
#       -gdwarf-2; v0.2.56 switches PPC frame-base emission to
#       DW_OP_reg31 (a DWARF-2 op, the post-prolog FP) which Tiger's
#       cctools dwarf_utilities-42 handles cleanly.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}

DEMODIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_SRC="$DEMODIR/v0.2.56-n-oso-dsymutil.c"
HELP_SRC="$DEMODIR/v0.2.56-n-oso-dsymutil-helpers.c"

MAIN_O_STABS="$TMP/v0256-main-stabs.o"
HELP_O_STABS="$TMP/v0256-helpers-stabs.o"
EXE_STABS="$TMP/v0256-exe-stabs"

MAIN_O_DW="$TMP/v0256-main-dw.o"
HELP_O_DW="$TMP/v0256-helpers-dw.o"
EXE_DW="$TMP/v0256-exe-dw"

if [ ! -x "$TCC" ]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCCFLAGS="-B$TCCROOT -L$TCCROOT -I$TCCROOT/include"

echo "==> [stabs] Building two-step -gstabs flow..."
"$TCC" $TCCFLAGS -gstabs -c "$MAIN_SRC" -o "$MAIN_O_STABS"
"$TCC" $TCCFLAGS -gstabs -c "$HELP_SRC" -o "$HELP_O_STABS"
"$TCC" $TCCFLAGS -gstabs "$MAIN_O_STABS" "$HELP_O_STABS" -o "$EXE_STABS"

echo "==> [stabs] Smoke run..."
out=$("$EXE_STABS")
if [ "$out" != "sum=7 prod=35 argc=1" ]; then
    echo "FAIL: stabs-mode run output mismatch: got '$out'"
    exit 1
fi
echo "  $out  ok"

echo "==> [stabs] Asserting no N_OSO records (would crash Tiger gdb)..."
osocnt=$(nm -ap "$EXE_STABS" | grep -c " OSO " || true)
if [ "$osocnt" -ne 0 ]; then
    echo "FAIL: stabs build has $osocnt OSO records; should be 0"
    nm -ap "$EXE_STABS" | grep " OSO " | head
    exit 1
fi
echo "  zero OSO records in stabs build  ok"

echo "==> [stabs] Tiger gdb reads source/line directly (no .dSYM)..."
echo "info line main" > "$TMP/v0256-gdb.cmd"
echo "list main"     >> "$TMP/v0256-gdb.cmd"
echo "quit"          >> "$TMP/v0256-gdb.cmd"
gdb_out=$(gdb -nx -batch -x "$TMP/v0256-gdb.cmd" "$EXE_STABS" 2>&1)
if ! echo "$gdb_out" | grep -q "starts at address"; then
    echo "FAIL: gdb didn't find source line info on stabs-mode build"
    echo "$gdb_out" | tail
    exit 1
fi
echo "  gdb reads file:line  ok"

echo
echo "==> [dwarf] Building two-step -gdwarf-2 flow..."
"$TCC" $TCCFLAGS -gdwarf-2 -c "$MAIN_SRC" -o "$MAIN_O_DW"
"$TCC" $TCCFLAGS -gdwarf-2 -c "$HELP_SRC" -o "$HELP_O_DW"
"$TCC" $TCCFLAGS -gdwarf-2 "$MAIN_O_DW" "$HELP_O_DW" -o "$EXE_DW"

echo "==> [dwarf] Smoke run..."
out=$("$EXE_DW")
if [ "$out" != "sum=7 prod=35 argc=1" ]; then
    echo "FAIL: dwarf-mode run output mismatch: got '$out'"
    exit 1
fi
echo "  $out  ok"

echo "==> [dwarf] Asserting synthesized SO/OSO/SO chain in exe..."
chain_count=$(nm -ap "$EXE_DW" | grep -cE " (SO|OSO) " || true)
if [ "$chain_count" -lt 6 ]; then
    echo "FAIL: expected >=6 SO/OSO entries (2 TUs * (SO + OSO + SO)), got $chain_count"
    nm -ap "$EXE_DW" | grep -E "OSO|SO " | head -10
    exit 1
fi
echo "  found $chain_count SO/OSO entries  ok"

echo "==> [dwarf] Asserting OSO records point at .o realpaths..."
for oso_path in "$MAIN_O_DW" "$HELP_O_DW"; do
    rp=$(perl -MCwd=realpath -E 'say realpath($ARGV[0])' "$oso_path")
    if ! nm -ap "$EXE_DW" | grep -q " OSO $rp\$"; then
        echo "FAIL: OSO record for $rp not in exe"
        nm -ap "$EXE_DW" | grep "OSO" | head
        exit 1
    fi
done
echo "  both .o realpaths present in OSO chain  ok"

echo "==> [dwarf] Asserting OSO mtimes non-zero..."
if nm -ap "$EXE_DW" | grep " OSO " | grep -q "^00000000"; then
    echo "FAIL: at least one OSO record has zero mtime"
    nm -ap "$EXE_DW" | grep " OSO " | head
    exit 1
fi
echo "  all OSO mtimes non-zero  ok"

echo "==> [dwarf] Asserting per-function BNSYM/FUN/FUN(size)/ENSYM markers..."
bnsym_count=$(nm -ap "$EXE_DW" | grep -c "BNSYM" || true)
if [ "$bnsym_count" -lt 3 ]; then
    echo "FAIL: expected >=3 BNSYM (main + helper_add + helper_mul), got $bnsym_count"
    exit 1
fi
ensym_count=$(nm -ap "$EXE_DW" | grep -c "ENSYM" || true)
if [ "$ensym_count" -ne "$bnsym_count" ]; then
    echo "FAIL: BNSYM count ($bnsym_count) != ENSYM count ($ensym_count)"
    exit 1
fi
echo "  found $bnsym_count BNSYM/ENSYM pairs (>= 3 = main + helper_add + helper_mul)  ok"

echo "==> [dwarf] Asserting FUN markers carry function names..."
for fn in _main _helper_add _helper_mul; do
    if ! nm -ap "$EXE_DW" | grep "FUN" | grep -q " $fn\$"; then
        echo "FAIL: no FUN marker for $fn"
        nm -ap "$EXE_DW" | grep "FUN" | head
        exit 1
    fi
done
echo "  _main / _helper_add / _helper_mul all present in FUN markers  ok"

echo "==> [dwarf] dsymutil runs cleanly (pre-v0.2.56 it asserted)..."
DSYM="$EXE_DW.dSYM"
rm -rf "$DSYM"
if ! dsymutil "$EXE_DW" -o "$DSYM" 2>&1; then
    echo "FAIL: dsymutil exited with non-zero status"
    exit 1
fi
DSYM_BIN="$DSYM/Contents/Resources/DWARF/$(basename "$EXE_DW")"
if [ ! -f "$DSYM_BIN" ]; then
    echo "FAIL: dsymutil didn't produce a .dSYM bundle"
    exit 1
fi
echo "  dsymutil exited 0 and produced a .dSYM bundle  ok"

echo "==> [dwarf] dSYM contains __debug_pubnames listing the demo functions..."
pn_out=$(dwarfdump --debug-pubnames "$DSYM_BIN" 2>&1)
for fn in main helper_add helper_mul; do
    if ! echo "$pn_out" | grep -q "^0x[0-9a-f]*: $fn\$"; then
        echo "FAIL: no $fn entry in dSYM __debug_pubnames"
        echo "$pn_out" | head
        exit 1
    fi
done
echo "  main / helper_add / helper_mul listed in dSYM pubnames  ok"

echo
echo "OK: -gstabs flow unchanged (Tiger gdb reads exe-resident stabs);"
echo "    -gdwarf-2 emits an N_OSO debug-map chain dsymutil consumes cleanly."
