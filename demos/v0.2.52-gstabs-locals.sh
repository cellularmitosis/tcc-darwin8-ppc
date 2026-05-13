#!/bin/sh
# v0.2.52-g3 milestone — N_PSYM / N_LSYM stack offsets for
# gdb-on-Tiger (roadmap #7.5 Phase 2, item 1a).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.52-gstabs-locals.sh
#
# Expected last line:
#     OK: tcc -gstabs PSYM/LSYM offsets read correctly under gdb.
#
# What this demonstrates:
#   v0.2.51 made `tcc -gstabs` produce Mach-O binaries whose
#   STAB chain Tiger gdb 6.3 reads for file:line breakpoints,
#   source listing, and `print <global>`. But the N_PSYM / N_LSYM
#   stack offsets emitted by tcc were r31 (FP) relative, while
#   Tiger gdb interprets them as r1 (post-prolog SP) relative —
#   so `print <param>` and `print <local>` returned garbage from
#   inside the function's outgoing parameter / linkage area.
#
#   v0.2.52 closes the gap with a one-spot fix in tccdbg.c's
#   tcc_debug_finish: on Mach-O PPC, add the function's frame_size
#   (already stashed by gfunc_epilog in ppc_last_frame_size) to
#   every N_PSYM / N_LSYM value at emission time. r1 + frame_size
#   = r31, so the stored offset now resolves to the right stack
#   slot under gdb's SP-relative interpretation. Type-def LSYMs
#   (struct fields / typedefs, value==0) are exempted.
#
#   This demo:
#     1. Builds the .c with `tcc -gstabs`.
#     2. Runs it (sanity — expected "r1=60 r2=310 r3=-83").
#     3. nm -ap dump showing each function's PSYM/LSYM offsets
#        are now large positive numbers (frame_size + r31-offset)
#        instead of the small 24/28/-12-style values pre-v0.2.52.
#     4. gdb scripts:
#        (a) `break add3`, `run`, `print a/b/c`, step past the
#            local assignments, `print s1/s2`. Asserts each value.
#        (b) `break with_local_and_global`, `print x`, step, check
#            `doubled / with_g / with_s` mid-function.
#        (c) `break many_locals`, walk through five locals,
#            check intermediate values match hand-computed math.
#        (d) `bt` inside add3 shows `main (argc=..., argv=...)`
#            stack frame with real argv pointer, not garbage.
set -e
cd "$(dirname "$0")/.."

TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ../scripts/build-tiger.sh configure || true
)

SRC="demos/v0.2.52-gstabs-locals.c"
OUT=/tmp/v0.2.52-gstabs-locals

# (1) build
"$TCC" -B./tcc -L./tcc -I./tcc/include -gstabs -w "$SRC" -o "$OUT"

# (2) run
echo "== program output =="
"$OUT"
ACTUAL_OUT=$("$OUT")
[ "$ACTUAL_OUT" = "r1=60 r2=310 r3=-83" ] || {
    echo "FAIL: program output unexpected: $ACTUAL_OUT"
    exit 1
}

# (3) STAB offset dump — params and locals
echo
echo "== STAB stack offsets (nm -ap, filtered) =="
nm -ap "$OUT" | grep -E "PSYM|LSYM.*[a-z_][a-z_]*:[0-9]" | head -25

