#!/usr/bin/env bash
# Demo for v0.2.40-g3 — GNU sed 4.8 builds + works under tcc
#
# Real-world program build #6 (after lua, zlib, bzip2, cJSON,
# sqlite). sed exercises a different mix of codegen than gzip:
# heavy string manipulation, regex compilation (gnulib's regex.c
# with bitfields and same-TU forward-ref-then-tentative-def of
# globals), large dispatch tables, locale handling.
#
# v0.2.40 was the release that made this work. Three substantive
# tcc fixes had to land:
#
#   1. VT_BOOL store/load via pointer (`_Bool *p; *p = b;`).
#      Hit by sed's executor.
#
#   2. `__attribute__((gnu_inline))` recognized; predefine
#      `__GNUC_GNU_INLINE__=1` on Apple. gnulib's c-ctype.h uses
#      `_GL_INLINE` which on gcc-4.x defaults to gnu89-inline
#      semantics ("extern inline" = body-for-inlining-only, with
#      a separate strong def in c-ctype.c). Without the attribute
#      handling, every TU emitting c-ctype.h's inlines tripped
#      "defined twice" link errors.
#
#   3. PIC SECTDIFF reloc translation for forward-ref-then-
#      tentative-def-of-global. When tcc parses regex.c it sees
#      `extern reg_syntax_t re_syntax_options;` from regex.h
#      first (PIC reloc emitted), then the tentative def at line
#      243. The .o writer baked the .o-internal SECTDIFF
#      displacement into addis/addi immediates; the loader was
#      silently dropping the reloc, leaving the wrong constant
#      in the linked exe. Result: re_compile_pattern read 0 for
#      the syntax flag, and ERE `(...)` / BRE `\{n,m\}` patterns
#      didn't compile.
#
# This demo:
#  - Pulls leopard.sh's sed-4.8 binpkg config.cache.
#  - Strips the env-locked entries (ac_cv_env_*, ac_cv_build*,
#    ac_cv_host*, ac_cv_prog_*, ac_cv_path_CC,
#    ac_cv_sys_largefile_CC, ac_cv_func_acl_*).
#  - Pins gnulib's known-bad ac_cv_func values via env (the
#    leopard.sh cache was generated against gcc-4.9 and got
#    several glibc-isms wrong on Tiger; without the pin, gnulib
#    pulls in fseterr.h etc. and the build fails late).
#  - Runs `./configure --disable-dependency-tracking --disable-acl`
#    with CC=tcc.
#  - Builds.
#  - Runs the resulting sed/sed across a battery of regex
#    features (s///, address ranges, BRE+ERE backrefs, BRE
#    intervals, line deletion, append) and verifies output.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0240-sed-build"
PREFIX="$TMP/v0240-sed-install"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

rm -rf "$WORK" "$PREFIX"
mkdir -p "$WORK"
cd "$WORK"

echo "==> Downloading sed 4.8 source..."
[[ ! -f sed-4.8.tar.gz ]] && \
    curl -sL https://leopard.sh/tigersh/dist/sed-4.8.tar.gz -o sed-4.8.tar.gz

echo "==> Downloading leopard.sh sed-4.8 binpkg (just for its config.cache)..."
[[ ! -f sed-4.8-binpkg.tar.gz ]] && \
    curl -sL https://leopard.sh/tigersh/binpkgs/sed-4.8.tiger.g3.tar.gz \
         -o sed-4.8-binpkg.tar.gz
tar xzf sed-4.8-binpkg.tar.gz sed-4.8/share/tiger.sh/sed-4.8/config.cache.gz
gunzip -c sed-4.8/share/tiger.sh/sed-4.8/config.cache.gz > leopard.cache
rm -rf sed-4.8

tar xzf sed-4.8.tar.gz
cd sed-4.8
# Strip the env-locked entries and the gcc-4.9-flavored ACL detections.
grep -v -E "^ac_cv_env_|^ac_cv_build|^ac_cv_host|^ac_cv_prog_CC|^ac_cv_prog_cc_|^ac_cv_prog_ac_ct_CC|^ac_cv_prog_CPP|^ac_cv_prog_ac_ct_CPP|^ac_cv_path_CC|^ac_cv_sys_largefile_CC|^ac_cv_func_acl_" \
    ../leopard.cache > config.cache

echo "==> Configuring with tcc as CC (strict env pin for gnulib)..."
ac_cv_func___freading=no \
ac_cv_func___fseterr=no \
ac_cv_header_stdio_ext_h=no \
ac_cv_func_fdopendir=no \
ac_cv_func_utimensat=no \
ac_cv_func_futimens=no \
ac_cv_func_lutimes=no \
ac_cv_func_futimesat=no \
CC="$TCC_ABS" \
CFLAGS="-B$TCCROOT -L$TCCROOT -I$TCCROOT/include" \
./configure --disable-dependency-tracking --disable-acl -C \
            --prefix="$PREFIX" > configure.log 2>&1 \
    || { echo "FAIL: configure failed; tail of log:"; tail -30 configure.log; exit 1; }

