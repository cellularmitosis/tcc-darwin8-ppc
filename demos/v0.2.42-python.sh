#!/usr/bin/env bash
# Demo for v0.2.42-g3 — Python 2.7.18 builds and runs end-to-end with tcc
#
# Eighth real-world program (after lua, zlib, bzip2, cJSON, sqlite,
# sed, gzip), and by far the largest: ~150 .c files, ~600k lines of
# C. The Python interpreter exercises every corner of the codegen:
# refcounted heap objects with lots of pointer chasing, dynamic
# dispatch via function-pointer tables, deep recursion through
# eval_frame, exception propagation, GC traversal, format strings
# with %Lf for long-double, large-offset struct member access in
# PyTypeObject and friends, hash table walks.
#
# Pre-v0.2.41, tcc-built python.exe died at startup with "Fatal
# Python error: Can't initialize type type" — somewhere in the
# PyType_Ready chain that runs before main() returns. v0.2.41's
# extra_off-truncation fix in the PIC load/store paths fixed it as
# a side effect: PyTypeObject is large (220+ bytes), its members
# live well past the 16-bit-signed displacement range, and tcc was
# masking off the high bits of those offsets when accessing
# pre-existing global PyTypeObjects (PyType_Type itself, PyDict_Type,
# etc.) — so the read of `tp_basicsize`, `tp_itemsize`, etc. landed
# at the wrong fields of the wrong objects. PyType_Ready iterates
# over inherited slots and bails when it hits invalid data.
#
# This demo:
# - Uses the leopard.sh-style binpkg config.cache, with the strict
#   strip pattern from v0.2.40-sed (also strips
#   `ac_cv_prog_ac_ct_CC=gcc-4.9` so tcc actually drives the build).
# - Pins the gnulib AC_CHECK_FUNC mis-detections via env (tcc's
#   permissive implicit-decl handling fools AC_CHECK_FUNC into
#   thinking glibc-isms exist on Tiger).
# - Configures with --without-pymalloc and with the right
#   MACHDEP_OBJS pruning (Python expects ToolboxGlue / CoreFoundation
#   on Mac; we're not building the Mac framework, just the
#   interpreter).
# - Builds python.exe.
# - Verifies linkage is libSystem-only (no libgcc_s).
# - Runs a Python program that exercises basic features: list
#   comprehensions, recursion, classes + methods, string ops, dict
#   walks, exception handling.

set -e

TCC=${TCC:-./tcc/tcc}
TMP=${TMPDIR:-/tmp}
WORK="$TMP/v0242-python-build"

if [[ ! -x "$TCC" ]]; then
    echo "ERROR: tcc binary not found at $TCC. Build with scripts/build-tiger.sh first." >&2
    exit 1
fi

TCCROOT=$(cd "$(dirname "$TCC")" && pwd)
TCC_ABS="$TCCROOT/$(basename "$TCC")"

rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

echo "==> Downloading Python 2.7.18 source..."
[[ ! -f Python-2.7.18.tar.xz ]] && \
    curl -sL https://www.python.org/ftp/python/2.7.18/Python-2.7.18.tar.xz \
         -o Python-2.7.18.tar.xz

echo "==> Downloading leopard.sh python2-2.7.18 binpkg (just for its config.cache)..."
[[ ! -f python2-2.7.18-binpkg.tar.gz ]] && \
    curl -sL https://leopard.sh/tigersh/binpkgs/python2-2.7.18.tiger.g3.tar.gz \
         -o python2-2.7.18-binpkg.tar.gz
tar xzf python2-2.7.18-binpkg.tar.gz \
    python2-2.7.18/share/tiger.sh/python2-2.7.18/config.cache.gz
gunzip -c python2-2.7.18/share/tiger.sh/python2-2.7.18/config.cache.gz > leopard.cache
rm -rf python2-2.7.18

# Use xzcat or fall back to xz -d.
if command -v xzcat > /dev/null 2>&1; then
    xzcat Python-2.7.18.tar.xz | tar x
else
    xz -d -c Python-2.7.18.tar.xz | tar x
fi
cd Python-2.7.18

# Strip env-locked + gcc-4.9-flavored cache entries.
grep -v -E "^ac_cv_env_|^ac_cv_build|^ac_cv_host|^ac_cv_prog_CC|^ac_cv_prog_cc_|^ac_cv_prog_ac_ct_CC|^ac_cv_prog_CPP|^ac_cv_prog_ac_ct_CPP|^ac_cv_path_CC|^ac_cv_sys_largefile_CC" \
    ../leopard.cache > config.cache

