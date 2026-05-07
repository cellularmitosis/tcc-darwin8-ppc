#!/bin/sh
# v0.2.31-libtcc-slide.sh — proves a dylib carrying real
# `static const struct {const char *name; ...} tbl[]` tables
# survives dyld sliding.
#
# v0.2.29 added VANILLA local relocs for ADDR32 entries in
# __data / __mod_init_func / __mod_term_func, but tcc still
# routed pointer-bearing const tables to .rodata which lands
# in __TEXT,__const (read-only at runtime). dyld can't write
# those slots during slide processing, so the embedded
# `name` pointers stayed unslid and any subsequent
# dereference crashed under slide.
#
# v0.2.31 routes initialized const data to .data when the
# output is a dylib (TCC_OUTPUT_DLL). The whole table now
# lives in __DATA,__data which IS writable; v0.2.29's
# locrel walker picks up the embedded pointer entries and
# emits a relocation_info for each, so dyld fixes them
# during slide.
#
# Concrete demonstration: build a dylib that exports a
# `lookup(name)` function backed by a const-array of
# {const char *, int} pairs. Force dyld to slide the dylib
# (mmap blocker over 0x40000000) and verify lookup() still
# returns the right values — i.e. that the inner `name`
# pointers got slid correctly.

set -e
cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.31-libtcc-slide}
mkdir -p "$WORK"

cat > "$WORK/lookup_lib.c" <<'EOF'
#include <string.h>

/* This is the pattern we care about: a static const array of
 * structs whose first field is a string-literal pointer. tcc
 * 0.2.30 and earlier routed this to __const (read-only); we now
 * route it to __data so dyld can patch the embedded pointers
 * during slide. */
static const struct {
    const char *name;
    int         value;
} tbl[] = {
    { "alpha",   1 },
    { "beta",    2 },
    { "gamma",   3 },
    { "delta",   4 },
    { "epsilon", 5 },
};

int lookup(const char *name) {
    int i;
    for (i = 0; i < (int)(sizeof(tbl)/sizeof(tbl[0])); i++) {
        if (strcmp(tbl[i].name, name) == 0)
            return tbl[i].value;
    }
    return -1;
}

const char *name_of(int value) {
    int i;
    for (i = 0; i < (int)(sizeof(tbl)/sizeof(tbl[0])); i++) {
        if (tbl[i].value == value)
            return tbl[i].name;
    }
    return "?";
}
EOF

cat > "$WORK/runner.h" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

static int run_checks(const char *label) {
    void *h = dlopen("./lookup_lib.dylib", RTLD_NOW);
    if (!h) { printf("[%s] dlopen: %s\n", label, dlerror()); return 1; }
    int (*lookup)(const char *) = dlsym(h, "lookup");
    const char *(*name_of)(int) = dlsym(h, "name_of");
    int fails = 0;
#define CHECK(call, exp) do { \
    typeof(exp) g = (call); \
    if ((typeof(exp))g != (typeof(exp))(exp)) { \
        printf("  [%s] %-25s -> %s (expected %s) FAIL\n", label, #call, \
               sizeof(g)==sizeof(int) ? (char[16]){0} : (char *)g, \
               sizeof(exp)==sizeof(int) ? (char[16]){0} : (char *)(exp)); \
        fails++; } } while(0)
    if (lookup("alpha")   != 1)        { printf("  [%s] lookup alpha FAIL\n", label); fails++; }
    if (lookup("beta")    != 2)        { printf("  [%s] lookup beta FAIL\n", label); fails++; }
    if (lookup("epsilon") != 5)        { printf("  [%s] lookup epsilon FAIL\n", label); fails++; }
    if (lookup("zoinks")  != -1)       { printf("  [%s] lookup zoinks FAIL\n", label); fails++; }
    if (strcmp(name_of(3), "gamma"))   { printf("  [%s] name_of 3 FAIL got=%s\n", label, name_of(3)); fails++; }
    if (strcmp(name_of(4), "delta"))   { printf("  [%s] name_of 4 FAIL got=%s\n", label, name_of(4)); fails++; }
    if (fails == 0)
        printf("  [%s] all 6 lookups ok\n", label);
    return fails;
}
EOF

cat > "$WORK/dlopen_normally.c" <<'EOF'
#include "runner.h"
int main(void) { return run_checks("no slide") == 0 ? 0 : 1; }
EOF

cat > "$WORK/dlopen_slid.c" <<'EOF'
#include <sys/mman.h>
#include <errno.h>
#include "runner.h"
int main(void) {
    void *blocker = mmap((void *)0x40000000, 0x01000000,
                         PROT_NONE,
                         MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (blocker == MAP_FAILED) {
        fprintf(stderr, "mmap blocker FAILED (errno=%d)\n", errno);
        return 1;
    }
    return run_checks("with slide") == 0 ? 0 : 1;
}
EOF

cd "$WORK"
"$OLDPWD/$TCC" -shared -o lookup_lib.dylib lookup_lib.c
file lookup_lib.dylib
echo "==> locrel count (must be > 0; covers tbl's name pointers):"
otool -lv lookup_lib.dylib | grep "nlocrel"

gcc-4.0 -o dlopen_normally dlopen_normally.c
gcc-4.0 -o dlopen_slid     dlopen_slid.c

rc1=0; rc2=0
echo
echo "==> RUN 1: dlopen at preferred vmaddr"
./dlopen_normally || rc1=1
echo
echo "==> RUN 2: blocker forces dyld to slide"
./dlopen_slid     || rc2=1

if [ $rc1 -eq 0 ] && [ $rc2 -eq 0 ]; then
    echo
    echo "PASS — pointer-bearing const tables survive slide"
    exit 0
else
    echo
    echo "FAIL"
    exit 1
fi
