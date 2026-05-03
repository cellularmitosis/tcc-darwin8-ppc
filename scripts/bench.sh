#!/bin/sh
# bench.sh — compile-time benchmark for tcc-darwin8-ppc.
#
# Compiles lua 5.4.7 (~30 source files) repeatedly with each
# compiler, runs each twice, throws away the first result, and
# reports wall-clock seconds for the cold + warm + warm runs.
#
# Compilers measured:
#   tcc          (this repo's binary)
#   gcc-4.0 -O0  (Tiger system gcc, no optimization)
#   gcc-4.0 -Os  (Tiger system gcc, size opt)
#
# Why two runs and discard first: the first compile primes the
# disk cache (source + headers) and warms up the dynamic linker
# state. The second run is the cleaner steady-state number and
# is what we report.
#
# Run on ibookg37 / imacg3 / similar Tiger PPC host:
#
#     ./scripts/bench.sh
#
# Optional env:
#   LUA_TARBALL  — path to lua-5.4.7.tar.gz (default: /tmp/lua-5.4.7.tar.gz).
#                  Fetched via curl from www.lua.org if missing.
#   TCC          — tcc binary to use (default: ./tcc/tcc).

set -e
cd "$(dirname "$0")/.."

LUA_VER=5.4.7
LUA_TARBALL=${LUA_TARBALL:-/tmp/lua-${LUA_VER}.tar.gz}
LUA_SRC=/tmp/lua-${LUA_VER}-bench
TCC=${TCC:-./tcc/tcc}
TCC_B=$(dirname "$TCC")
GCC=/usr/bin/gcc-4.0

# Check tooling.
if [ ! -x "$TCC" ]; then
    echo "error: tcc binary not at $TCC. Build first or set TCC=..." >&2
    exit 1
fi
if [ ! -x "$GCC" ]; then
    echo "error: $GCC not present (this is the Tiger system gcc -- expected on Tiger PPC host)" >&2
    exit 1
fi

# Stage lua sources fresh under $LUA_SRC.
if [ ! -f "$LUA_TARBALL" ]; then
    echo "==> fetching $LUA_TARBALL"
    curl -sLo "$LUA_TARBALL" "https://www.lua.org/ftp/lua-${LUA_VER}.tar.gz"
fi
rm -rf "$LUA_SRC"
mkdir -p "$LUA_SRC"
tar -C "$LUA_SRC" -xzf "$LUA_TARBALL"
LUA_C_DIR=$LUA_SRC/lua-${LUA_VER}/src
ls "$LUA_C_DIR"/*.c >/dev/null

# Pick the .c files we'd compile to make the standalone interpreter:
# everything except the alternate driver files.
CFILES=$(cd "$LUA_C_DIR" && ls *.c | grep -v -E '^(luac|onelua|ltests)\.c$')
N=$(echo "$CFILES" | wc -l | tr -d ' ')

echo "==> benchmarking compile-time of $N lua $LUA_VER source files"
echo "    source dir: $LUA_C_DIR"
echo

# bench_one COMPILER ARGS DESCRIPTION
# Compiles all of $CFILES with the given compiler + args, twice.
# Reports the second wall-clock time (steady state).
bench_one() {
    desc=$1
    cmd=$2
    extra=$3

    # Sanity: smoke-compile one file first to surface any setup
    # issues outside the timed runs.
    if ! eval "$cmd $extra -c $LUA_C_DIR/lapi.c -o /dev/null" 2>/dev/null; then
        echo "  $desc: SKIP (smoke compile failed)"
        return
    fi

    # Two runs; only the second is reported.
    for run in 1 2; do
        rm -f "$LUA_C_DIR"/*.o
        START=$(date +%s)
        for f in $CFILES; do
            eval "$cmd $extra -c $LUA_C_DIR/$f -o $LUA_C_DIR/${f%.c}.o" \
                >/dev/null 2>&1 || {
                    echo "  $desc: FAIL on $f"
                    return
                }
        done
        END=$(date +%s)
        ELAPSED=$((END - START))
        if [ "$run" = "2" ]; then
            printf "  %-22s  %4d s  (%d files / %d s)\n" \
                "$desc" "$ELAPSED" "$N" "$ELAPSED"
        fi
    done
}

bench_one "tcc"          "$TCC -B$TCC_B -DLUA_USE_MACOSX" ""
bench_one "gcc-4.0 -O0"  "$GCC -arch ppc -DLUA_USE_MACOSX" "-O0 -w"
bench_one "gcc-4.0 -Os"  "$GCC -arch ppc -DLUA_USE_MACOSX" "-Os -w"

# Cleanup so we don't leave the staging dir around.
rm -rf "$LUA_SRC"

echo
echo "Done. Steady-state second-run wall-clock seconds reported."
