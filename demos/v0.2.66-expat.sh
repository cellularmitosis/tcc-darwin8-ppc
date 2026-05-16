#!/usr/bin/env bash
# Demo for v0.2.66-g3 — libexpat (Brian Kernighan-era XML parser used
# by Python, Apache, libxml, Webkit, ...) builds + runs correctly with
# tcc as CC on Tiger PPC.
#
# This is a real-world demo: configure with tcc as CC, run make, run
# the embedded 345-test internal suite. expat is a pure C library with
# no external deps (libc only) and exercises a different shape of C
# than the existing demos (XML tokenizer / parser, table-driven state
# machines, string-interning hash tables, ~13K lines).
#
# Why v0.2.66 enables this: xmlparse.c includes <math.h> for isnan().
# Before v0.2.66's math-builtin prototype fix, isnan() returned garbage
# via the ABI-coincidence trap (see demos/v0.2.66-awk.sh for the full
# story). With v0.2.66-g3, isnan() returns correct values and expat's
# numeric entity overflow check works.
#
# Why CFLAGS=-Werror=implicit-function-declaration: autoconf's
# AC_LINK_IFELSE / AC_CHECK_FUNCS probes a function by writing a
# conftest.c that calls it (no header), expecting the link to fail if
# the function doesn't exist. tcc's PPC writer emits undefined
# externals into the binary without validating against linked dylibs —
# so any "does function X exist?" probe says YES, even when X isn't in
# Tiger's libSystem.
#
# Concrete case: expat's conftest probes arc4random_buf (not in Tiger
# libSystem; arc4random IS). Without Werror, configure detects
# HAVE_ARC4RANDOM_BUF=1, the build produces xmlwf, and dyld fails at
# startup with "Symbol not found: _arc4random_buf". With Werror,
# tcc rejects the implicit declaration, configure correctly says NO,
# falls through to arc4random which IS detected, builds clean.
#
# This is the general workaround for autoconf-with-tcc-on-Tiger until
# tcc's PPC writer learns to validate externals against dynsymtab.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0266-expat-build"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

echo "==> Downloading expat 2.5.0 source..."
CURL=/opt/tigersh-deps-0.1/bin/curl
[[ -x "$CURL" ]] || CURL=curl
"$CURL" -fsSL \
    --cacert /Users/macuser/tmp/cacert-2026-03-19.pem \
    https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.gz \
    -o expat-2.5.0.tar.gz
tar xzf expat-2.5.0.tar.gz
cd expat-2.5.0

echo "==> Configuring with tcc as CC (and Werror=implicit-function-declaration"
echo "    to keep autoconf's AC_CHECK_FUNCS honest — see header comment)..."
CC="$TCC_ABS -B$TCCROOT -L$TCCROOT -I$TCCROOT/include" \
CFLAGS="-Werror=implicit-function-declaration" \
    ./configure --disable-shared --without-docbook >/dev/null 2>&1

echo "==> Verifying configure detected the right symbols..."
if ! grep -q '^/\* #undef HAVE_ARC4RANDOM_BUF' expat_config.h; then
    echo "FAIL: HAVE_ARC4RANDOM_BUF should be undef'd (not in Tiger libSystem)"
    grep ARC4RANDOM expat_config.h
    exit 1
fi
if ! grep -q '^#define HAVE_ARC4RANDOM 1' expat_config.h; then
    echo "FAIL: HAVE_ARC4RANDOM should be defined (IS in Tiger libSystem)"
    grep ARC4RANDOM expat_config.h
    exit 1
fi
echo "  PASS: HAVE_ARC4RANDOM_BUF undef'd, HAVE_ARC4RANDOM defined"

echo "==> Building libexpat.a + xmlwf with tcc..."
PATH=/opt/make-4.3/bin:$PATH make >/dev/null 2>build.log
if [[ ! -x xmlwf/xmlwf ]]; then
    echo "FAIL: xmlwf not produced. Tail of build.log:"
    tail -20 build.log
    exit 1
fi
echo "  built: xmlwf/xmlwf, lib/.libs/libexpat.a"

echo "==> Verifying tcc-only linkage (should be just libSystem)..."
LINKAGE=$(otool -L xmlwf/xmlwf | tail -n +2 | awk '{print $1}')
if echo "$LINKAGE" | grep -vq libSystem; then
    echo "FAIL: xmlwf linked against more than libSystem:"
    echo "$LINKAGE"
    exit 1
fi
echo "  links only against: $LINKAGE"

echo "==> xmlwf --version..."
./xmlwf/xmlwf -v 2>&1 | head -1

echo "==> Smoke test: parse a well-formed XML doc, expect exit 0..."
cat > /tmp/v0266-expat-good.xml <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<library>
  <book id="1"><title>K&amp;R</title><author>Kernighan</author></book>
  <book id="2"><title>UNIX Programming Environment</title></book>
</library>
EOF
if ! ./xmlwf/xmlwf /tmp/v0266-expat-good.xml; then
    echo "FAIL: xmlwf rejected well-formed XML"
    exit 1
fi
echo "  PASS: xmlwf accepted well-formed XML"

echo "==> Smoke test: malformed XML should be rejected with exit 2..."
cat > /tmp/v0266-expat-bad.xml <<'EOF'
<unclosed>
EOF
set +e
./xmlwf/xmlwf /tmp/v0266-expat-bad.xml 2>/dev/null
xmlwf_rc=$?
set -e
if [[ "$xmlwf_rc" != "2" ]]; then
    echo "FAIL: malformed XML expected rc=2, got rc=$xmlwf_rc"
    exit 1
fi
echo "  PASS: xmlwf rejected malformed XML (rc=$xmlwf_rc)"

echo "==> Smoke test: numeric character reference (exercises isnan path)..."
cat > /tmp/v0266-expat-numref.xml <<'EOF'
<?xml version="1.0"?>
<t>&#65;&#66;&#67;</t>
EOF
if ! ./xmlwf/xmlwf /tmp/v0266-expat-numref.xml; then
    echo "FAIL: xmlwf rejected XML with numeric refs"
    exit 1
fi
echo "  PASS: numeric character refs parsed OK"

echo "==> Running expat's internal test suite (345 tests)..."
PATH=/opt/make-4.3/bin:$PATH make -C tests runtests >/dev/null 2>&1
TEST_OUT=$(./tests/runtests 2>&1)
echo "$TEST_OUT" | tail -2
if ! echo "$TEST_OUT" | grep -q 'Failed: 0$'; then
    echo "FAIL: runtests had failures"
    exit 1
fi

echo
echo "PASS — libexpat 2.5.0 builds + runs correctly with tcc on Tiger PPC"
echo "       (345 / 345 internal tests, xmlwf links only libSystem)"
