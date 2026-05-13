#!/bin/sh
# v0.2.54-g3 milestone — stabs survive `tcc -c` -> link round-trip
# (roadmap #7.5 Phase 2, item 1b prerequisite).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.54-stabs-roundtrip.sh
#
# Expected last line:
#     OK: tcc -gstabs survives the .o round-trip; multi-TU gdb workflow holds.
#
# What this demonstrates:
#   v0.2.51 made `tcc -gstabs <single>.c -o <single>` produce a
#   gdb-debuggable Mach-O — the one-step compile-and-link flow.
#   But `tcc -gstabs -c foo.c -o foo.o && tcc -gstabs foo.o -o foo`
#   (the two-step flow real Makefiles use) silently dropped the
#   stab chain at the .o write: `classify_section` had no Mach-O
#   mapping for `.stab` / `.stabstr`.
#
#   v0.2.54 closes that gap. .stab -> __DWARF,__stab and .stabstr ->
#   __DWARF,__stabstr (mirroring .debug_*). The .o reader knows the
#   reverse mapping, fixes up each .o's n_strx offsets so they
#   index into the merged .stabstr, wires up stab_section + ->link,
#   and threads STT_SECTION-anchored relocs through
#   emit_section_relocs.
#
#   Test plan:
#     1. tcc -gstabs -c each .c -> .o.
#     2. Sanity: each .o has __DWARF,__stab + __DWARF,__stabstr;
#        __stab has non-zero reloc count.
#     3. tcc -gstabs link both .o's -> exe.
#     4. Sanity: linked exe has FUN/SLINE/BNSYM/ENSYM entries in
#        LC_SYMTAB nlist.
#     5. Run the exe (smoke).
#     6. gdb workflow: break main, run, step into helper_add (in
#        the second .o), inspect params, walk bt, continue into
#        helper_mul, print product, finish.
set -e
cd "$(dirname "$0")/.."

TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ../scripts/build-tiger.sh configure || true
)

SRC_MAIN="demos/v0.2.54-stabs-roundtrip.c"
SRC_HELP="demos/v0.2.54-stabs-roundtrip-helpers.c"
O_MAIN=/tmp/v0.2.54-roundtrip-main.o
O_HELP=/tmp/v0.2.54-roundtrip-help.o
OUT=/tmp/v0.2.54-roundtrip

# (1) compile-only, each .c -> .o
"$TCC" -B./tcc -L./tcc -I./tcc/include -gstabs -c -w "$SRC_MAIN" -o "$O_MAIN"
"$TCC" -B./tcc -L./tcc -I./tcc/include -gstabs -c -w "$SRC_HELP" -o "$O_HELP"

# (2) sanity: __DWARF sections present in each .o
echo "== sanity: __DWARF,__stab + __DWARF,__stabstr in each .o =="
for OBJ in "$O_MAIN" "$O_HELP"; do
    SEGSECT=$(otool -X -l "$OBJ" | grep -E "sectname __stab\b|sectname __stabstr\b" | wc -l | tr -d ' ')
    if [ "$SEGSECT" != "2" ]; then
        echo "FAIL: $OBJ missing __stab or __stabstr (count=$SEGSECT)"
        exit 1
    fi
    NRELOC=$(otool -X -l "$OBJ" | awk '/sectname __stab$/,/^Section/' | grep "    nreloc" | awk '{print $2}')
    if [ -z "$NRELOC" ] || [ "$NRELOC" = "0" ]; then
        echo "FAIL: $OBJ __stab has no relocs (nreloc=$NRELOC)"
        exit 1
    fi
    echo "  $OBJ: __stab nreloc=$NRELOC, __stabstr present  ✓"
done

# (3) link the two .o's -> exe
"$TCC" -B./tcc -L./tcc -gstabs "$O_MAIN" "$O_HELP" -o "$OUT"

# (4) sanity: linked exe has the full stab chain
echo
echo "== sanity: stab chain in linked exe LC_SYMTAB =="
for KIND in "SO" "FUN" "SLINE" "BNSYM" "ENSYM"; do
    CNT=$(nm -ap "$OUT" | grep -cE " $KIND ")
    if [ "$CNT" -lt 1 ]; then
        echo "FAIL: $KIND not present in linked exe"
        exit 1
    fi
    echo "  $KIND entries: $CNT  ✓"
done
# helper_add / helper_mul (from the second .o) must appear as FUN entries
nm -ap "$OUT" | grep -q "FUN helper_add:" || { echo "FAIL: helper_add FUN missing"; exit 1; }
nm -ap "$OUT" | grep -q "FUN helper_mul:" || { echo "FAIL: helper_mul FUN missing"; exit 1; }
nm -ap "$OUT" | grep -q "FUN main:"       || { echo "FAIL: main FUN missing";       exit 1; }
echo "  FUN entries cross both .o's (helper_add, helper_mul, main)  ✓"

# (5) smoke-run
echo
echo "== smoke run =="
ACTUAL=$("$OUT")
EXPECTED="a=15 b=30"
[ "$ACTUAL" = "$EXPECTED" ] || {
    echo "FAIL: program output unexpected: $ACTUAL"
    echo "  expected: $EXPECTED"
    exit 1
}
echo "  output: $ACTUAL"

# (6) gdb workflow across .o boundaries
echo
echo "== gdb cross-TU debugging =="
cat > /tmp/v0.2.54-gdb.cmd <<'EOF'
break main
break helper_add
break helper_mul
run
print g_seed
continue
print x
print y
next
print sum
bt
continue
print x
print y
next
print product
bt
quit
EOF
G=$(gdb -batch -nx -x /tmp/v0.2.54-gdb.cmd "$OUT" 2>&1)
echo "$G" | tail -40

# Hits on each breakpoint, in order.
echo "$G" | grep -q "Breakpoint 1, main "       || { echo "FAIL: gdb didn't hit main";       exit 1; }
echo "$G" | grep -q "Breakpoint 2, helper_add " || { echo "FAIL: gdb didn't hit helper_add"; exit 1; }
echo "$G" | grep -q "Breakpoint 3, helper_mul " || { echo "FAIL: gdb didn't hit helper_mul"; exit 1; }

# Param/local prints (g_seed=5 from helpers .o; helper_add(5,10) -> sum=15;
# helper_mul(15,2) -> product=30).
echo "$G" | grep -q '\$1 = 5'   || { echo "FAIL: g_seed != 5";              exit 1; }
echo "$G" | grep -q '\$2 = 5'   || { echo "FAIL: helper_add x != 5";        exit 1; }
echo "$G" | grep -q '\$3 = 10'  || { echo "FAIL: helper_add y != 10";       exit 1; }
echo "$G" | grep -q '\$4 = 15'  || { echo "FAIL: helper_add sum != 15";     exit 1; }
echo "$G" | grep -q '\$5 = 15'  || { echo "FAIL: helper_mul x != 15";       exit 1; }
echo "$G" | grep -q '\$6 = 2'   || { echo "FAIL: helper_mul y != 2";        exit 1; }
echo "$G" | grep -q '\$7 = 30'  || { echo "FAIL: helper_mul product != 30"; exit 1; }

# Backtrace must show both frames with file:line.
echo "$G" | grep -q "helper_add.*roundtrip-helpers.c"      || { echo "FAIL: bt missing helper_add file:line"; exit 1; }
echo "$G" | grep -q "main.*v0.2.54-stabs-roundtrip.c"      || { echo "FAIL: bt missing main file:line";       exit 1; }

echo
echo "OK: tcc -gstabs survives the .o round-trip; multi-TU gdb workflow holds."