# (4) gdb workflow: print params and locals across three functions,
#     verifying both the entry values and the stepped post-assignment
#     values.
echo
echo "== gdb: add3(10, 20, 30) =="
cat > /tmp/v0.2.52-gdb-add3.cmd <<'EOF'
break add3
run
print a
print b
print c
next
print s1
next
print s2
quit
EOF
G_ADD3=$(gdb -batch -nx -x /tmp/v0.2.52-gdb-add3.cmd "$OUT" 2>&1)
echo "$G_ADD3" | tail -15
echo "$G_ADD3" | grep -q '\$1 = 10' || { echo "FAIL: add3 a != 10"; exit 1; }
echo "$G_ADD3" | grep -q '\$2 = 20' || { echo "FAIL: add3 b != 20"; exit 1; }
echo "$G_ADD3" | grep -q '\$3 = 30' || { echo "FAIL: add3 c != 30"; exit 1; }
echo "$G_ADD3" | grep -q '\$4 = 30' || { echo "FAIL: add3 s1 != 30 (a+b)"; exit 1; }
echo "$G_ADD3" | grep -q '\$5 = 60' || { echo "FAIL: add3 s2 != 60 (s1+c)"; exit 1; }

echo
echo "== gdb: with_local_and_global(5) =="
cat > /tmp/v0.2.52-gdb-wlg.cmd <<'EOF'
break with_local_and_global
run
print x
next
print doubled
next
print with_g
next
print with_s
quit
EOF
G_WLG=$(gdb -batch -nx -x /tmp/v0.2.52-gdb-wlg.cmd "$OUT" 2>&1)
echo "$G_WLG" | tail -15
echo "$G_WLG" | grep -q '\$1 = 5'   || { echo "FAIL: wlg x != 5"; exit 1; }
echo "$G_WLG" | grep -q '\$2 = 10'  || { echo "FAIL: wlg doubled != 10"; exit 1; }
echo "$G_WLG" | grep -q '\$3 = 210' || { echo "FAIL: wlg with_g != 210 (10+200)"; exit 1; }
echo "$G_WLG" | grep -q '\$4 = 310' || { echo "FAIL: wlg with_s != 310 (210+100)"; exit 1; }

echo
echo "== gdb: many_locals(7, 11) =="
cat > /tmp/v0.2.52-gdb-many.cmd <<'EOF'
break many_locals
run
print p
print q
next
print a
next
print b
next
print c
next
print d
next
print e
quit
EOF
G_MANY=$(gdb -batch -nx -x /tmp/v0.2.52-gdb-many.cmd "$OUT" 2>&1)
echo "$G_MANY" | tail -20
echo "$G_MANY" | grep -q '\$1 = 7'   || { echo "FAIL: many p != 7"; exit 1; }
echo "$G_MANY" | grep -q '\$2 = 11'  || { echo "FAIL: many q != 11"; exit 1; }
echo "$G_MANY" | grep -q '\$3 = 8'   || { echo "FAIL: many a != 8 (p+1)"; exit 1; }
echo "$G_MANY" | grep -q '\$4 = 13'  || { echo "FAIL: many b != 13 (q+2)"; exit 1; }
echo "$G_MANY" | grep -q '\$5 = 21'  || { echo "FAIL: many c != 21 (a+b)"; exit 1; }
echo "$G_MANY" | grep -q '\$6 = 104' || { echo "FAIL: many d != 104 (a*b)"; exit 1; }
echo "$G_MANY" | grep -q '\$7 = -83' || { echo "FAIL: many e != -83 (c-d)"; exit 1; }

echo
echo "== gdb: backtrace frames show argument values =="
cat > /tmp/v0.2.52-gdb-bt.cmd <<'EOF'
break add3
run
bt
quit
EOF
G_BT=$(gdb -batch -nx -x /tmp/v0.2.52-gdb-bt.cmd "$OUT" 2>&1)
echo "$G_BT" | tail -10
echo "$G_BT" | grep -qE "add3 \(a=10, b=20, c=30\)" \
    || { echo "FAIL: bt #0 doesn't show add3 (a=10, b=20, c=30)"; exit 1; }
echo "$G_BT" | grep -qE "in main \(argc=[0-9]+, argv=0x[0-9a-f]+\)" \
    || { echo "FAIL: bt #1 doesn't show main(argc=..., argv=0x...)"; exit 1; }

echo
echo "OK: tcc -gstabs PSYM/LSYM offsets read correctly under gdb."
