#!/opt/tigersh-deps-0.1/bin/bash
# Run lua's official testes/ test suite against tcc-built lua, then
# also against gcc-4.0-built lua, and diff the two outputs.
set -e -o pipefail

LUA_VER=5.4.7
WORK=/Users/macuser/tmp/lua-test
TCC_TREE=/Users/macuser/tcc-darwin8-ppc
TCC=$TCC_TREE/tcc/tcc
TCC_FLAGS="-B$TCC_TREE/tcc -L$TCC_TREE/tcc -I$TCC_TREE/tcc/include -DLUA_USE_POSIX -DLUA_USE_DLOPEN"

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f "lua-${LUA_VER}.tar.gz" ]; then
    echo "==> fetch lua-${LUA_VER}.tar.gz"
    /opt/tigersh-deps-0.1/bin/curl -fsSL \
        --cacert /Users/macuser/tmp/cacert-2026-03-19.pem \
        -o "lua-${LUA_VER}.tar.gz" \
        "https://www.lua.org/ftp/lua-${LUA_VER}.tar.gz"
fi

if [ ! -d "lua-${LUA_VER}" ]; then
    tar xzf "lua-${LUA_VER}.tar.gz"
fi

cd "lua-${LUA_VER}"

# The lua test suite ships separately at lua.org/tests.
if [ ! -d testes ]; then
    echo "==> fetch lua-${LUA_VER}-tests.tar.gz"
    /opt/tigersh-deps-0.1/bin/curl -fsSL \
        --cacert /Users/macuser/tmp/cacert-2026-03-19.pem \
        -o "lua-${LUA_VER}-tests.tar.gz" \
        "https://www.lua.org/tests/lua-${LUA_VER}-tests.tar.gz"
    tar xzf "lua-${LUA_VER}-tests.tar.gz"
    # The expanded dir is "lua-${LUA_VER}-tests"; rename to testes.
    mv "lua-${LUA_VER}-tests" testes
fi

echo "==> build lua with tcc (skip luac.c — has its own main)"
cd src
rm -f *.o
for f in *.c; do
    [ "$f" = "luac.c" ] && continue
    $TCC $TCC_FLAGS -c "$f"
done
$TCC $TCC_FLAGS *.o -o ../lua.tcc -lm -ldl
cd ..

echo "==> build lua with gcc-4.0 (reference)"
cd src
rm -f *.o
for f in *.c; do
    [ "$f" = "luac.c" ] && continue
    gcc-4.0 -O0 -DLUA_USE_POSIX -DLUA_USE_DLOPEN -c "$f"
done
gcc-4.0 -O0 *.o -o ../lua.gcc -lm -ldl
cd ..

# Build the testes companion .so libs (one of the tests `require`s
# them via dlopen). Use gcc-4.0 since the makefile expects gcc; both
# lua interpreters then load the SAME .so binaries via dlopen, so
# any test divergence is a real lua-codegen difference.
echo "==> build testes/libs/*.so with gcc-4.0"
cd testes/libs
gcc-4.0 -O0 -I../.. -dynamiclib -bundle -undefined dynamic_lookup -o lib1.so lib1.c    || true
gcc-4.0 -O0 -I../.. -dynamiclib -bundle -undefined dynamic_lookup -o lib11.so lib11.c  || true
gcc-4.0 -O0 -I../.. -dynamiclib -bundle -undefined dynamic_lookup -o lib2.so lib2.c    || true
gcc-4.0 -O0 -I../.. -dynamiclib -bundle -undefined dynamic_lookup -o lib21.so lib21.c  || true
gcc-4.0 -O0 -I../.. -dynamiclib -bundle -undefined dynamic_lookup -o lib2-v2.so lib22.c || true
cd ../..

# Run a subset of the test suite. Some tests (main.lua, api.lua) are
# tightly coupled to the lua source path and dlopen behavior; we skip
# those for now and run the language tests that exercise tcc-emitted
# lua-runtime code paths.
TESTS="bitwise.lua calls.lua closure.lua code.lua constructs.lua coroutine.lua errors.lua events.lua files.lua gc.lua goto.lua literals.lua locals.lua math.lua nextvar.lua pm.lua sort.lua strings.lua tpack.lua tracegc.lua vararg.lua verybig.lua"

# Run a single lua test under perl-alarm timeout so we don't get
# blocked by infinite loops in tests. perl exec replaces itself with
# the lua interpreter so the alarm signal reaches lua directly.
run_lua_test() {
    local lua=$1; local test=$2; local out=$3; local exit_f=$4
    local timeout=180
    perl -e '
        $SIG{ALRM} = sub { kill 9, $$; exit 137 };
        alarm shift @ARGV;
        exec @ARGV;
    ' $timeout "$lua" "$test" > "$out" 2>&1
    echo "$?" > "$exit_f"
}

set +e   # tests are allowed to fail / time out without ending the script

echo "==> run lua tests with tcc-built lua"
cd testes
for t in $TESTS; do
    [ -f "$t" ] || continue
    run_lua_test ../lua.tcc "$t" "../testes.tcc.${t%.lua}.out" "../testes.tcc.${t%.lua}.exit"
    echo "  ran (tcc) $t exit=$(cat ../testes.tcc.${t%.lua}.exit)"
done
cd ..

echo "==> run lua tests with gcc-built lua"
cd testes
for t in $TESTS; do
    [ -f "$t" ] || continue
    run_lua_test ../lua.gcc "$t" "../testes.gcc.${t%.lua}.out" "../testes.gcc.${t%.lua}.exit"
    echo "  ran (gcc) $t exit=$(cat ../testes.gcc.${t%.lua}.exit)"
done
cd ..

set -e

echo "==> compare"
PASS=0; FAIL=0; SKIP=0
for t in $TESTS; do
    base="${t%.lua}"
    g_out="testes.gcc.${base}.out"
    t_out="testes.tcc.${base}.out"
    g_exit_f="testes.gcc.${base}.exit"
    t_exit_f="testes.tcc.${base}.exit"
    [ -f "$g_out" ] || { SKIP=$((SKIP+1)); echo "SKIP $t (no gcc out)"; continue; }
    [ -f "$t_out" ] || { SKIP=$((SKIP+1)); echo "SKIP $t (no tcc out)"; continue; }
    g_exit=$(cat "$g_exit_f" 2>/dev/null)
    t_exit=$(cat "$t_exit_f" 2>/dev/null)
    if [ "$g_exit" != "0" ]; then
        SKIP=$((SKIP+1)); echo "SKIP $t (gcc exit=$g_exit; test infra issue not tcc)"; continue
    fi
    if [ "$t_exit" != "$g_exit" ]; then
        FAIL=$((FAIL+1)); echo "FAIL $t (exit: gcc=$g_exit tcc=$t_exit)"; continue
    fi
    if ! diff -q "$g_out" "$t_out" > /dev/null 2>&1; then
        FAIL=$((FAIL+1)); echo "FAIL $t (output diff)"; continue
    fi
    PASS=$((PASS+1)); echo "PASS $t"
done
echo "==="
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
echo "==> DONE"
