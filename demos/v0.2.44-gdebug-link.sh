#!/usr/bin/env bash
# Demo for v0.2.44-g3 — `tcc -g` linking separately-compiled .o
# files no longer produces SEGV-ing executables.
#
# Pre-v0.2.44, after the v0.2.43 smap-NULL fix made `tcc -g -c
# foo.c` no longer crash, the next problem appeared at the LINK
# step: `tcc -g -o exe a.o b.o c.o` produced an exe that
# segfaulted at runtime when accessing a global pointer
# initialized to an extern symbol's address. Single-source-
# compile-and-link (`tcc -g all.c -o exe`) worked, so the bug
# was specific to crossing TU boundaries with -g enabled.
#
# Root cause: in macho_translate_relocs, when a scattered
# SECTDIFF reloc's target value `sc_value` was looked up in
# `ctx->sects[]` to find the containing section, the linear
# search took the FIRST section whose [addr, addr+size) range
# matched. Without -g, the only sections with non-zero
# addresses were the real ones (text, data, nl_symbol_ptr,
# stub) — the lookup found the right section. With -g, the
# debug sections (__debug_info, __debug_abbrev, __debug_line,
# etc.) all start at addr=0 and have large sizes; one of them
# would shadow the real __nl_symbol_ptr at addr 0x180, and the
# lookup would return the wrong section index. The downstream
# call to macho_resolve_stub_slot then computed a bogus
# entry_idx (e.g. 96 in this demo's case, when the indirect
# table only has 3 entries), returned -1, and the SECTDIFF
# reloc was silently dropped — leaving the .o-internal
# displacement (sc_value - pair_value, encoded at compile time)
# baked into the linked exe's addis/addi or addis/lwz immediates.
# Result: indirect address load reads from inside __text instead
# of from __nl_symbol_ptr, dereferences garbage, SEGV.
#
# Fix: in the section-search loop, only consider sections that
# can plausibly be SECTDIFF targets — either real merged tcc
# sections (sec_to_tcc[j] != NULL) or stub-like sections
# (S_SYMBOL_STUBS / S_LAZY_SYMBOL_POINTERS /
# S_NON_LAZY_SYMBOL_POINTERS). Debug sections are skipped, so
# their overlapping addr=0 ranges no longer shadow the real
# stub section.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0244-gdebug-link"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

rm -rf "$WORK"
mkdir -p "$WORK"

# Smallest reproducer split across 3 TUs.
cat > "$WORK/data.c" <<'EOF'
extern int v;
int *p = &v;
EOF

cat > "$WORK/data_def.c" <<'EOF'
int v = 42;
EOF

cat > "$WORK/data_main.c" <<'EOF'
extern int *p;
#include <stdio.h>
int main(void) {
    printf("*p = %d\n", *p);
    return *p == 42 ? 0 : 1;
}
EOF

echo "==> Compiling each .c with -g..."
for f in data data_def data_main; do
    "$TCC_ABS" -g -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
               -c "$WORK/$f.c" -o "$WORK/$f.o"
    echo "  $f.o ($(wc -c < "$WORK/$f.o") bytes)"
done

echo "==> Linking with -g..."
"$TCC_ABS" -g -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
           -o "$WORK/exe" "$WORK/data.o" "$WORK/data_def.o" "$WORK/data_main.o"

echo "==> Running exe (pre-v0.2.44 this SEGV'd)..."
OUT=$("$WORK/exe")
if [[ "$OUT" != "*p = 42" ]]; then
    echo "FAIL: exe output: '$OUT'"
    exit 1
fi
echo "  $OUT"

# Bigger test: function-pointer init from extern + indirect call.
cat > "$WORK/fn_def.c" <<'EOF'
int counter = 0;
int bump(int n) { counter += n; return counter; }
EOF

cat > "$WORK/fn_table.c" <<'EOF'
extern int bump(int);
int (*table[])(int) = { bump, bump, 0 };
EOF

cat > "$WORK/fn_main.c" <<'EOF'
extern int (*table[])(int);
extern int counter;
#include <stdio.h>
int main(void) {
    table[0](1);
    table[1](10);
    table[0](100);
    printf("counter=%d\n", counter);
    return counter == 111 ? 0 : 1;
}
EOF

echo "==> Function-pointer indirect-call test (3 TUs, -g)..."
for f in fn_def fn_table fn_main; do
    "$TCC_ABS" -g -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
               -c "$WORK/$f.c" -o "$WORK/$f.o"
done
"$TCC_ABS" -g -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
           -o "$WORK/exe2" "$WORK/fn_def.o" "$WORK/fn_table.o" "$WORK/fn_main.o"

OUT=$("$WORK/exe2")
if [[ "$OUT" != "counter=111" ]]; then
    echo "FAIL: exe2 output: '$OUT'"
    exit 1
fi
echo "  $OUT"

echo
echo "PASS — tcc -g links cross-TU extern refs into working executables"
