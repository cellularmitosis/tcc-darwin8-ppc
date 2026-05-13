#!/opt/tigersh-deps-0.1/bin/bash
# build-csmith-on-ppc.sh — build csmith 2.3.0 from source on a Tiger
# 10.4 PowerPC G3 host (imacg3 / ibookg37 / ...).
#
# Contingency for: open item #5 ("csmith building on a PPC host").
# As of 2026-05-12 uranium is the only host in the fleet with a
# csmith binary (Homebrew on Apple Silicon); ibookg38 had one but
# was written off as bad hardware in session 067. If we can't
# resurrect ibookg38 to grab its binary, run this script on a
# remaining G3 to get a working csmith there.
#
# Usage (run directly on the PPC host):
#     bash scripts/build-csmith-on-ppc.sh
#
# Or remote-run from uranium:
#     scp scripts/build-csmith-on-ppc.sh imacg3:/tmp/build-csmith.sh
#     ssh imacg3 '/opt/tigersh-deps-0.1/bin/bash /tmp/build-csmith.sh'
#
# Environment overrides (defaults shown):
#     INSTALL_PREFIX=$HOME/csmith-2.3.0
#     BUILD_DIR=$HOME/tmp/csmith-build-2.3.0
#
# What it does:
#   1. Sanity-checks the Tiger-PPC toolchain (gcc-4.9.4, make-4.3,
#      curl-7.87, openssl-1.1.1t).
#   2. Downloads csmith-2.3.0 from GitHub via curl with modern TLS.
#   3. Verifies sha256 against the upstream brew formula's value.
#   4. Builds with gcc-4.9.4 (the only C++11 compiler we have on
#      Tiger PPC) and installs to $INSTALL_PREFIX.
#   5. Mirrors brew's runtime-headers layout at
#      $INSTALL_PREFIX/include/csmith-2.3.0/ so existing campaign
#      scripts that expect CSMITH_PATH there Just Work.
#   6. Self-tests by generating a 1-seed program.
#
# Idempotent: if a working csmith $CSMITH_VERSION is already present
# at $INSTALL_PREFIX, exits 0 without rebuilding.

set -eu
set -o pipefail

CSMITH_VERSION="2.3.0"
CSMITH_TARBALL_URL="https://github.com/csmith-project/csmith/archive/refs/tags/csmith-${CSMITH_VERSION}.tar.gz"
# sha256 verified against the Homebrew csmith.rb formula on uranium.
CSMITH_TARBALL_SHA256="9d024a6b202f6a1b9e01351218a85888c06b67b837fe4c6f8ef5bd522fae098c"

INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/csmith-${CSMITH_VERSION}}"
BUILD_DIR="${BUILD_DIR:-$HOME/tmp/csmith-build-${CSMITH_VERSION}}"

# Tiger-PPC toolchain paths (verified on imacg3 in session 067; same
# layout expected on ibookg37 via tiger.sh provisioning).
GCC_PREFIX="/opt/gcc-4.9.4"
GCC="$GCC_PREFIX/bin/gcc-4.9"
GXX="$GCC_PREFIX/bin/g++-4.9"
MAKE="/opt/make-4.3/bin/make"
CURL="/opt/curl-7.87.0/bin/curl"
OPENSSL="/opt/openssl-1.1.1t/bin/openssl"

say()  { printf '==> %s\n' "$*"; }
die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# ---- Step 0: host + toolchain sanity ----------------------------------------

case "$(uname -p)" in
    powerpc)            ;;
    *) die "$(uname -p) is not powerpc — this script is Tiger-PPC only";;
esac

case "$(uname -r)" in
    8.*|9.*)            ;;
    *) say "WARNING: $(uname -sr) is not Tiger / Leopard — proceeding anyway";;
esac

for f in "$GCC" "$GXX" "$MAKE" "$CURL" "$OPENSSL"; do
    [ -x "$f" ] || die "$f missing or not executable (tigerbrew/tiger.sh layout assumed)"
done

# ---- Step 1: short-circuit if already installed -----------------------------

if [ -x "$INSTALL_PREFIX/bin/csmith" ]; then
    if "$INSTALL_PREFIX/bin/csmith" --version 2>&1 | grep -q "$CSMITH_VERSION"; then
        say "csmith $CSMITH_VERSION already installed at $INSTALL_PREFIX"
        "$INSTALL_PREFIX/bin/csmith" --version
        exit 0
    fi
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# ---- Step 2: download tarball -----------------------------------------------

TARBALL="$BUILD_DIR/csmith-${CSMITH_VERSION}.tar.gz"

verify_sha() {
    local f="$1" want="$2"
    local got
    got="$("$OPENSSL" dgst -sha256 "$f" | awk '{print $NF}')"
    [ "$got" = "$want" ]
}

