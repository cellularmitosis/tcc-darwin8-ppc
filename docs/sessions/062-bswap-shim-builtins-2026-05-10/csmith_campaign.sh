#!/opt/tigersh-deps-0.1/bin/bash
# Csmith differential testing campaign on imacg3 / ibookg38.
# Args: $1 = first seed, $2 = last seed (inclusive)
#       $3 = SRC dir (default /Users/macuser/tmp/csmith)
#       $4 = OUT dir (default /Users/macuser/tmp/csmith-out)
# Env:  EXTRA_CC_SRC = optional extra .c file (e.g. bswap_compat.c)
#                      compiled into both the gcc and tcc binary.
#                      Used to provide gcc-4.0 with __builtin_bswap32 etc.
#       EXTRA_CC_HDR = optional header injected via -include into both
#                      compilers (e.g. builtin_compat.h, which UB-guards
#                      __builtin_clz/ctz so tcc's libtcc1.a software impl
#                      doesn't false-positive vs gcc-PPC's cntlzw).
# Reads .c files from $SRC/seed-N.c
# Writes results to $OUT/seed-N.{gcc,tcc}.{exit,out}
# Summary: $OUT/SUMMARY.txt
set -u

FIRST=${1:-1}
LAST=${2:-100}
SRC=${3:-/Users/macuser/tmp/csmith}
OUT=${4:-/Users/macuser/tmp/csmith-out}
RT=/Users/macuser/tmp/csmith-runtime
TCC_TREE=/Users/macuser/tcc-darwin8-ppc
TCC=$TCC_TREE/tcc/tcc
TCC_FLAGS="-B$TCC_TREE/tcc -L$TCC_TREE/tcc -I$TCC_TREE/tcc/include -I$RT -w"
GCC=/usr/bin/gcc-4.0
GCC_FLAGS="-O0 -w -I$RT"
RUN_TIMEOUT=15  # seconds; csmith programs sometimes infinite-loop
EXTRA_CC_SRC=${EXTRA_CC_SRC:-}
EXTRA_CC_HDR=${EXTRA_CC_HDR:-}
INCLUDE_FLAG=
[ -n "$EXTRA_CC_HDR" ] && INCLUDE_FLAG="-include $EXTRA_CC_HDR"

mkdir -p "$OUT"
SUMMARY="$OUT/SUMMARY.txt"
> "$SUMMARY"

PASS=0; FAIL=0; SKIP=0

# Tiger ships no `timeout(1)`; perl alarm is the smallest portable
# wrapper. ~7s gcc compile + ~1s tcc compile + ~few-s run.
run_with_timeout() {
    local timeout=$1; shift
    perl -e '
        $SIG{ALRM} = sub { kill 9, -$$; exit 124 };
        alarm shift @ARGV;
        exec @ARGV;
    ' "$timeout" "$@"
}

seed=$((FIRST-1))
while [ $seed -lt $LAST ]; do
    seed=$((seed+1))
    src="$SRC/seed-$seed.c"
    [ -f "$src" ] || { echo "SKIP $seed (no src)" >> "$SUMMARY"; SKIP=$((SKIP+1)); continue; }

    # gcc-4.0 reference
    g_exe="$OUT/seed-$seed.gcc"
    g_out="$OUT/seed-$seed.gcc.out"
    g_err="$OUT/seed-$seed.gcc.err"
    if ! $GCC $GCC_FLAGS $INCLUDE_FLAG $EXTRA_CC_SRC "$src" -o "$g_exe" 2>"$g_err"; then
        echo "SKIP $seed (gcc-compile-fail)" >> "$SUMMARY"
        SKIP=$((SKIP+1))
        continue
    fi
    run_with_timeout $RUN_TIMEOUT "$g_exe" > "$g_out" 2>&1
    g_exit=$?
    # Tiger's perl-alarm timeout doesn't intercept the alarm cleanly:
    # the exec'd program inherits the alarm and is killed by SIGALRM,
    # giving exit 142 (128+14). Treat 142 as timeout, plus 124 in case
    # a future perl version returns the explicit timeout exit code.
    if [ $g_exit -eq 124 ] || [ $g_exit -eq 142 ]; then
        echo "SKIP $seed (gcc-timeout)" >> "$SUMMARY"
        SKIP=$((SKIP+1))
        continue
    fi

    # tcc
    t_exe="$OUT/seed-$seed.tcc"
    t_out="$OUT/seed-$seed.tcc.out"
    t_err="$OUT/seed-$seed.tcc.err"
    if ! $TCC $TCC_FLAGS $INCLUDE_FLAG $EXTRA_CC_SRC "$src" -o "$t_exe" 2>"$t_err"; then
        echo "FAIL $seed (tcc-compile-fail)" >> "$SUMMARY"
        FAIL=$((FAIL+1))
        continue
    fi
    run_with_timeout $RUN_TIMEOUT "$t_exe" > "$t_out" 2>&1
    t_exit=$?

    if [ $t_exit -eq 124 ] || [ $t_exit -eq 142 ]; then
        echo "FAIL $seed (tcc-timeout)" >> "$SUMMARY"
        FAIL=$((FAIL+1))
        continue
    fi

    if [ "$g_exit" != "$t_exit" ]; then
        echo "FAIL $seed (exit: gcc=$g_exit tcc=$t_exit)" >> "$SUMMARY"
        FAIL=$((FAIL+1))
        continue
    fi

    if ! diff -q "$g_out" "$t_out" > /dev/null 2>&1; then
        echo "FAIL $seed (output-diff)" >> "$SUMMARY"
        FAIL=$((FAIL+1))
        continue
    fi

    echo "PASS $seed" >> "$SUMMARY"
    PASS=$((PASS+1))
    # Clean up to save space.
    rm -f "$g_exe" "$t_exe" "$g_out" "$t_out" "$g_err" "$t_err"
done

echo "===" >> "$SUMMARY"
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP" >> "$SUMMARY"
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
