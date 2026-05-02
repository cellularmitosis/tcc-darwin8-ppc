#!/bin/sh
# build-release-tarball.sh — produce a /opt-installable tarball for
# tcc-darwin8-ppc on Tiger PowerPC. Default version v0.2.0-g3.
#
# Layout (tarball expands into /opt/tcc-darwin8-ppc-<version>/):
#   bin/tcc                  — the self-hosted PPC binary
#   lib/tcc/libtcc1.a        — runtime helpers
#   lib/tcc/include/         — tcc's own headers
#   share/doc/tcc-darwin8-ppc/README
#   share/doc/tcc-darwin8-ppc/RELEASE_NOTES
#   share/doc/tcc-darwin8-ppc/SELF_HOST_NOTES   (020 writeup)
#   share/doc/tcc-darwin8-ppc/SELF_LINK_NOTES   (027 writeup)
#
# Run on the G3 (or wherever a working ./tcc exists). Defaults to
# producing tcc-darwin8-ppc-v0.2.0-g3.tar.gz in the repo root.

set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)

VERSION="${VERSION:-v0.2.2-g3}"
PKGNAME=tcc-darwin8-ppc-$VERSION
TARNAME=$PKGNAME.tar.gz
PREFIX=/opt/$PKGNAME

# Step 1: build the host tcc fresh.
echo "==> step 1/4: build host tcc with gcc-4.0"
( cd tcc && [ -f config.mak ] || ./configure --config-semlock=no --prefix=$PREFIX )
( cd tcc && /opt/make-4.3/bin/make >/dev/null )

# Step 2: bootstrap tcc-self with the full self-host fixpoint.
# bootstrap-tcc-self.sh produces a tcc-self compiled AND linked by
# tcc itself (no gcc-4.0). FIXPOINT=1 then verifies tcc-self2.o ==
# tcc-self3.o (canonical self-host fixpoint).
echo "==> step 2/4: bootstrap tcc-self + fixpoint test"
WORK=/tmp/$PKGNAME-build/self1
OUT=/tmp/$PKGNAME-build/tcc-self
mkdir -p "$WORK"
FIXPOINT=1 TCC=./tcc/tcc OUT="$OUT" WORK="$WORK" \
    ./scripts/bootstrap-tcc-self.sh

# Step 3: stage the install tree.
echo "==> step 3/4: stage install tree under /tmp/$PKGNAME"
STAGE=/tmp/$PKGNAME
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib/tcc/include" "$STAGE/share/doc/tcc-darwin8-ppc"
install -m 755 "$OUT" "$STAGE/bin/tcc"
install -m 644 tcc/libtcc1.a "$STAGE/lib/tcc/libtcc1.a"
install -m 644 tcc/include/*.h "$STAGE/lib/tcc/include/"
install -m 644 tcc/tcclib.h "$STAGE/lib/tcc/include/"
install -m 644 README.md "$STAGE/share/doc/tcc-darwin8-ppc/README"
install -m 644 docs/sessions/020-self-host/README.md "$STAGE/share/doc/tcc-darwin8-ppc/SELF_HOST_NOTES"
install -m 644 docs/sessions/027-self-link/README.md "$STAGE/share/doc/tcc-darwin8-ppc/SELF_LINK_NOTES"
cat > "$STAGE/share/doc/tcc-darwin8-ppc/RELEASE_NOTES" <<EOF
tcc-darwin8-ppc $VERSION
========================

Fully self-hosted release: tcc compiles AND links itself with no
gcc-4.0 involvement. Targets 32-bit PowerPC on Mac OS X 10.4
Tiger (Mach-O). Built and bootstrapped on a real iBook G3/G4.

What's new in $VERSION (over v0.1.0-g3):
  * Self-link: bootstrap-tcc-self.sh no longer needs gcc-4.0; tcc
    links its own tcc-self via auto-loaded /usr/lib/crt1.o.
  * Bundled libgcc helpers in libtcc1.a (long-long arithmetic,
    IEEE 754 conversions).
  * Self-host fixpoint reached: tcc -> tcc-self -> tcc-self2 ->
    tcc-self3, with tcc-self2.o == tcc-self3.o byte-identical.

Install:
  sudo mkdir -p $PREFIX
  sudo tar -C /opt -xzf $TARNAME
  Then add $PREFIX/bin to PATH or invoke as $PREFIX/bin/tcc.

Try it:
  $PREFIX/bin/tcc -B$PREFIX/lib/tcc -run hello.c
  $PREFIX/bin/tcc -B$PREFIX/lib/tcc -o hello hello.c \\
      $PREFIX/lib/tcc/libtcc1.a

See $PREFIX/share/doc/tcc-darwin8-ppc/SELF_LINK_NOTES for the
v0.2.0 milestone writeup (the four overlapping bugs that had to
be peeled off before self-link worked).

Project: https://github.com/cellularmitosis/tcc-darwin8-ppc
EOF

# Step 4: tar it up.
echo "==> step 4/4: package $TARNAME"
( cd /tmp && tar czf "$ROOT/$TARNAME" "$PKGNAME" )
ls -la "$ROOT/$TARNAME"

echo
echo "Done. Install with:"
echo "  sudo tar -C /opt -xzf $TARNAME"
