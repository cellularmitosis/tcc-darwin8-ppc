#!/usr/bin/env bash
# Demo for v0.2.67-g3 — link-time validation of undefined external
# symbols against linked dylibs' export tables (roadmap #12).
#
# Pre-v0.2.67: tcc's PPC writer emitted any SHN_UNDEF symbol into the
# Mach-O nlist regardless of whether it actually resolved at runtime.
# Effects:
#
#   (a) A binary calling a non-existent function (e.g.
#       `extern void foo(); int main(){ foo(); }` with no foo()
#       anywhere) linked cleanly. dyld SIGABRT'd at startup with
#       "Symbol not found: _foo".
#
#   (b) autoconf's AC_LINK_IFELSE / AC_CHECK_FUNCS conftests work by
#       linking a 2-line probe against each candidate symbol and
#       checking the link's exit code. Every probe reported success,
#       so HAVE_X was unconditionally defined for every X — even
#       things that DON'T exist in Tiger libSystem (arc4random_buf,
#       getrandom, ...). Programs built with that false-positive
#       config then failed at runtime.
#
# Post-v0.2.67:
#
#   * `tcc_add_macos_sdkpath` auto-loads /usr/lib/libSystem.B.dylib
#     (and its LC_SUB_LIBRARY sub-libraries — Tiger libSystem's
#     transitive libmathCommon.A.dylib for log2/sqrtf/exp2/...)
#     into s1->dynsymtab_section.
#   * Before laying out an EXE, ppc-macho.c walks s1->symtab for
#     SHN_UNDEF + STB_GLOBAL + non-weak symbols and validates each
#     against dynsymtab. Unresolved syms produce
#     `tcc: error: undefined symbol 'X'` at link time.
#
# Three sub-tests below cover the surface:
#
#   1. Bogus function -> tcc rejects with "undefined symbol" (expected
#      to fail; we check exit code is nonzero and stderr names the
#      bogus symbol).
#   2. Real libSystem call (printf via stdio.h) -> tcc accepts and the
#      exe runs cleanly.
#   3. Math call routed through libSystem's sub-library (log2 lives in
#      libmathCommon.A.dylib, not libSystem.B.dylib's own nlist) ->
#      also accepts, exercises the sub-library recursion path.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT="$(cd "$(dirname "$TCC")" && pwd)"
TCC_ABS="$TCCROOT/$(basename "$TCC")"

# ---- Sub-test 1: bogus symbol must be rejected at link time. ----
echo "==> Test 1: link of a binary calling a non-existent function"
echo "    (pre-v0.2.67: linked clean, dyld SIGABRT at run time)"
cat > "$TMP/v0267-bogus.c" <<'EOF'
extern void definitely_not_a_real_function_xyzzy(void);
int main(void) { definitely_not_a_real_function_xyzzy(); return 0; }
EOF

if "$TCC_ABS" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
        -o "$TMP/v0267-bogus" "$TMP/v0267-bogus.c" 2>"$TMP/v0267-bogus.err"; then
    echo "FAIL: tcc accepted the bogus link instead of rejecting it"
    cat "$TMP/v0267-bogus.err"
    exit 1
fi
if ! grep -q "undefined symbol '_definitely_not_a_real_function_xyzzy'" \
            "$TMP/v0267-bogus.err"; then
    echo "FAIL: tcc rejected the link but the error didn't name the bogus symbol"
    cat "$TMP/v0267-bogus.err"
    exit 1
fi
echo "  PASS: rejected with 'undefined symbol ...' at link time"

# ---- Sub-test 2: real libSystem call (printf) still links and runs. ----
echo "==> Test 2: real libSystem call (printf) accepted, runs cleanly"
cat > "$TMP/v0267-printf.c" <<'EOF'
#include <stdio.h>
int main(void) { printf("hello from libSystem\n"); return 0; }
EOF
"$TCC_ABS" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
    -o "$TMP/v0267-printf" "$TMP/v0267-printf.c"
out=$("$TMP/v0267-printf")
if [[ "$out" != "hello from libSystem" ]]; then
    echo "FAIL: unexpected output: $out"
    exit 1
fi
echo "  PASS: printf-using exe links and runs"

# ---- Sub-test 3: sub-library symbol (log2 is in libmathCommon, not
#       libSystem's own nlist). Validates the LC_LOAD_DYLIB recursion. ----
echo "==> Test 3: sub-library call (log2 via libmathCommon) accepted, runs"
cat > "$TMP/v0267-log2.c" <<'EOF'
#include <stdio.h>
#include <math.h>
int main(void) {
    double x = log2(8.0);
    printf("log2(8.0) = %.1f\n", x);
    return 0;
}
EOF
"$TCC_ABS" -B"$TCCROOT" -L"$TCCROOT" -I"$TCCROOT/include" \
    -o "$TMP/v0267-log2" "$TMP/v0267-log2.c"
out=$("$TMP/v0267-log2")
if [[ "$out" != "log2(8.0) = 3.0" ]]; then
    echo "FAIL: unexpected output: $out"
    exit 1
fi
echo "  PASS: log2() exe (sub-library lookup) links and runs"

# ---- Sub-test 4: verify the binaries link only against libSystem.
#       sub-libraries must NOT appear as direct LC_LOAD_DYLIB entries. ----
echo "==> Test 4: log2 exe should link only libSystem (not libmathCommon)"
deps=$(otool -L "$TMP/v0267-log2" | tail -n +2 | awk '{print $1}')
expected="/usr/lib/libSystem.B.dylib"
if [[ "$deps" != "$expected" ]]; then
    echo "FAIL: expected single dep on libSystem; got:"
    echo "$deps"
    exit 1
fi
echo "  PASS: links only $expected"

echo
echo "PASS — undefined-extern validation rejects bogus symbols, accepts real"
echo "       libSystem and sub-library symbols, and the umbrella LC_LOAD_DYLIB"
echo "       layout (libSystem only, sub-libs invisible) is preserved."
