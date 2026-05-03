#!/bin/sh
# v0.2.12-g3 milestone — build & run real-world lua 5.4.7 with tcc.
#
# Run on ibookg37 (or any Tiger 10.4.11 PowerPC):
#
#     ./demos/v0.2.12-lua.sh
#
# Expected last lines:
#     fib(20) = 6765
#     done
#     exit=0
#
# Why this matters: this is the first non-trivial third-party
# program to build and run end-to-end with tcc-darwin8-ppc. Lua's
# 30+ source files exercise nearly every codegen path (structs by
# value, function pointers, inline functions, computed goto,
# longjmp/setjmp, varargs, string formatting, FP, doubles, the
# whole standard library) — and they all work.
#
# What v0.2.12 added (vs v0.2.11):
#   - struct-pass-by-value-from-deref (`use(*p)` where *p is a
#     struct) now compiles. Previously errored "deref of basic
#     type 0x7 not yet supported".
#   - Cross-TU PIC reloc translation: the Mach-O reader now
#     handles scattered HA16_SECTDIFF / LO16_SECTDIFF relocs.
#     Without this, multi-TU programs that reference extern data
#     symbols (__sF/stderr, env vars, ...) linked successfully
#     but crashed at runtime — the second TU's PIC pairs read
#     from random __TEXT addresses.
#
# Without these two fixes, lua compiled but crashed on first
# stderr write at startup.
#
# Source: https://www.lua.org/ftp/lua-5.4.7.tar.gz (~360KB)
#
# Limitations:
#   - sqlite3 amalgamation also builds and `./sqlite3 -version`
#     works, but `select 1+1` still hits a separate codegen bug
#     under investigation. See docs/sessions/040-pickup-2026-05-03.

set -e
cd "$(dirname "$0")/.."

LUA_VER=5.4.7
LUA_SRC=/tmp/lua-${LUA_VER}-src

# 1. Fetch lua sources if not already present.
if [ ! -d "$LUA_SRC" ]; then
    echo "==> fetching lua-${LUA_VER}.tar.gz"
    cd /tmp
    if [ ! -f "lua-${LUA_VER}.tar.gz" ]; then
        # If on a host without modern TLS, get the tarball some other
        # way and stage it at /tmp/lua-${LUA_VER}.tar.gz first.
        curl -sLO "https://www.lua.org/ftp/lua-${LUA_VER}.tar.gz"
    fi
    tar xzf "lua-${LUA_VER}.tar.gz"
    mv "lua-${LUA_VER}/src" "$LUA_SRC"
    rm -rf "lua-${LUA_VER}"
    cd - >/dev/null
fi

# 2. Compile every lua module with tcc. Skip the alternate driver
#    files (lua.c is the standalone interpreter; luac.c is the
#    bytecode compiler; we want lua here).
echo "==> compiling lua modules with tcc"
cd "$LUA_SRC"
TCC=$OLDPWD/tcc/tcc
B=$OLDPWD/tcc
for f in $(ls *.c | grep -v -E '^(luac|onelua|ltests)\.c$'); do
    out="${f%.c}.o"
    [ "$f" = "lua.c" ] && continue   # link lua.c last
    "$TCC" -B"$B" -DLUA_USE_MACOSX -c "$f" -o "$out" >/dev/null 2>&1
done

# 3. Link the lua interpreter.
echo "==> linking lua"
"$TCC" -B"$B" -DLUA_USE_MACOSX -o lua lua.c \
    $(ls *.o | grep -v -E '^(luac|onelua|ltests)\.o$')

# 4. Run a small Lua program to prove it actually works.
echo "==> running"
file ./lua
./lua -v
cat > /tmp/lua_demo.lua <<'EOF'
print("== arithmetic ==")
print(1 + 1, 7 * 6, 100 / 3)
print("== strings ==")
local s = "hello, world"
print(s:upper(), #s)
print("== closures ==")
local function counter()
    local n = 0
    return function() n = n + 1; return n end
end
local c = counter()
print(c(), c(), c())
print("== fibonacci ==")
local function fib(n)
    if n < 2 then return n end
    return fib(n-1) + fib(n-2)
end
print("fib(20) =", fib(20))
print("done")
EOF
./lua /tmp/lua_demo.lua
echo "exit=$?"
