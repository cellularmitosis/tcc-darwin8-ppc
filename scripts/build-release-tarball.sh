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
# Run on the G3 (or wherever a working ./tcc exists). Tarball lands
# in <repo>/artifacts/ (gitignored — canonical copy is the GitHub
# release asset).

set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUTDIR="$ROOT/artifacts"
mkdir -p "$OUTDIR"

VERSION="${VERSION:-v0.2.10-g3}"
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

What's new (cumulative since v0.1.0-g3):
  * Self-link: bootstrap-tcc-self.sh no longer needs gcc-4.0; tcc
    links its own tcc-self via auto-loaded /usr/lib/crt1.o.
  * Bundled libgcc helpers in libtcc1.a (long-long arithmetic,
    IEEE 754 conversions, __eprintf for assert, no-op bound-check
    stubs, single-threaded atomic stubs).
  * Self-host fixpoint: tcc -> tcc-self -> tcc-self2 -> tcc-self3,
    with tcc-self2.o == tcc-self3.o byte-identical.
  * Struct-by-value parameters (≤ 32 bytes) AND struct returns via
    hidden pointer per the Apple PPC ABI.
  * EXE PIC reloc fallback handles forward-defined and aliased
    symbols (a regression's break-out-of-loop bug is also fixed).
  * Big-endian sub-word param offset; FP-to-LL libgcc helper
    return-swap; absolute-address load/store; computed goto;
    void* deref; VT_BOOL handling.
  * Tiger realpath workaround in normalized_PATHCMP.
  * Long-frame prolog/epilog (>32KB stack frames work).
  * Long-offset local load/store/address-of.
  * VLAs (variable-length arrays) supported via gen_vla_alloc.
  * Constructor / destructor support: __attribute__((constructor))
    and __attribute__((destructor)) emit __mod_init_func and
    __mod_term_func sections that dyld walks at startup / exit.
  * VLA + function-call interaction is now safe: each VLA reserves
    a 128-byte buffer below its data so callee param-spills don't
    corrupt the array.
  * tcc -run mode works for the first time on PPC:
    create_plt_entry + relocate_plt now generate proper 4-instruction
    branch islands (lis/ori/mtctr/bctr) that JIT-resolve every libc
    call through dlsym. Simple programs (printf hi-world, struct
    tests, etc.) now run end-to-end via "tcc -run".
  * Lock-free atomics for 1-, 2-, and 4-byte widths via lwarx/stwcx
    (tcc/lib/atomic-ppc.S, compiled by gcc-4.0 via a per-file
    Makefile rule). Byte and short use word-RMW with masking
    since PPC32 has no lbarx/sbarx. 8-byte stays under
    pthread_mutex (no ldarx/stdcx on PPC32). Real-world impact:
    124_atomic_counter (16 threads x 65535 ops x 4 widths)
    drops from ~6m23s to 2.4s -- 137x speedup.
  * Variadic FP arg shadow spill: a long-standing codegen bug
    where printf calls with FP args whose GPR shadow slots ran
    past r10 (gslot >= 8) didn't actually write the value to the
    outgoing parameter stack. printf read garbage from there.
    The fix flipped two tests (73_arm64, 70_floating_point_literals)
    that had been suspected of unfixable platform-mismatch /
    upstream-parser causes.
  * tests2 baseline at this release: 108 / 118 (91.5%) under the
    default -o exe path. Total count is 118 not 122 because four
    LE-byte-order-specific tests (90_struct-init,
    91_ptr_longlong_arith32, 95_bitfields, 95_bitfields_ms) are
    properly skipped on big-endian. +3 vs v0.2.8: 104_inline
    via N_WEAK_REF fix, 73_arm64 + 70_floating_point_literals
    via the variadic FP shadow fix.

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
echo "==> step 4/4: package artifacts/$TARNAME"
( cd /tmp && tar czf "$OUTDIR/$TARNAME" "$PKGNAME" )
ls -la "$OUTDIR/$TARNAME"

echo
echo "Done. Install with:"
echo "  sudo tar -C /opt -xzf artifacts/$TARNAME"
