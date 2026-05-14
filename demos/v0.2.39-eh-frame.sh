#!/usr/bin/env bash
# Demo for v0.2.39-g3 — DWARF __eh_frame + per-prolog CFI in linked exe
#
# Pre-v0.2.39, `tcc -g -o exe` emitted .debug_info / .debug_line /
# .debug_aranges / .debug_str into the __DWARF segment but NO
# .eh_frame, because:
#   1. tcc.h's TCC_EH_FRAME guard excluded ELF_OBJ_ONLY targets
#      (= Mach-O & PE), so eh_frame_section was never created.
#   2. Even if created, classify_section didn't accept .eh_frame —
#      it would have been silently dropped.
#   3. macho_output_exe didn't know to lay out a __TEXT,__eh_frame
#      slot.
#
# v0.2.39 closes the gap:
#   * tcc.h enables TCC_EH_FRAME for TCC_TARGET_MACHO. The runtime
#     ELF-only registration paths (.eh_frame_hdr, dl_iterate_phdr)
#     are unaffected because they're gated on dynamic-link output.
#   * tccelf.c keeps `unwind_tables` on for non-ELF outputs when
#     `-g` is in use, so eh_frame_section actually gets created.
#   * classify_section maps .eh_frame -> __TEXT,__eh_frame
#     (S_REGULAR; the conventional Mach-O slot dwarfdump --eh-frame
#     dispatches on).
#   * macho_output_exe gains an eh_frame discovery + layout slot
#     in __TEXT after __symbol_stub, plus the standard
#     allocate / resolve / emit / write pattern.
#
# And: per-prolog CFI. Pre-v0.2.39, the FDE for each function had
# only the CIE initial state (CFA = r1+0), which is wrong post-stwu
# and would break stack unwinding from inside any function body.
# v0.2.39's tccdbg.c PPC FDE block now emits:
#
#   DW_CFA_advance_loc 96         ; past the prolog's stwu
#   DW_CFA_def_cfa_offset N       ; CFA = r1 + frame_size
#   DW_CFA_offset_extended 65, M  ; LR (column 65) at CFA + (8-N)
#
# where N = frame_size and M = (N - 8) / 4 (in DAF=-4 units).
# ppc-gen.c's gfunc_epilog stashes frame_size in
# `ppc_last_frame_size` for tccdbg.c to read.
#
# Also bundled: default DWARF version is now 2 on PPC Darwin (was
# 4). Tiger's bundled dwarfdump fully understands DWARF-2 but only
# partially DWARF-4 (Unknown DW_FORM_sec_offset = 0x17). Bare `-g`
# now produces tooling-compatible output without needing
# `-gdwarf-2`.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0239-cfi.c"
EXE="$TMP/v0239-cfi-exe"

cat > "$SRC" <<'EOF'
#include <stdio.h>

int sq(int n) {
    return n * n;
}

int sum_to(int n) {
    int total = 0;
    int i;
    for (i = 1; i <= n; i++)
        total += i;
    return total;
}

int main(void) {
    int x = sq(5);              /* 25 */
    int y = sum_to(x);          /* 1+2+...+25 = 325 */
    printf("sq(5)=%d, sum_to(25)=%d\n", x, y);
    return 0;
}
EOF

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)

echo "==> Linking with -gdwarf (default DWARF version)..."
# Pre-v0.2.55 bare `-g` resolved to DWARF; v0.2.55 flipped the default
# to stabs so Tiger gdb just works.  This demo predates that flip and
# exists to verify the DWARF emission path (eh_frame + CFI + version);
# spell -gdwarf explicitly so it tests what it intends regardless of the
# bare-`-g` default.
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" -gdwarf \
    "$SRC" -o "$EXE"

echo "==> Verifying __TEXT,__eh_frame is present..."
if ! otool -l "$EXE" | grep -q "sectname __eh_frame"; then
    echo "FAIL: no __eh_frame section in linked exe"
    otool -l "$EXE" | grep sectname | head
    exit 1
fi
echo "  __eh_frame section present"

echo "==> Verifying CFI directives appear in dwarfdump --eh-frame..."
EHFILE="$TMP/v0239-ehdump.txt"
dwarfdump --eh-frame "$EXE" > "$EHFILE" 2>&1
if ! grep -q "DW_CFA_def_cfa_offset" "$EHFILE"; then
    echo "FAIL: no DW_CFA_def_cfa_offset in eh_frame"
    head -30 "$EHFILE"
    exit 1
fi
if ! grep -q "DW_CFA_offset_extended" "$EHFILE"; then
    echo "FAIL: no DW_CFA_offset_extended (LR save) in eh_frame"
    exit 1
fi
echo "  per-prolog CFI directives present"

echo "==> Verifying CIE has correct PPC return-address column (65 = LR)..."
if ! grep "ra_register" "$EHFILE" | head -1 | grep -q "0x41"; then
    echo "FAIL: CIE ra_register != 0x41 (= 65 = LR)"
    grep "ra_register" "$EHFILE" | head -1
    exit 1
fi
echo "  CIE ra_register = 0x41 (LR) ok"

echo "==> Verifying CIE alignment factors (CAF=4, DAF=-4)..."
if ! grep -q "code_align: 4" "$EHFILE"; then
    echo "FAIL: CAF != 4"
    exit 1
fi
if ! grep -q "data_align: -4" "$EHFILE"; then
    echo "FAIL: DAF != -4"
    exit 1
fi
echo "  CAF=4 / DAF=-4 ok"

echo "==> Counting FDEs (one per function: sq, sum_to, main, plus tcc-injected)..."
FDE_COUNT=$(grep -c "^0x........: FDE" "$EHFILE" || true)
if [[ "$FDE_COUNT" -lt 3 ]]; then
    echo "FAIL: only $FDE_COUNT FDEs found, expected at least 3"
    exit 1
fi
echo "  found $FDE_COUNT FDEs"

echo "==> Verifying default DWARF version is 2 (was 4 pre-v0.2.39)..."
DWFILE="$TMP/v0239-dwdump.txt"
dwarfdump "$EXE" > "$DWFILE" 2>&1
if ! grep "version =" "$DWFILE" | head -1 | grep -q "version = 0x0002"; then
    echo "FAIL: default DWARF version not 2"
    grep "version =" "$DWFILE" | head -1
    exit 1
fi
echo "  default DWARF version = 2 ok"

echo "==> Verifying exe still runs..."
RUN=$("$EXE")
if [[ "$RUN" != "sq(5)=25, sum_to(25)=325" ]]; then
    echo "FAIL: exe output unexpected: $RUN"
    exit 1
fi
echo "  exe runs and prints '$RUN'"

echo
echo "==> Sample of dwarfdump --eh-frame output:"
sed -n '5,28p' "$EHFILE"

echo
echo "PASS — tcc -g emits __eh_frame + per-prolog CFI"
