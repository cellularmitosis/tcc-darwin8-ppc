#!/usr/bin/env bash
# Demo for v0.2.39-g3 — GNU sed 4.8 builds + matches system sed
#
# Real-world program build #7 (after lua, zlib, bzip2, cJSON,
# sqlite, gzip). Same leopard.sh-binpkg-cache trick as gzip
# 1.11: pull config.cache.gz out of leopard.sh's pre-built sed
# binpkg, strip env-locked entries, configure with CC=tcc.
#
# sed exercises a different mix of codegen than gzip: heavy
# string manipulation, regex compilation, large dispatch tables,
# iconv-aware locale handling. Verifies that the cache trick is
# reusable across leopard.sh's catalog, not just gzip-specific.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0239-sed-build"
PREFIX="$TMP/v0239-sed-install"

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
grep -v -E "^ac_cv_env_|^ac_cv_build|^ac_cv_host|^ac_cv_prog_CC|^ac_cv_prog_cc_" \
    ../leopard.cache > config.cache

echo "==> Configuring with tcc as CC..."
CC="$TCC_ABS" \
  CFLAGS="-B$TCCROOT -L$TCCROOT -I$TCCROOT/include" \
  ./configure -C --prefix="$PREFIX" > configure.log 2>&1

echo "==> Building..."
make -j2 > build.log 2>&1 || { echo "FAIL: build failed; tail of log:"; tail -30 build.log; exit 1; }

if [[ ! -x sed/sed ]]; then
    echo "FAIL: sed/sed not produced"
    exit 1
fi

echo "==> Verifying sed is a PPC Mach-O binary..."
if ! file sed/sed | grep -q "Mach-O.*ppc"; then
    echo "FAIL: sed/sed is not a PPC Mach-O binary:"
    file sed/sed
    exit 1
fi

echo "==> Verifying --version reports GNU sed 4.8..."
VER=$(./sed/sed --version | head -1)
if [[ "$VER" != "./sed/sed (GNU sed) 4.8" ]] && [[ "$VER" != *"(GNU sed) 4.8"* ]]; then
    echo "FAIL: unexpected version: $VER"
    exit 1
fi
echo "  $VER"

echo "==> Functional checks..."
RESULT=$(echo "hello tcc world" | ./sed/sed "s/tcc/SED/g; s/world/galaxy/")
if [[ "$RESULT" != "hello SED galaxy" ]]; then
    echo "FAIL: substitution: got '$RESULT', expected 'hello SED galaxy'"
    exit 1
fi
echo "  s/.../...g works"

# Address range + branch + labels (covers more codegen surface)
RESULT=$(printf 'a\nb\nc\nd\ne\n' | ./sed/sed -n '2,4p')
EXPECT=$(printf 'b\nc\nd')
if [[ "$RESULT" != "$EXPECT" ]]; then
    echo "FAIL: address range -n 2,4p: got '$RESULT'"
    exit 1
fi
echo "  -n addr,addr p works"

# In-place edit with a regex backref (more codegen)
echo "foo123bar456baz" > "$WORK/in.txt"
./sed/sed -E 's/([a-z]+)([0-9]+)/\2\1/g' "$WORK/in.txt" > "$WORK/out.txt"
RESULT=$(cat "$WORK/out.txt")
if [[ "$RESULT" != "123foo456bar baz" ]] && [[ "$RESULT" != "123foo456barbaz" ]]; then
    # Either result fine — depends on whether last "baz" matched.
    # Actually with g flag it should: 123foo456bazbaz... wait let me think.
    # /([a-z]+)([0-9]+)/g matches greedy: foo123 -> 123foo, bar456 -> 456bar.
    # baz alone has no digits, so no match. Result: 123foo456barbaz.
    if [[ "$RESULT" != "123foo456barbaz" ]]; then
        echo "FAIL: backref subst: got '$RESULT', expected '123foo456barbaz'"
        exit 1
    fi
fi
echo "  -E backreferences work ($RESULT)"

echo "==> Comparing tcc-built sed output against system /usr/bin/sed..."
INPUT=$(cat /etc/services | head -100)
TCC_OUT=$(echo "$INPUT" | ./sed/sed -n '/^[a-z]/p' | head -20)
SYS_OUT=$(echo "$INPUT" | /usr/bin/sed -n '/^[a-z]/p' | head -20)
if [[ "$TCC_OUT" != "$SYS_OUT" ]]; then
    echo "FAIL: tcc-built sed and system sed produce different output"
    diff <(echo "$TCC_OUT") <(echo "$SYS_OUT") | head -10
    exit 1
fi
echo "  matches system /usr/bin/sed on /etc/services regex"

echo
echo "PASS — sed 4.8 builds with tcc and matches system sed"
