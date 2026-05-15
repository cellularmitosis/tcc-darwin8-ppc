#!/bin/sh
# v0.2.59-g3 milestone — dylib self-link diagnostics
# (roadmap #7, dylib half).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.59-dylib-self-link-diagnostics.sh
#
# Expected last line:
#     ro=ok | data=42 | bss=11 22 33 | got=ok | call_count=2
#
# What this demonstrates:
#   The Mach-O dylib writer (tcc/ppc-macho.c::macho_output_dylib)
#   now runs the same four pre-write sanity checks as
#   macho_output_exe (added v0.2.50). The dylib path adapts each
#   check for its different fixed points:
#     (a) VM layout — base = DYLIB_TEXT_VMADDR_BASE (0x40000000),
#         no __PAGEZERO, no __DWARF segment in the dylib path.
#     (b) `__mh_dylib_header` registered as N_ABS at __TEXT base
#         (the dylib equivalent of `__mh_execute_header`); no
#         entry-point check (dylibs have no LC_UNIXTHREAD).
#     (c) PIC stub addresses inside __symbol_stub1 (32-byte
#         stubs, not the EXE writer's 16-byte absolute stubs);
#         slot addresses inside __nl_symbol_ptr; every
#         ST_PPC_NEEDS_STUB symbol got a stub allocated.
#     (d) Every defined symbol's section is in the dylib writer's
#         emitted section list.
#   The dylib source built below touches every section class
#   (text, rodata, data, bss, libSystem-imported externs, a
#   function-pointer initializer that produces a locrel
#   slide-time fixup) so the dylib writer's happy path runs every
#   check on every emitted section. If any check fires in the
#   future, the build below will fail with a useful tcc-level
#   error instead of producing a dylib that crashes inside dyld
#   or returns NULL from dlopen with no useful context.
set -e
cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.59-dylib-self-link-diagnostics}
mkdir -p "$WORK"

# Dylib source — covers every section class the writer emits.
cat > "$WORK/libdiag.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>

static const char  diag_ro[]  = "rodata works";
static int         diag_data  = 42;
static int         diag_bss[8];
static int         diag_calls = 0;

static void diag_init_fn(int *p) { *p = 99; }

/* Function-pointer-in-data: exercises the locrel slide-time
 * fixup path that (a) layout checks must keep clean. */
static void (*diag_init_fn_slot)(int *) = diag_init_fn;

int diag_greet(const char *who)
{
    diag_calls++;
    /* libSystem-imported extern: triggers PIC stub + nl-ptr slot. */
    printf("hello %s (#%d)\n", who, diag_calls);
    return 0;
}

int           diag_get_count(void)        { return diag_calls; }
const char   *diag_get_ro(void)           { return diag_ro; }
int          *diag_get_data_ptr(void)     { return &diag_data; }
int          *diag_get_bss_ptr(void)      { return diag_bss; }
void        (*diag_get_init_fn_ptr(void))(int *) { return diag_init_fn_slot; }
EOF

# Step 1: build the dylib. If any of the four checks fires,
# tcc will exit non-zero with a `ppc-macho: dylib ...` error.
$TCC -shared -o "$WORK/libdiag.dylib" "$WORK/libdiag.c"

# Step 2: build the dlopen driver.
$TCC -B./tcc -L./tcc -I./tcc/include \
     -o "$WORK/diag_drv" demos/v0.2.59-dylib-self-link-diagnostics.c

# Step 3: run from $WORK so dlopen("./libdiag.dylib") resolves.
cd "$WORK" && ./diag_drv