CC_LINE=$(grep "^CC = " Makefile | head -1)
if [[ "$CC_LINE" != "CC = $TCC_ABS"* ]]; then
    echo "FAIL: CC was overridden, Makefile has: $CC_LINE"
    exit 1
fi
echo "  CC=$TCC_ABS confirmed in Makefile"

echo "==> Building sed/sed (only — gnulib-tests don't compile cleanly)..."
make sed/sed > build.log 2>&1 \
    || { echo "FAIL: build failed; tail of log:"; tail -30 build.log; exit 1; }

if [[ ! -x sed/sed ]]; then
    echo "FAIL: sed/sed not produced"
    exit 1
fi

echo "==> Verifying sed/sed is a PPC Mach-O binary..."
if ! file sed/sed | grep -q "Mach-O.*ppc"; then
    echo "FAIL: not a PPC Mach-O binary:"
    file sed/sed
    exit 1
fi

echo "==> Verifying sed/sed links only against libSystem (no libgcc_s)..."
LIBS=$(otool -L sed/sed | tail -n +2)
if echo "$LIBS" | grep -q "libgcc"; then
    echo "FAIL: links against libgcc_s — gcc was somehow involved:"
    echo "$LIBS"
    exit 1
fi
if ! echo "$LIBS" | grep -q "libSystem.B.dylib"; then
    echo "FAIL: doesn't link libSystem:"
    echo "$LIBS"
    exit 1
fi
echo "  links only against /usr/lib/libSystem.B.dylib"

echo "==> Verifying --version reports GNU sed 4.8..."
VER=$(./sed/sed --version | head -1)
if [[ "$VER" != *"(GNU sed) 4.8"* ]]; then
    echo "FAIL: unexpected version: $VER"
    exit 1
fi
echo "  $VER"

echo "==> Functional checks..."

# Test 1: s/.../...g and chained substitution.
RESULT=$(echo "hello tcc world" | ./sed/sed "s/tcc/SED/g; s/world/galaxy/")
if [[ "$RESULT" != "hello SED galaxy" ]]; then
    echo "FAIL: s///g + chain: got '$RESULT'"
    exit 1
fi
echo "  s/.../...g + chained subst works"

# Test 2: -n addr,addr p (address range).
RESULT=$(printf 'a\nb\nc\nd\ne\n' | ./sed/sed -n '2,4p')
EXPECT=$(printf 'b\nc\nd')
if [[ "$RESULT" != "$EXPECT" ]]; then
    echo "FAIL: -n addr,addr p: got '$RESULT'"
    exit 1
fi
echo "  -n addr,addr p works"

# Test 3: -E with backrefs (this was THE failing case before v0.2.40).
RESULT=$(echo "foo123bar456" | ./sed/sed -E "s/([a-z]+)([0-9]+)/\2\1/g")
if [[ "$RESULT" != "123foo456bar" ]]; then
    echo "FAIL: -E backref: got '$RESULT'"
    exit 1
fi
echo "  -E backref works (was broken pre-v0.2.40)"

# Test 4: BRE backref (always worked, but a regression smoketest).
RESULT=$(echo "abc" | ./sed/sed "s/\(.\)\(.\)\(.\)/\3\2\1/")
if [[ "$RESULT" != "cba" ]]; then
    echo "FAIL: BRE backref: got '$RESULT'"
    exit 1
fi
echo "  BRE backref works"

# Test 5: BRE intervals \{n,\} (also broken pre-v0.2.40).
RESULT=$(echo "aaab" | ./sed/sed "s/a\{2,\}/A/")
if [[ "$RESULT" != "Ab" ]]; then
    echo "FAIL: BRE interval: got '$RESULT'"
    exit 1
fi
echo "  BRE interval \\{n,\\} works (was broken pre-v0.2.40)"

# Test 6: Line delete.
RESULT=$(printf '1\n2\n3\n' | ./sed/sed '/2/d')
EXPECT=$(printf '1\n3')
if [[ "$RESULT" != "$EXPECT" ]]; then
    echo "FAIL: /pat/d: got '$RESULT'"
    exit 1
fi
echo "  /pat/d works"

# Test 7: append (multi-line command).
RESULT=$(echo "x" | ./sed/sed 'a\
appended')
EXPECT=$(printf 'x\nappended')
if [[ "$RESULT" != "$EXPECT" ]]; then
    echo "FAIL: a\\: got '$RESULT'"
    exit 1
fi
echo "  a\\ append works"

echo
echo "PASS — GNU sed 4.8 builds + works end-to-end with tcc on Tiger PPC"
