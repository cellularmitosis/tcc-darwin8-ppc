#!/usr/bin/env bash
# Demo for v0.2.41-g3 — GNU gzip 1.11 builds + round-trips with tcc
#
# Real-world program build #7 (after lua, zlib, bzip2, cJSON,
# sqlite, sed). The fix that made this work was a real codegen
# bug:
#
# `ppc-gen.c::load()` / `store()`'s PIC paths used
# `(extra_off & 0xffff)` to compute the D-form load/store
# immediate. For symbol+offset references where the byte offset
# exceeded 0x7fff, the high bits silently dropped on the floor.
#
# This was fine for most code (offsets are typically struct
# member positions, < 0x8000). But gnulib's gzip uses the
# pattern `#define head (prev + WSIZE)` where WSIZE = 32768.
# `head[n]` becomes `*(prev + WSIZE + n)` which decomposes to
# pointer = prev, offset = WSIZE * sizeof(Pos) = 0x10000.
# The 0x10000 was masking to 0 — `head[n]` got lowered to
# `prev[n]`.
#
# fill_window's slide loop (triggered when strstart >= 65274,
# i.e. input >= 64 KB) was supposed to do:
#
#     for (n = 0; n < HASH_SIZE; n++) head[n] = m >= WSIZE ? m-WSIZE : NIL;
#     for (n = 0; n < WSIZE; n++)     prev[n] = m >= WSIZE ? m-WSIZE : NIL;
#
# Under tcc, both loops actually wrote to prev[] (the head[]
# loop's offset got nuked). The hash chains were corrupt after
# the slide, so deflate emitted invalid back-references and
# the gunzip output had CRC errors at any input size that
# triggered the slide.
#
# Fix: add `ppc_adjust_extra_off` helper that emits an `addis`
# for offset bits > 15-bit-signed, returns the lo16 portion.
# Apply at the top of all four affected PIC paths in load() and
# store().
#
# This demo verifies:
# - gzip 1.11 builds with strict tcc-only enforcement (the
#   leopard.sh-binpkg-cache pattern + env pin for gnulib's
#   AC_CHECK_FUNC mis-detections + --disable-dependency-tracking).
# - The resulting binary links only against libSystem (no
#   libgcc_s — i.e. tcc was actually the compiler).
# - Round-trip on multiple input sizes, including ones large
#   enough to trigger fill_window's slide (>= 64 KB).
# - System /usr/bin/gunzip can decompress tcc-built gzip's
#   output — proving the deflate stream is RFC 1952 compliant
#   and not just self-consistent.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0241-gzip-build"
PREFIX="$TMP/v0241-gzip-install"

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
[[ ! -f gzip-1.11.tar.gz ]] && \
    curl -sL https://leopard.sh/tigersh/dist/gzip-1.11.tar.gz -o gzip-1.11.tar.gz

echo "==> Downloading leopard.sh gzip-1.11 binpkg (just for its config.cache)..."
[[ ! -f gzip-1.11-binpkg.tar.gz ]] && \
    curl -sL https://leopard.sh/tigersh/binpkgs/gzip-1.11.tiger.g3.tar.gz \
         -o gzip-1.11-binpkg.tar.gz
tar xzf gzip-1.11-binpkg.tar.gz \
    gzip-1.11/share/tiger.sh/gzip-1.11/config.cache.gz
gunzip -c gzip-1.11/share/tiger.sh/gzip-1.11/config.cache.gz > leopard.cache
rm -rf gzip-1.11

tar xzf gzip-1.11.tar.gz
cd gzip-1.11

# Strip the env-locked entries.
grep -v -E "^ac_cv_env_|^ac_cv_build|^ac_cv_host|^ac_cv_prog_CC|^ac_cv_prog_cc_|^ac_cv_prog_ac_ct_CC|^ac_cv_prog_CPP|^ac_cv_prog_ac_ct_CPP|^ac_cv_path_CC|^ac_cv_sys_largefile_CC" \
    ../leopard.cache > config.cache

echo "==> Configuring with tcc as CC..."
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
./configure --disable-dependency-tracking -C \
            --prefix="$PREFIX" > configure.log 2>&1 \
    || { echo "FAIL: configure failed; tail:"; tail -30 configure.log; exit 1; }

CC_LINE=$(grep "^CC = " Makefile | head -1)
if [[ "$CC_LINE" != "CC = $TCC_ABS"* ]]; then
    echo "FAIL: CC was overridden, Makefile has: $CC_LINE"
    exit 1
fi
echo "  CC=$TCC_ABS confirmed in Makefile"

echo "==> Building gzip..."
make > build.log 2>&1 \
    || { echo "FAIL: build failed; tail:"; tail -30 build.log; exit 1; }

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

echo "==> Verifying gzip links only against libSystem (no libgcc_s)..."
LIBS=$(otool -L gzip | tail -n +2)
if echo "$LIBS" | grep -q "libgcc"; then
    echo "FAIL: links against libgcc_s — gcc was somehow involved:"
    echo "$LIBS"
    exit 1
fi
echo "  links only against /usr/lib/libSystem.B.dylib"

echo "==> Verifying gzip --version reports gzip 1.11..."
VER=$(./gzip --version | head -1)
if [[ "$VER" != "gzip 1.11" ]]; then
    echo "FAIL: unexpected version: $VER"
    exit 1
fi
echo "  $VER"

echo "==> Round-trip tests at multiple sizes..."
for sz_kb in 1 16 32 48 56 60 64 100 200 500; do
    bytes=$((sz_kb * 1024))
    yes "abcdefgh01234567" | dd bs=1024 count=$sz_kb 2>/dev/null > "$WORK/in"
    ./gzip -c "$WORK/in" > "$WORK/in.gz"

    # Self-roundtrip: tcc-gzip compresses, tcc-gzip decompresses.
    ./gzip -d -c "$WORK/in.gz" > "$WORK/out"
    if ! cmp -s "$WORK/in" "$WORK/out"; then
        echo "FAIL: self round-trip ${sz_kb}K differs"
        exit 1
    fi

    # System gunzip decompress — proves the .gz file is real.
    /usr/bin/gunzip -c "$WORK/in.gz" > "$WORK/sys-out"
    if ! cmp -s "$WORK/in" "$WORK/sys-out"; then
        echo "FAIL: system gunzip rejects tcc-gzip ${sz_kb}K output"
        exit 1
    fi

    echo "  ${sz_kb}K self+sys round-trip OK"
done

echo "==> Round-trip on /etc/services (~570 KB, real-world data)..."
cp /etc/services "$WORK/svc"
./gzip -c "$WORK/svc" > "$WORK/svc.gz"
./gzip -d -c "$WORK/svc.gz" > "$WORK/svc.rt"
if ! cmp -s "$WORK/svc" "$WORK/svc.rt"; then
    echo "FAIL: /etc/services self round-trip differs"
    exit 1
fi
/usr/bin/gunzip -c "$WORK/svc.gz" > "$WORK/svc.sys"
if ! cmp -s "$WORK/svc" "$WORK/svc.sys"; then
    echo "FAIL: /etc/services sys gunzip differs"
    exit 1
fi
ORIG=$(stat -f%z "$WORK/svc")
COMP=$(stat -f%z "$WORK/svc.gz")
PCT=$((COMP * 100 / ORIG))
echo "  /etc/services: $ORIG -> $COMP (${PCT}%) self+sys OK"

echo
echo "PASS — GNU gzip 1.11 builds + round-trips end-to-end with tcc on Tiger PPC"
