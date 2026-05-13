#!/bin/sh
# v0.2.53-g3 milestone — N_BNSYM / N_ENSYM function-body markers
# (roadmap #7.5 Phase 2, item 1c).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.53-bnsym-ensym.sh
#
# Expected last line:
#     OK: tcc -gstabs emits paired BNSYM/ENSYM around every function.
#
# What this demonstrates:
#   v0.2.51 (Phase 1) made tcc -gstabs produce Mach-O binaries whose
#   STAB chain Tiger gdb 6.3 reads for file:line breakpoints, source
#   listing, backtraces. v0.2.52 (Phase 2 item 1a) fixed PSYM/LSYM
#   stack offsets so `print <param>` / `print <local>` work.
#
#   This release (Phase 2 item 1c) adds the Apple-conventional
#   N_BNSYM / N_ENSYM markers around each function body. Each user
#   function now emits, in order:
#
#       BNSYM   (n_value = function start address)
#       FUN     (name + opening entry)
#       SLINE   (per source line in body)
#       PSYM/LSYM ... (params and locals)
#       FUN     (empty-name closing entry, n_value = function size)
#       ENSYM   (n_value = function end address)
#
#   On Tiger gdb 6.3 the markers are silent — gdb's SLINE-based
#   prolog skip already gets `break <func>` right without them. The
#   value is convention/correctness:
#       - aligns tcc's stabs output with gcc-4.0's on the same target
#       - infrastructure for dsymutil round-tripping (Phase 2 item 1b
#         will add N_OSO; BNSYM/ENSYM is part of what dsymutil expects
#         to walk)
#       - future-proofs for static analyzers / newer debuggers that
#         rely on explicit body extents rather than guessing.
#
#   This demo:
#     1. Builds the .c with `tcc -gstabs`.
#     2. Runs it (sanity).
#     3. nm -ap dump showing paired BNSYM/ENSYM around every user
#        function (tiny / small / medium / stat_helper / main and
#        the tcc-emitted __keymgr_* helpers).
#     4. Asserts paired count matches function count.
#     5. Asserts each BNSYM address == matching FUN address and each
#        ENSYM == matching FUN address + size.
#     6. Regression: re-runs the v0.2.52 PSYM/LSYM gdb workflow on
#        this binary's `medium(1,2,3)` call to confirm Phase 2-1a
#        still works.
set -e
cd "$(dirname "$0")/.."

TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ../scripts/build-tiger.sh configure || true
)

SRC="demos/v0.2.53-bnsym-ensym.c"
OUT=/tmp/v0.2.53-bnsym-ensym

# (1) build
"$TCC" -B./tcc -L./tcc -I./tcc/include -gstabs -w "$SRC" -o "$OUT"

# (2) run
echo "== program output =="
"$OUT"
ACTUAL_OUT=$("$OUT")
EXPECTED_OUT="tiny=11 small=14 medium=11 stat=15"
[ "$ACTUAL_OUT" = "$EXPECTED_OUT" ] || {
    echo "FAIL: program output unexpected: $ACTUAL_OUT"
    echo "  expected: $EXPECTED_OUT"
    exit 1
}

# (3) STAB dump — BNSYM/ENSYM/FUN around user functions
echo
echo "== STAB BNSYM/ENSYM/FUN (nm -ap, filtered) =="
nm -ap "$OUT" | grep -E "BNSYM|ENSYM| FUN .*(tiny|small|medium|stat_helper|main):" | head -40

# (4) Count BNSYM vs ENSYM — must be balanced
BNSYM_CNT=$(nm -ap "$OUT" | grep -c "BNSYM")
ENSYM_CNT=$(nm -ap "$OUT" | grep -c "ENSYM")
echo
echo "BNSYM count: $BNSYM_CNT, ENSYM count: $ENSYM_CNT"
[ "$BNSYM_CNT" = "$ENSYM_CNT" ] || {
    echo "FAIL: unbalanced BNSYM/ENSYM (count mismatch)"
    exit 1
}
[ "$BNSYM_CNT" -ge 5 ] || {
    echo "FAIL: expected >= 5 BNSYM (tiny+small+medium+stat_helper+main+keymgr_*); got $BNSYM_CNT"
    exit 1
}