if [ ! -f "$TARBALL" ] || ! verify_sha "$TARBALL" "$CSMITH_TARBALL_SHA256"; then
    say "Downloading csmith $CSMITH_VERSION from GitHub..."
    "$CURL" -L --fail --output "$TARBALL.tmp" "$CSMITH_TARBALL_URL"
    mv "$TARBALL.tmp" "$TARBALL"
fi

if ! verify_sha "$TARBALL" "$CSMITH_TARBALL_SHA256"; then
    actual="$("$OPENSSL" dgst -sha256 "$TARBALL" | awk '{print $NF}')"
    die "sha256 mismatch on $TARBALL — expected $CSMITH_TARBALL_SHA256, got $actual"
fi
say "sha256 OK"

# ---- Step 3: extract --------------------------------------------------------

SRC_DIR="$BUILD_DIR/csmith-csmith-${CSMITH_VERSION}"
if [ ! -d "$SRC_DIR" ]; then
    say "Extracting tarball..."
    tar xzf "$TARBALL" -C "$BUILD_DIR"
fi
cd "$SRC_DIR"

# ---- Step 4: configure ------------------------------------------------------
#
# PATH layout (order matters):
#   * /usr/bin first so ar/ranlib/strip resolve to Apple's universal
#     binaries (the only ar that works correctly on G3 — /opt/cctools-*
#     on some hosts is built G4-only, see host_ibookg38.md).
#   * /opt/gcc-4.9.4/bin so the -4.9-suffixed compiler binaries are on
#     PATH for any indirect invocation (we also set CC/CXX explicitly).
#   * /opt/make-4.3/bin so configure's make detection picks up 4.3
#     instead of system 3.80 (avoids the silent $(or ...) failure
#     captured in session 067's HANDOFF).

export PATH="/usr/bin:$GCC_PREFIX/bin:/opt/make-4.3/bin:$PATH"
export CC="$GCC"
export CXX="$GXX"

# Force libstdc++ to come from gcc-4.9.4 (system libstdc++.6.dylib is
# the gcc-4.0-era ABI and won't link a C++11 binary).
export CXXFLAGS="${CXXFLAGS:-} -O2"
export LDFLAGS="${LDFLAGS:-} -L$GCC_PREFIX/lib"

if [ ! -f "Makefile" ]; then
    say "Running ./configure..."
    ./configure \
        --prefix="$INSTALL_PREFIX" \
        --disable-dependency-tracking \
        2>&1 | tee "$BUILD_DIR/configure.log" | tail -20
fi

# ---- Step 5: build ----------------------------------------------------------

say "Building (this takes ~5-10 minutes on a G3)..."
"$MAKE" -j2 2>&1 | tee "$BUILD_DIR/make.log" | tail -8 || {
    say "Build failed — full log: $BUILD_DIR/make.log"
    exit 1
}

# ---- Step 6: install --------------------------------------------------------

say "Installing to $INSTALL_PREFIX..."
"$MAKE" install 2>&1 | tee "$BUILD_DIR/install.log" | tail -8

# Mirror Homebrew's runtime-headers location so existing campaign
# scripts can use CSMITH_PATH=$INSTALL_PREFIX/include/csmith-$CSMITH_VERSION.
mkdir -p "$INSTALL_PREFIX/include/csmith-$CSMITH_VERSION"
cp runtime/*.h "$INSTALL_PREFIX/include/csmith-$CSMITH_VERSION/"

# ---- Step 7: verify ---------------------------------------------------------

say "Verifying install..."
"$INSTALL_PREFIX/bin/csmith" --version

TMP_C="$BUILD_DIR/self-test-seed1.c"
"$INSTALL_PREFIX/bin/csmith" --seed 1 -o "$TMP_C"
[ -s "$TMP_C" ] || die "csmith produced empty output for --seed 1"
say "csmith --seed 1 produced $(wc -l < "$TMP_C") lines of C"

# Show dynamic-library deps so a future cross-host copy knows what
# it'll need to bring along.
say "Dynamic deps (otool -L):"
/usr/bin/otool -L "$INSTALL_PREFIX/bin/csmith" | sed 's/^/    /'

# ---- Summary ----------------------------------------------------------------

cat <<EOF

SUCCESS: csmith $CSMITH_VERSION installed at $INSTALL_PREFIX

To use:
    export PATH="$INSTALL_PREFIX/bin:\$PATH"
    export CSMITH_PATH="$INSTALL_PREFIX/include/csmith-$CSMITH_VERSION"

Runtime headers:    $INSTALL_PREFIX/include/csmith-$CSMITH_VERSION/
Build logs:         $BUILD_DIR/{configure,make,install}.log

To copy this build to a sibling G3 host, also bring:
    $GCC_PREFIX/lib/libstdc++.6.dylib
    $GCC_PREFIX/lib/libgcc_s.1.dylib
(or rebuild from this script on the target host).
EOF
