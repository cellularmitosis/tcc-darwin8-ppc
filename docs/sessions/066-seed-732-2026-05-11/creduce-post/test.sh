#!/bin/bash
# 3-way creduce predicate for the r12 reg-allocator clobber.
#
# A reduced test.c is "interesting" iff:
#   (a) gcc-4.0 -O0 compiles and runs cleanly,
#   (b) the PRE-fix tcc compiles and runs cleanly but DISAGREES with
#       gcc's output (preserves the bug),
#   (c) the POST-fix tcc compiles and runs cleanly AND AGREES with
#       gcc (rules out UB-class reductions that just happen to make
#       any tcc disagree).
#
# Both checks run on ibookg37. The reduced program must:
#   - terminate within ${TIMEOUT_S} seconds under each binary,
#   - print at least one line to stdout (so we have something to
#     diff — empty-output is rejected to keep creduce honest).

set -u

TIMEOUT_S=15
HOST="ibookg37"
REMOTE_DIR="/Users/macuser/tmp/cr-732-post"
PRE_TCC="/tmp/tcc-prefix"
PRE_B="/tmp/tccdir-prefix"
POST_TCC="/Users/macuser/tcc-darwin8-ppc/tcc/tcc"
POST_B="/Users/macuser/tcc-darwin8-ppc/tcc"
INC="/Users/macuser/tmp/csmith"

ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "mkdir -p $REMOTE_DIR" \
  >/dev/null 2>&1 || exit 1

scp -B -o ConnectTimeout=10 test.c "$HOST:$REMOTE_DIR/test.c" \
  >/dev/null 2>&1 || exit 1

ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "
cd $REMOTE_DIR
ulimit -t $TIMEOUT_S

# (a) gcc-4.0 -O0
/usr/bin/gcc-4.0 -O0 -w -I$INC test.c -o test.gcc 2>/dev/null || exit 10
./test.gcc 1 > test.gcc.out 2>/dev/null || exit 11
[ -s test.gcc.out ] || exit 12

# (b) PRE-fix tcc must compile + run but DISAGREE with gcc
$PRE_TCC -B$PRE_B -I$PRE_B/include -I$INC test.c -o test.pre 2>/dev/null || exit 20
./test.pre 1 > test.pre.out 2>/dev/null || exit 21
diff -q test.gcc.out test.pre.out >/dev/null 2>&1 && exit 22

# (c) POST-fix tcc must compile + run AND AGREE with gcc
$POST_TCC -B$POST_B -I$POST_B/include -I$INC test.c -o test.post 2>/dev/null || exit 30
./test.post 1 > test.post.out 2>/dev/null || exit 31
diff -q test.gcc.out test.post.out >/dev/null 2>&1 || exit 32

exit 0
" >/dev/null 2>&1
exit $?
