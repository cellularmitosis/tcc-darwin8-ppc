#!/usr/bin/env bash
# Demo for v0.2.33-g3 — libtcc callback (SHN_ABS) reachability
#
# Pre-v0.2.33, calls from JIT-compiled code into a callback registered
# via `tcc_add_symbol` failed when the JIT region landed >32MB from
# the libtcc binary (typical on a host process that's loaded a lot
# of code already). The PPC `bl` instruction has a 24-bit signed
# displacement (~32MB), and tcc's build_got_entries skipped synthesizing
# PLT trampolines for absolute (SHN_ABS) symbols on 32-bit platforms.
#
# v0.2.33 adds PPC32 to the list of targets that DO synthesize PLT
# trampolines for SHN_ABS symbols, so a JIT'd `bl callback` is
# rewritten to `bl plt_stub`, where plt_stub is in the same JIT
# region as the call site and does an absolute indirect jump
# (lis/ori/mtctr/bctr) to the real callback address.
#
# This is exactly the scenario that the upstream `libtest` stage
# exercises — and it had been failing silently since at least
# session 045. The session-046 HANDOFF mistakenly listed it as
# passing.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
SRC="$TMP/v0233-cb.c"
OUT="$TMP/v0233-cb"

cat > "$SRC" <<'EOF'
/* Mirrors tcc/tests/libtcc_test.c but as a small standalone demo. */
#include <libtcc.h>
#include <stdio.h>
#include <stdlib.h>

/* Callback the JIT'd code will invoke via `bl _add`. tcc registers
 * its address via tcc_add_symbol; pre-v0.2.33 the resulting REL24
 * out-of-ranged when the JIT region landed far from this binary. */
int add(int a, int b) { return a + b; }

const char hello[] = "hello from JIT'd code";

static const char prog[] =
    "extern int add(int, int);\n"
    "extern const char hello[];\n"
    "int compute(int n) {\n"
    "  printf(\"%s, n=%d\\n\", hello, n);\n"
    "  return add(n, add(n, n));\n"      /* 3*n */
    "}\n";

int main(int argc, char **argv) {
    TCCState *s = tcc_new();
    int i;
    int (*fn)(int);

    if (!s) { fprintf(stderr, "tcc_new failed\n"); return 1; }

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] == 'B')
            tcc_set_lib_path(s, argv[i] + 2);
        else if (argv[i][0] == '-' && argv[i][1] == 'I')
            tcc_add_include_path(s, argv[i] + 2);
        else if (argv[i][0] == '-' && argv[i][1] == 'L')
            tcc_add_library_path(s, argv[i] + 2);
    }

    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s, prog) == -1) return 1;

    tcc_add_symbol(s, "add", add);
    tcc_add_symbol(s, "hello", hello);

    if (tcc_relocate(s) < 0) {
        fprintf(stderr, "tcc_relocate failed\n");
        return 1;
    }

    fn = tcc_get_symbol(s, "compute");
    if (!fn) { fprintf(stderr, "tcc_get_symbol(compute) failed\n"); return 1; }

    int r = fn(7);
    if (r != 21) {
        fprintf(stderr, "FAIL: compute(7) returned %d, expected 21\n", r);
        return 1;
    }
    printf("PASS — JIT call into add() reached real address (got 21)\n");

    tcc_delete(s);
    return 0;
}
EOF

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
echo "==> Building libtcc-callback demo with gcc-4.0..."
gcc-4.0 -I"$TCCROOT" -L"$TCCROOT" "$SRC" "$TCCROOT/libtcc.a" -lm -ldl -lpthread \
    -flat_namespace -undefined warning -o "$OUT"

echo "==> Running..."
"$OUT" -B"$TCCROOT" -I"$TCCROOT/include" -L"$TCCROOT"