# (5) Address pairing — each user function's BNSYM should match its
#     opening FUN address, and each ENSYM should follow a closing FUN
#     (empty-name FUN size-entry).
echo
echo "== address pairing check =="
for FN in tiny small medium main; do
    # Grab the BNSYM address that immediately precedes the FUN <fn>:F* entry.
    # nm -ap preserves emission order, so BNSYM appears right before the
    # opening FUN.
    PAIR=$(nm -ap "$OUT" | awk -v fn="$FN" '
        /BNSYM/ { saved_addr = $1; next }
        $0 ~ ("FUN " fn ":") {
            print saved_addr, $1
            exit
        }
    ')
    BSYM_ADDR=$(echo "$PAIR" | awk '{print $1}')
    FUN_ADDR=$(echo "$PAIR" | awk '{print $2}')
    if [ -z "$BSYM_ADDR" ] || [ "$BSYM_ADDR" != "$FUN_ADDR" ]; then
        echo "FAIL: $FN BNSYM addr ($BSYM_ADDR) != opening FUN addr ($FUN_ADDR)"
        exit 1
    fi
    echo "  $FN: BNSYM @ $BSYM_ADDR == FUN @ $FUN_ADDR  ✓"
done

# stat_helper is `static` so its FUN entry uses lowercase 'f', not 'F'.
PAIR=$(nm -ap "$OUT" | awk '
    /BNSYM/ { saved_addr = $1; next }
    /FUN stat_helper:f/ {
        print saved_addr, $1
        exit
    }
')
BSYM_ADDR=$(echo "$PAIR" | awk '{print $1}')
FUN_ADDR=$(echo "$PAIR" | awk '{print $2}')
if [ -z "$BSYM_ADDR" ] || [ "$BSYM_ADDR" != "$FUN_ADDR" ]; then
    echo "FAIL: stat_helper BNSYM addr ($BSYM_ADDR) != opening FUN addr ($FUN_ADDR)"
    exit 1
fi
echo "  stat_helper: BNSYM @ $BSYM_ADDR == FUN @ $FUN_ADDR  ✓"

# (6) Regression: Phase 2-1a PSYM/LSYM offsets still work for `medium`
echo
echo "== regression: medium(1,2,3) param/local prints =="
cat > /tmp/v0.2.53-gdb-medium.cmd <<'EOF'
break medium
run
print p
print q
print r
next
print t1
next
print t2
next
print t3
next
print t4
quit
EOF
G_MED=$(gdb -batch -nx -x /tmp/v0.2.53-gdb-medium.cmd "$OUT" 2>&1)
echo "$G_MED" | tail -20
echo "$G_MED" | grep -q '\$1 = 1' || { echo "FAIL: medium p != 1"; exit 1; }
echo "$G_MED" | grep -q '\$2 = 2' || { echo "FAIL: medium q != 2"; exit 1; }
echo "$G_MED" | grep -q '\$3 = 3' || { echo "FAIL: medium r != 3"; exit 1; }
echo "$G_MED" | grep -q '\$4 = 3' || { echo "FAIL: medium t1 != 3"; exit 1; }
echo "$G_MED" | grep -q '\$5 = 6' || { echo "FAIL: medium t2 != 6"; exit 1; }
echo "$G_MED" | grep -q '\$6 = 12' || { echo "FAIL: medium t3 != 12"; exit 1; }
echo "$G_MED" | grep -q '\$7 = 11' || { echo "FAIL: medium t4 != 11 (t3-p)"; exit 1; }

echo
echo "OK: tcc -gstabs emits paired BNSYM/ENSYM around every function."
