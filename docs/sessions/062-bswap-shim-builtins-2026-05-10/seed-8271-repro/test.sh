#!/bin/bash
# creduce interestingness script for seed-1536 reduction.
# Predicate: tcc-built binary SEGVs (or fails some other way) but
# gcc-built binary runs cleanly (exit 0).
# Exit 0 = "interesting" = bug still present.
# Exit non-zero = bug went away.
#
# Targets ibookg38 (the faster host); the main builtins campaign
# should be done by the time this is launched.

set -u

HOST=ibookg38
VARIANT_HOST_DIR=/Users/macuser/tmp/creduce-8271
SHIM=/Users/macuser/tmp/bswap_compat.c
INC=/Users/macuser/tmp/builtin_compat.h
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

# Both must compile cleanly. Builtins arm needs the shim + UB-guard.
$GCC $GCC_FLAGS -include $INC $SHIM test.c -o gcc.exe 2>/dev/null || exit 1
$TCC $TCC_FLAGS -include $INC $SHIM test.c -o tcc.exe 2>/dev/null || exit 1

# gcc must run cleanly within budget.
perl -e '\$SIG{ALRM}=sub{kill 9,-\$\$;exit 124}; alarm $RUN_TIMEOUT; exec @ARGV' ./gcc.exe > gcc.out 2>&1
g_exit=\$?

# Reject gcc timeouts and crashes.
[ \$g_exit -eq 0 ] || exit 1

# tcc must produce a checksum (proves the program isn't trivially
# emitting nothing). After reduction shrinks the program, this guard
# may be too strict — relax to 'gcc.out non-empty' once we hit walls.
grep -q '^checksum =' gcc.out || exit 1

# tcc must crash (signal exit code 128+).
perl -e '\$SIG{ALRM}=sub{kill 9,-\$\$;exit 124}; alarm $RUN_TIMEOUT; exec @ARGV' ./tcc.exe > tcc.out 2>&1
t_exit=\$?

# 139 = SIGSEGV (128 + 11). Accept any signal kill (>= 128) since
# reduction may shift the symptom from SIGSEGV to SIGBUS or SIGILL.
[ \$t_exit -ge 128 ] && [ \$t_exit -ne 124 ] && exit 0

exit 1
"