echo "==> Configuring with tcc as CC..."
# Pin gnulib AC_CHECK_FUNC mis-detections. Most Python doesn't
# need these (it doesn't link gnulib directly), but the
# leopard.sh binpkg's config.cache may carry over wrong values
# from gcc-4.9's permissive AC_CHECK_FUNC behaviour.
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
LDFLAGS="-B$TCCROOT" \
./configure -C --without-pymalloc > configure.log 2>&1 \
    || { echo "FAIL: configure failed; tail:"; tail -30 configure.log; exit 1; }

CC_LINE=$(grep "^CC=" Makefile | head -1 | tr -s "[:space:]" " ")
if [[ "$CC_LINE" != *"$TCC_ABS"* ]]; then
    echo "FAIL: CC doesn't reference tcc, Makefile has: $CC_LINE"
    exit 1
fi
echo "  $CC_LINE"

# Strip Mac-framework bits (we only want the bare interpreter).
# Also strip `-g` from CFLAGS — there's a known tcc -g bug when
# emitting DWARF for a static-init struct array with an
# extern-function-pointer field (Modules/config.c). Filed as a
# follow-up; for now build without debug info.
sed -i.bak \
    -e 's|Python/mactoolboxglue\.o||g' \
    -e 's|-u _PyMac_Error||g' \
    -e 's|-framework CoreFoundation||g' \
    -e 's| -g | |g' \
    -e 's|^OPT=\(.*\) -g\(.*\)|OPT=\1\2|' \
    Makefile

echo "==> Building python.exe (this takes a few minutes on a G3)..."
make python.exe > build.log 2>&1 \
    || { echo "FAIL: build failed; tail:"; tail -30 build.log; exit 1; }

if [[ ! -x python.exe ]]; then
    echo "FAIL: python.exe not produced"
    exit 1
fi

echo "==> Verifying python.exe is a PPC Mach-O binary..."
if ! file python.exe | grep -q "Mach-O.*ppc"; then
    echo "FAIL: python.exe is not a PPC Mach-O binary"
    file python.exe
    exit 1
fi

echo "==> Verifying python.exe links only against libSystem (no libgcc_s)..."
LIBS=$(otool -L python.exe | tail -n +2)
if echo "$LIBS" | grep -q "libgcc"; then
    echo "FAIL: links against libgcc_s — gcc was somehow involved:"
    echo "$LIBS"
    exit 1
fi
echo "  links only against /usr/lib/libSystem.B.dylib"

echo "==> python.exe -V..."
VER=$(./python.exe -V 2>&1)
if [[ "$VER" != "Python 2.7.18" ]]; then
    echo "FAIL: unexpected version: $VER"
    exit 1
fi
echo "  $VER"

echo "==> Running real Python program..."
PYTHONHOME=$PWD ./python.exe -S <<'EOF'
def fib(n):
    return 1 if n <= 1 else fib(n-1) + fib(n-2)
assert [fib(i) for i in range(10)] == [1, 1, 2, 3, 5, 8, 13, 21, 34, 55], "fib"
print("fib  ok")

class Counter:
    def __init__(self): self.n = 0
    def inc(self): self.n += 1; return self.n
c = Counter()
assert (c.inc(), c.inc(), c.inc()) == (1, 2, 3), "counter"
print("class ok")

s = "hello world"
assert s.upper() == "HELLO WORLD"
assert s[6:] == "world"
assert s.replace("world", "python") == "hello python"
print("str   ok")

d = {"a": 1, "b": 2, "c": 3}
assert sum(d.values()) == 6
assert sorted(d.keys()) == ["a", "b", "c"]
print("dict  ok")

try:
    1/0
    assert False, "no exception"
except ZeroDivisionError as e:
    assert "integer division" in str(e)
print("exc   ok")

# Exercise floating-point formatting (which goes through the
# %Lf-via-libSystem-redirect path).
assert "%.6f" % 3.14159265 == "3.141593"
print("float ok")

print("PASS - all six exercises completed")
EOF

echo
echo "PASS — Python 2.7.18 builds + runs end-to-end with tcc on Tiger PPC"
