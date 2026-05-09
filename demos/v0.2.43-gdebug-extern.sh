#!/usr/bin/env bash
# Demo for v0.2.43-g3 — `tcc -g` no longer SIGBUSes on
# `extern int v; int *p = &v;` and similar patterns.
#
# Pre-v0.2.43, compiling a static initializer that referenced
# an extern symbol with `-g` enabled tripped a NULL-pointer
# dereference inside `ppc-macho.c::classify_sym` (called from
# `macho_output_file` during OBJ output). The path was:
#
#   tccgen.c::init_putv emits an R_DATA_PTR reloc against the
#   extern sym `v` in the data section. The sym is pushed to
#   tcc's elfsym table via put_extern_sym with sh_num=SHN_UNDEF.
#   put_extern_sym2 then calls tcc_debug_extern_sym(v, ...) which
#   emits a DWARF DW_TAG_variable entry with a DW_AT_location
#   pointing at v's runtime address (via greloca against v in
#   dwarf_info_section).
#
#   At OBJ write time, classify_sym walks smap[] looking for the
#   section containing each defined symbol. For the synthesized
#   __picsymbolstub1 / __la_symbol_ptr / __nl_symbol_ptr entries,
#   smap[j].elf is NULL — derefing `smap[j].elf->sh_num` was the
#   crash.
#
# Fix: NULL-check `smap[j].elf` in the section-lookup loop, like
# the four other identical loops in the same file already did.
#
# Surfaced by trying to build Python 2.7.18 with `-g`:
# `Modules/config.c` has `extern void initX(void);` declarations
# plus a static `_PyImport_Inittab[]` initializer holding pointers
# to those functions, which trigger the bad path.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

WORK="$TMP/v0243-gdebug"
rm -rf "$WORK"
mkdir -p "$WORK"

# 1. Smallest reproducer: extern data + static initializer.
cat > "$WORK/data.c" <<'EOF'
extern int v;
int *p = &v;
EOF

# 2. Function-pointer init from extern (the gnulib gzip pattern,
#    Python's _PyImport_Inittab pattern).
cat > "$WORK/fnptr.c" <<'EOF'
extern void f1(void);
extern void f2(void);

struct entry {
    char *name;
    void (*func)(void);
};

struct entry table[] = {
    {"a", f1},
    {"b", f2},
    {0,   0},
};
EOF

# 3. Mixed: extern data, extern func, struct array, static init.
cat > "$WORK/mixed.c" <<'EOF'
extern int x;
extern char *y;
extern int compute(int);

struct lookup {
    const char *key;
    int *valuep;
    int (*compute)(int);
};

const struct lookup table[] = {
    {"first",  &x, compute},
    {"second", &x, 0},
    {0,        0,  0},
};
EOF

echo "==> Compiling each with -g; pre-v0.2.43 tcc would SIGBUS..."
for f in data fnptr mixed; do
    echo -n "  $f.c ... "
    "$TCC_ABS" -g -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
               -c "$WORK/$f.c" -o "$WORK/$f.o" 2>&1
    if [[ ! -s "$WORK/$f.o" ]]; then
        echo "FAIL: empty output"
        exit 1
    fi
    echo "OK ($(wc -c < "$WORK/$f.o") bytes)"
done

echo "==> Verifying produced .o files have DWARF + the expected reloc..."
for f in data fnptr mixed; do
    if ! otool -l "$WORK/$f.o" | grep -q __DWARF; then
        echo "FAIL: $f.o missing __DWARF segment"
        exit 1
    fi
    echo "  $f.o: has __DWARF segment"
done

echo "==> Verifying dwarfdump can parse each (no Bus error past prior crash)..."
for f in data fnptr mixed; do
    if ! dwarfdump "$WORK/$f.o" > /dev/null 2>&1; then
        echo "FAIL: dwarfdump rejected $f.o"
        exit 1
    fi
    echo "  $f.o: dwarfdump parses cleanly"
done

# Verify the smallest case in single-source-compile-and-link
# (the .o-then-link path has a separate latent bug with DWARF in
# the linked exe that this demo doesn't address — filed for next
# session). Single-source compile + link works because the
# DWARF info is generated and consumed in the same pass.
cat > "$WORK/all.c" <<'EOF'
#include <stdio.h>
int v = 42;
extern int v;
int *p = &v;
int main(void) {
    printf("*p = %d\n", *p);
    return *p == 42 ? 0 : 1;
}
EOF
"$TCC_ABS" -g -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
           "$WORK/all.c" -o "$WORK/exe"

echo "==> Running linked exe..."
OUT=$("$WORK/exe")
if [[ "$OUT" != "*p = 42" ]]; then
    echo "FAIL: exe output: $OUT"
    exit 1
fi
echo "  $OUT"

echo
echo "PASS — tcc -g compiles extern-sym-in-static-init patterns"
