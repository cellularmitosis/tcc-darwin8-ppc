#!/usr/bin/env bash
# Demo for v0.2.63-g3 — extern *data* static-init references
# now resolve to the dyld-bound target address.
#
# This closes the long-deferred correctness gap from session 044:
# pre-v0.2.63, `int *p = &optind;` produced `p =
# &__nl_symbol_ptr[optind]` (the indirect pointer slot's
# address) instead of `p = &optind` (the libSystem int's
# address). The exe writer now emits a classic Mach-O external
# relocation (struct relocation_info, type=VANILLA, extern=1)
# in __LINKEDIT for each R_PPC_ADDR32 reloc against an undef
# *data* symbol, and dyld's doBindExternalRelocations fills the
# slot at load time — exactly what gcc-4.0 does.
#
# Two test cases:
#   1. `int *p = &optind`           — zero addend, the classic
#                                      case from session 044's
#                                      caveat list.
#   2. `int *q = &optind + 3`       — nonzero in-place addend.
#                                      Tiger's dyld treats
#                                      VANILLA externs as ADD,
#                                      not OVERWRITE, so the
#                                      addend survives.
#
# Pre-v0.2.63: both fail. Post-v0.2.63: both pass (matches gcc).

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

DEMOROOT="$(cd "$(dirname "$0")" && pwd)"
TCCROOT="$(cd "$(dirname "$TCC")" && pwd)"
SRC="$DEMOROOT/v0.2.63-extern-data-ref.c"
EXE="$TMP/v0263-extern-data-ref"

echo "==> Compiling demo..."
"$TCC" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" -o "$EXE" "$SRC"

echo "==> Verifying one external relocation was emitted (expect 2 entries: p and q)..."
NEXTREL=$(otool -l "$EXE" 2>/dev/null | awk '/nextrel/ {print $2; exit}')
if [[ "$NEXTREL" != "2" ]]; then
    echo "FAIL: expected 2 external relocations, got: $NEXTREL"
    exit 1
fi
echo "  nextrel = $NEXTREL (as expected)"

echo "==> Running demo..."
"$EXE"

echo
echo "PASS — extern *data* static-init refs resolve correctly via dyld bind"
