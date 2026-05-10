#!/bin/bash
# creduce interestingness script for seed-1705 reduction.
# Predicate: tcc and gcc both compile and run cleanly, but the
# `g_142` per-variable CRC32 hash differs.
# Exit 0 = "interesting" = bug still present.
# Exit non-zero = bug went away.
#
# Targets ibookg38 (faster host).

set -u

HOST=ibookg38
VARIANT_HOST_DIR=/Users/macuser/tmp/creduce-1705
RT=/Users/macuser/tmp/csmith-runtime
TCC_TREE=/Users/macuser/tcc-darwin8-ppc
TCC=$TCC_TREE/tcc/tcc
TCC_FLAGS="-B$TCC_TREE/tcc -L$TCC_TREE/tcc -I$TCC_TREE/tcc/include -I$RT -w"
GCC=/usr/bin/gcc-4.0
GCC_FLAGS="-O0 -w -I$RT"
RUN_TIMEOUT=10

scp -q test.c $HOST:$VARIANT_HOST_DIR/test.c || exit 1

ssh $HOST "
set -u
mkdir -p $VARIANT_HOST_DIR
cd $VARIANT_HOST_DIR

# Both must compile cleanly.
$GCC $GCC_FLAGS test.c -o gcc.exe 2>/dev/null || exit 1
$TCC $TCC_FLAGS test.c -o tcc.exe 2>/dev/null || exit 1

# Both must run cleanly within budget. Arg '1' triggers per-variable
# CRC32 dump.
perl -e '\$SIG{ALRM}=sub{kill 9,-\$\$;exit 124}; alarm $RUN_TIMEOUT; exec @ARGV' ./gcc.exe 1 > gcc.out 2>&1
[ \$? -eq 0 ] || exit 1
perl -e '\$SIG{ALRM}=sub{kill 9,-\$\$;exit 124}; alarm $RUN_TIMEOUT; exec @ARGV' ./tcc.exe 1 > tcc.out 2>&1
[ \$? -eq 0 ] || exit 1

# Both outputs must contain the g_142 hash line. (After reduction
# shrinks the program this line may disappear; reject when it does.)
g_g142=\$(grep 'hashing g_142 :' gcc.out)
t_g142=\$(grep 'hashing g_142 :' tcc.out)
[ -n \"\$g_g142\" ] || exit 1
[ -n \"\$t_g142\" ] || exit 1

# Outputs must differ.
[ \"\$g_g142\" != \"\$t_g142\" ] && exit 0

exit 1
"
