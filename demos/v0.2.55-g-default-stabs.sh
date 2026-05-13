#!/bin/sh
# v0.2.55-g3 milestone — `-g` defaults to stabs on PPC Darwin
# (roadmap #7.5 Phase 2, item 1d).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.55-g-default-stabs.sh
#
# Expected last line:
#     OK: bare -g now emits stabs by default; -gdwarf-2 still emits DWARF.
#
# What this demonstrates:
#   The whole stabs-on-Tiger arc (v0.2.51..v0.2.54) made `tcc -gstabs`
#   produce a `gdb 6.3`-debuggable Mach-O on Tiger PPC across both
#   the one-step and two-step compile/link flows. v0.2.55 makes that
#   the DEFAULT: plain `tcc -g hello.c -o hello && gdb hello` works
#   out of the box without typing `-gstabs`.
#
#   Test plan:
#     1. Build with bare `-g`; confirm exe has FUN/SLINE/BNSYM/ENSYM
#        in LC_SYMTAB and NO __DWARF segment.
#     2. Build same source with `-gdwarf-2`; confirm exe has __DWARF
#        segment and NO stabs entries (regression check — DWARF path
#        still reachable when asked for explicitly).
#     3. gdb -batch on the bare-`-g` build: break in `compute`, run,
#        print params/locals/global, walk bt, assert file:line.
set -e
cd "$(dirname "$0")/.."

TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ../scripts/build-tiger.sh configure || true
)

SRC=demos/v0.2.55-g-default-stabs.c
OUT_STABS=/tmp/v0.2.55-stabs
OUT_DWARF=/tmp/v0.2.55-dwarf

# (1) build with bare `-g` -> expect stabs
"$TCC" -B./tcc -L./tcc -I./tcc/include -g -w "$SRC" -o "$OUT_STABS"

echo "== bare -g build: stab chain in LC_SYMTAB =="
for KIND in "SO" "FUN" "SLINE" "BNSYM" "ENSYM"; do
    CNT=$(nm -ap "$OUT_STABS" | grep -cE " $KIND ")
    if [ "$CNT" -lt 1 ]; then
        echo "FAIL: $KIND not present in bare-\`-g\` exe — default did not flip"
        exit 1
    fi
    echo "  $KIND entries: $CNT  ✓"
done

# v0.2.54 routes `.stab` / `.stabstr` into the `__DWARF` segment too
# (mirroring `.debug_*` -> `__DWARF,__debug_*`), so the discriminator
# between a stabs-emitting build and a DWARF-emitting build is the
# SECTIONS inside `__DWARF`, not the segment itself.
if otool -l "$OUT_STABS" | grep -q "sectname __stab$"; then
    echo "  __DWARF,__stab section present  ✓"
else
    echo "FAIL: bare -g produced no __DWARF,__stab section"
    exit 1
fi
if otool -l "$OUT_STABS" | grep -q "sectname __debug_info"; then
    echo "FAIL: bare -g produced a __debug_info section — default did not flip"
    exit 1
fi
echo "  no __debug_info section present  ✓"

# (2) build with explicit -gdwarf-2 -> expect DWARF, NOT stabs
"$TCC" -B./tcc -L./tcc -I./tcc/include -gdwarf-2 -w "$SRC" -o "$OUT_DWARF"

echo
echo "== explicit -gdwarf-2 build: __DWARF segment present, no stabs =="
if ! otool -l "$OUT_DWARF" | grep -q "segname __DWARF"; then
    echo "FAIL: -gdwarf-2 did NOT produce a __DWARF segment"
    exit 1
fi
echo "  __DWARF segment present  ✓"

if otool -l "$OUT_DWARF" | grep -q "sectname __debug_info"; then
    echo "  __DWARF,__debug_info present  ✓"
else
    echo "FAIL: -gdwarf-2 has no __debug_info section"
    exit 1
fi

# stabs entries in LC_SYMTAB must be absent for the DWARF build
for KIND in "FUN" "SLINE"; do
    CNT=$(nm -ap "$OUT_DWARF" | grep -cE " $KIND " || true)
    if [ "$CNT" -gt 0 ]; then
        echo "FAIL: $KIND stab entries present in -gdwarf-2 build ($CNT)"
        exit 1
    fi
done
echo "  no stabs entries in -gdwarf-2 build  ✓"

# (3) smoke-run both builds
echo
echo "== smoke run =="
for E in "$OUT_STABS" "$OUT_DWARF"; do
    OUT=$("$E")
    [ "$OUT" = "result=26" ] || {
        echo "FAIL: $E output unexpected: $OUT (expected result=26)"
        exit 1
    }
done
echo "  both exes print result=26  ✓"

# (4) gdb workflow on bare-`-g` build
echo
echo "== gdb workflow on bare -g build =="
cat > /tmp/v0.2.55-gdb.cmd <<'EOF'
break compute
run
print x
print y
next
print sum
next
print prod
print g_seed
bt
quit
EOF
G=$(gdb -batch -nx -x /tmp/v0.2.55-gdb.cmd "$OUT_STABS" 2>&1)
echo "$G" | tail -30

echo "$G" | grep -q "Breakpoint 1, compute " || { echo "FAIL: gdb didn't hit compute"; exit 1; }
echo "$G" | grep -q '\$1 = 3'  || { echo "FAIL: compute x != 3";       exit 1; }
echo "$G" | grep -q '\$2 = 4'  || { echo "FAIL: compute y != 4";       exit 1; }
echo "$G" | grep -q '\$3 = 7'  || { echo "FAIL: compute sum != 7";     exit 1; }
echo "$G" | grep -q '\$4 = 12' || { echo "FAIL: compute prod != 12";   exit 1; }
echo "$G" | grep -q '\$5 = 7'  || { echo "FAIL: g_seed != 7";          exit 1; }
echo "$G" | grep -q "compute.*v0.2.55-g-default-stabs.c" || {
    echo "FAIL: bt missing compute file:line"; exit 1; }
echo "$G" | grep -q "main.*v0.2.55-g-default-stabs.c" || {
    echo "FAIL: bt missing main file:line"; exit 1; }

echo
echo "OK: bare -g now emits stabs by default; -gdwarf-2 still emits DWARF."
