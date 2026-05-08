#!/usr/bin/env bash
# Demo for v0.2.39-g3 — gzip 1.11 builds + round-trips with tcc
#
# Real-world program build: GNU gzip 1.11. ~140 .c files (gnulib
# included), classic command-line tool. Builds clean with tcc as
# CC, links against libSystem only, round-trips byte-identical
# data, and the system /usr/bin/gzip can decompress tcc-built
# gzip's output (and vice versa).
#
# Configuration uses leopard.sh's pre-built gzip binpkg's bundled
# config.cache — that cache is the result of gcc-4.0 actually
# probing each gnulib function on Tiger PPC, so it has the
# accurate `=no` answers for missing glibc-isms (__freading,
# __fseterr, fdopendir, utimensat, ...).
#
# Why we don't use the leopard.sh tiger.cache + tiger.32.cache
# directly: those provide optimistic defaults like
# `ac_cv_func___freading=${ac_cv_func___freading=yes}`. gcc-4.0
# with `-Werror=implicit-function-declaration` will re-probe and
# correct these to "no" on Tiger; tcc is more permissive on
# implicit decls, accepts the cache's "yes", and the build then
# blows up when gnulib's fseterr.h tries to include the missing
# stdio_ext.h. The binpkg's cache has the corrected values
# already baked in.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0239-gzip-build"
PREFIX="$TMP/v0239-gzip-install"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

rm -rf "$WORK" "$PREFIX"
mkdir -p "$WORK"
cd "$WORK"

echo "==> Downloading gzip 1.11..."
if [[ ! -f gzip-1.11.tar.gz ]]; then
    curl -sL https://leopard.sh/tigersh/dist/gzip-1.11.tar.gz \
         -o gzip-1.11.tar.gz
fi

echo "==> Downloading leopard.sh gzip-1.11 binpkg (just for its config.cache)..."
if [[ ! -f gzip-1.11-binpkg.tar.gz ]]; then
    curl -sL https://leopard.sh/tigersh/binpkgs/gzip-1.11.tiger.g3.tar.gz \
         -o gzip-1.11-binpkg.tar.gz
fi
# Pull just the cache file out of the binpkg.
tar xzf gzip-1.11-binpkg.tar.gz \
    gzip-1.11/share/tiger.sh/gzip-1.11/config.cache.gz
gunzip -c gzip-1.11/share/tiger.sh/gzip-1.11/config.cache.gz > leopard.cache
rm -rf gzip-1.11   # remove the binpkg's gzip dir before extracting source

tar xzf gzip-1.11.tar.gz
cd gzip-1.11

# Use the leopard.sh gzip binpkg's bundled cache, but strip the
# env-locked entries that record the gcc-4.0 build environment —
# configure refuses to reuse a cache from a different CC/CFLAGS.
grep -v -E "^ac_cv_env_|^ac_cv_build|^ac_cv_host|^ac_cv_prog_CC|^ac_cv_prog_cc_" \
    ../leopard.cache > config.cache

echo "==> Configuring with tcc as CC..."
CC="$TCC_ABS" \
  CFLAGS="-B$TCCROOT -L$TCCROOT -I$TCCROOT/include" \
  ./configure -C --prefix="$PREFIX" > configure.log 2>&1

echo "==> Building..."
make -j2 > build.log 2>&1 || { echo "FAIL: build failed; tail of log:"; tail -30 build.log; exit 1; }

if [[ ! -x gzip ]]; then
    echo "FAIL: ./gzip not produced"
    exit 1
fi

echo "==> Verifying gzip is a PPC Mach-O binary..."
if ! file gzip | grep -q "Mach-O.*ppc"; then
    echo "FAIL: gzip is not a PPC Mach-O binary:"
    file gzip
    exit 1
fi

echo "==> Verifying --version reports gzip 1.11..."
VER=$(./gzip --version | head -1)
if [[ "$VER" != "gzip 1.11" ]]; then
    echo "FAIL: unexpected version: $VER"
    exit 1
fi
echo "  $VER"

echo "==> Round-trip test on /etc/services..."
SVC="$WORK/svc.txt"
GZF="$WORK/svc.gz"
RT="$WORK/svc.roundtrip"
cp /etc/services "$SVC"
ORIG_MD5=$(md5 -q "$SVC")
ORIG_SZ=$(stat -f%z "$SVC")

./gzip -k -c "$SVC" > "$GZF"
GZ_SZ=$(stat -f%z "$GZF")
echo "  $ORIG_SZ bytes compressed -> $GZ_SZ bytes ($((GZ_SZ * 100 / ORIG_SZ))%)"

./gzip -d -c "$GZF" > "$RT"
RT_MD5=$(md5 -q "$RT")
if [[ "$ORIG_MD5" != "$RT_MD5" ]]; then
    echo "FAIL: round-trip MD5 mismatch ($ORIG_MD5 vs $RT_MD5)"
    exit 1
fi
echo "  round-trip OK (md5 $ORIG_MD5)"

echo "==> Verifying system /usr/bin/gzip can decompress tcc-built gzip output..."
SYS_RT="$WORK/svc.system-decompressed"
/usr/bin/gzip -d -c "$GZF" > "$SYS_RT"
SYS_MD5=$(md5 -q "$SYS_RT")
if [[ "$ORIG_MD5" != "$SYS_MD5" ]]; then
    echo "FAIL: system gzip decompressed differently ($ORIG_MD5 vs $SYS_MD5)"
    exit 1
fi
echo "  cross-tool compatible (system gzip reads tcc-built output)"

echo "==> Reverse: tcc-built gzip decompresses system gzip's output..."
SYS_GZF="$WORK/svc.system.gz"
TCC_RT="$WORK/svc.tcc-decompressed"
# Tiger's /usr/bin/gzip doesn't support -k (keep input); use stdin instead.
/usr/bin/gzip -c < "$SVC" > "$SYS_GZF"
./gzip -d -c "$SYS_GZF" > "$TCC_RT"
TCC_MD5=$(md5 -q "$TCC_RT")
if [[ "$ORIG_MD5" != "$TCC_MD5" ]]; then
    echo "FAIL: tcc gzip decompressed system gzip output differently"
    exit 1
fi
echo "  reverse compatible (tcc-built gzip reads system gzip output)"

echo
echo "PASS — gzip 1.11 builds with tcc and round-trips byte-identical"
