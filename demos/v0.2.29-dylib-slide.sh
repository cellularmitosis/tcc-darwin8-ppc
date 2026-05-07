#!/bin/sh
# v0.2.29-dylib-slide.sh — proves tcc-built dylibs survive dyld
# sliding the image away from its preferred vmaddr (0x40000000).
#
# Pre-v0.2.29, tcc -shared output worked only when dyld loaded the
# dylib at exactly 0x40000000. If anything else occupied that range,
# dyld slid the image; absolute refs in __data and absolute lis/addi
# instruction pairs in __text continued to point at the unslid
# addresses, and the dylib either crashed or returned garbage.
#
# v0.2.29:
#   * code uses PIC sectdiff for local data refs (slide-invariant);
#     PIC base setup pinned to function prolog so it always runs
#     before any subsequent PIC use, regardless of control flow.
#   * macho_output_dylib emits VANILLA local relocs in LC_DYSYMTAB
#     for ADDR32 entries to local syms in __data / __mod_init_func /
#     __mod_term_func.
#
# Two test programs share one dylib:
#   1. dlopen_normally — loads the dylib at preferred vmaddr.
#   2. dlopen_slid     — first reserves 0x40000000-0x41000000 with
#                         mmap(MAP_FIXED), forcing dyld to slide.
# Both must produce identical results across every static-init
# pattern: pointer to local data, pointer-to-pointer, function
# pointer, array of function pointers, __attribute__((constructor)).

set -e
cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.29-dylib-slide}
mkdir -p "$WORK"

# ---- The dylib under test ---------------------------------------
cat > "$WORK/slide_lib.c" <<'EOF'
#include <stdio.h>
#include <string.h>

static int arr[10] = {10,20,30,40,50,60,70,80,90,100};
static int *p_arr = &arr[3];                /* abs ptr in __data */
static int **p_p_arr = &p_arr;              /* ptr-to-ptr in __data */

static int local_fn1(int x) { return x * 2; }
static int local_fn2(int x) { return x + 100; }
static int (*fn_ptr)(int) = &local_fn1;
static int (*fn_array[2])(int) = { &local_fn1, &local_fn2 };

static int ctor_called = 0;
__attribute__((constructor))
static void ctor(void) { ctor_called = 1; }

int test_p_arr(void)         { return *p_arr; }
int test_p_p_arr(void)       { return **p_p_arr; }
int test_fn_ptr(int x)       { return fn_ptr(x); }
int test_fn_array_0(int x)   { return fn_array[0](x); }
int test_fn_array_1(int x)   { return fn_array[1](x); }
int test_ctor_called(void)   { return ctor_called; }
int test_arr_at(int i)       { return arr[i]; }
int *get_arr_addr(void)      { return arr; }
int *get_p_arr(void)         { return p_arr; }
EOF

# ---- The shared body of both host test programs -----------------
cat > "$WORK/runner.h" <<'EOF'
#include <stdio.h>
#include <dlfcn.h>

static int run_checks(const char *label) {
    void *h = dlopen("./slide_lib.dylib", RTLD_NOW);
    if (!h) { printf("[%s] dlopen: %s\n", label, dlerror()); return 1; }
    int (*test_p_arr)(void)       = dlsym(h, "test_p_arr");
    int (*test_p_p_arr)(void)     = dlsym(h, "test_p_p_arr");
    int (*test_fn_ptr)(int)       = dlsym(h, "test_fn_ptr");
    int (*test_fn_array_0)(int)   = dlsym(h, "test_fn_array_0");
    int (*test_fn_array_1)(int)   = dlsym(h, "test_fn_array_1");
    int (*test_ctor_called)(void) = dlsym(h, "test_ctor_called");
    int (*test_arr_at)(int)       = dlsym(h, "test_arr_at");
    int *(*get_arr_addr)(void)    = dlsym(h, "get_arr_addr");
    int *(*get_p_arr)(void)       = dlsym(h, "get_p_arr");
    int *arr_va    = get_arr_addr();
    int *p_arr_val = get_p_arr();
    int fails      = 0;
#define EXPECT(name, got, expected) do { \
    int g = (got), e = (int)(expected); \
    if (g != e) { printf("  [%s] %-22s = %d (expected %d) FAIL\n", \
                          label, name, g, e); fails++; } \
    else        { printf("  [%s] %-22s = %d ok\n", label, name, g); } } while (0)
    printf("[%s] arr @ %p, p_arr -> %p (expected %p)\n",
           label, arr_va, p_arr_val, arr_va + 3);
    EXPECT("p_arr - arr",      p_arr_val - arr_va, 3);
    EXPECT("test_p_arr",       test_p_arr(),       40);
    EXPECT("test_p_p_arr",     test_p_p_arr(),     40);
    EXPECT("test_fn_ptr(7)",   test_fn_ptr(7),     14);
    EXPECT("test_fn_array_0",  test_fn_array_0(7), 14);
    EXPECT("test_fn_array_1",  test_fn_array_1(7), 107);
    EXPECT("test_ctor_called", test_ctor_called(), 1);
    EXPECT("test_arr_at(0)",   test_arr_at(0),     10);
    EXPECT("test_arr_at(9)",   test_arr_at(9),     100);
    return fails;
}
EOF

# ---- 1) plain dlopen ---------------------------------------------
cat > "$WORK/dlopen_normally.c" <<'EOF'
#include "runner.h"
int main(void) { return run_checks("no slide") == 0 ? 0 : 1; }
EOF

# ---- 2) blocker first, then dlopen ------------------------------
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
"$OLDPWD/$TCC" -shared -o slide_lib.dylib slide_lib.c
file slide_lib.dylib
echo "==> locrel summary:"
otool -lv slide_lib.dylib | grep -E "nlocrel|locreloff"
echo "==> locrel entries:"
otool -rv slide_lib.dylib

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
    echo "PASS — slide-invariant dylib output works"
    exit 0
else
    echo
    echo "FAIL"
    exit 1
fi
