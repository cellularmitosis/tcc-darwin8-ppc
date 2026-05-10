#!/bin/bash
# creduce interestingness script for seed-8034 reduction.
# Reduces a csmith program by shipping current variant to imacg3,
# compiling with both gcc-4.0 and tcc, running both, comparing output.
# Exit 0 = "interesting" = bug still present (outputs differ).
# Exit non-zero = bug went away (or compile error / timeout / etc.).
#
# Targets imacg3 because ibookg38 is busy with the main builtins
# campaign; the imacg3 default-opts sweep is the lowest-priority
# arm and tolerates the slowdown.
#
# Run from the directory containing test.c (creduce's convention).
# Requires: ssh imacg3 with /Users/macuser/tmp/bswap_compat.c +
# /Users/macuser/tmp/csmith-runtime/ already in place.

set -u

HOST=imacg3
VARIANT_HOST_DIR=/Users/macuser/tmp/creduce-8034
SHIM=/Users/macuser/tmp/bswap_compat.c
RT=/Users/macuser/tmp/csmith-runtime
TCC_TREE=/Users/macuser/tcc-darwin8-ppc
TCC=$TCC_TREE/tcc/tcc
TCC_FLAGS="-B$TCC_TREE/tcc -L$TCC_TREE/tcc -I$TCC_TREE/tcc/include -I$RT -w"
GCC=/usr/bin/gcc-4.0
GCC_FLAGS="-O0 -w -I$RT"
RUN_TIMEOUT=10

# Ship the current variant.
scp -q test.c $HOST:$VARIANT_HOST_DIR/test.c || exit 1

# Compile + run both, on the remote.
ssh $HOST "
set -u
mkdir -p $VARIANT_HOST_DIR
cd $VARIANT_HOST_DIR

# Both must compile cleanly.
$GCC $GCC_FLAGS $SHIM test.c -o gcc.exe 2>/dev/null || exit 1
$TCC $TCC_FLAGS $SHIM test.c -o tcc.exe 2>/dev/null || exit 1

# Both must run within budget.
perl -e '\$SIG{ALRM}=sub{kill 9,-\$\$;exit 124}; alarm $RUN_TIMEOUT; exec @ARGV' ./gcc.exe > gcc.out 2>&1
g_exit=\$?
perl -e '\$SIG{ALRM}=sub{kill 9,-\$\$;exit 124}; alarm $RUN_TIMEOUT; exec @ARGV' ./tcc.exe > tcc.out 2>&1
t_exit=\$?

# Reject timeouts and crashes (signal exit codes 128+).
[ \$g_exit -lt 128 ] || exit 1
[ \$t_exit -lt 128 ] || exit 1

# Both must produce a checksum line (avoids reductions where output is empty).
grep -q '^checksum =' gcc.out || exit 1
grep -q '^checksum =' tcc.out || exit 1

# Bug is preserved iff checksums differ.
if diff -q gcc.out tcc.out > /dev/null 2>&1; then
    exit 1
else
    exit 0
fi
"
