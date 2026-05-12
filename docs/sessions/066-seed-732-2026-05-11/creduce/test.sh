#!/bin/bash
# Test predicate for creduce: returns 0 (success) iff the reduced
# program both
#   (a) compiles + runs cleanly under both gcc-4.0 and post-063 tcc
#       on imacg3,
#   (b) gcc and tcc disagree on the value of g_359.f0 after func_1()
#       (or, equivalently, produce different per-global checksum
#       streams that diverge specifically at g_359.f0).
#
# The reduced program must keep:
#   - a `union U0 { int8_t f0; }`
#   - a static `union U0 g_359` initialized to a non-zero byte
#   - a call to a function that takes union U0 by value as a NON-LAST
#     argument
#   - a write to g_359.f0 via pointer alias `*l_505 |= ...`
#
# The simplest reproducer keeps g_359 visible, calls func_8 which
# does `*l_505 |= g_26.f0`, and main prints g_359.f0. We test
# "tcc gives wrong g_359.f0 vs gcc" by running the binary and
# checking the printed output.
#
# This script is invoked by creduce from inside a temporary dir
# containing the current candidate `test.c`. It must be deterministic
# and quick.
set -u

TIMEOUT_S=20
HOST="imacg3"
REMOTE_DIR="/Users/macuser/tmp/cr-732"
TCC="/Users/macuser/tcc-darwin8-ppc/tcc/tcc"
GCC="/usr/bin/gcc-4.0"
INC="/Users/macuser/tmp/csmith-runtime"

# Quick syntax sanity locally with cpp (csmith.h must resolve).
# We rely on csmith.h being present on the remote, not locally.

# Ship + build + run on imacg3.
ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "mkdir -p $REMOTE_DIR" \
  >/dev/null 2>&1 || exit 1

scp -B -o ConnectTimeout=10 test.c "$HOST:$REMOTE_DIR/test.c" \
  >/dev/null 2>&1 || exit 1

ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "
cd $REMOTE_DIR
ulimit -t $TIMEOUT_S

# Compile with gcc-4.0.
$GCC -O0 -w -I$INC test.c -o test.gcc 2>/dev/null || exit 10

# Compile with tcc.
$TCC -B/Users/macuser/tcc-darwin8-ppc/tcc -I/Users/macuser/tcc-darwin8-ppc/tcc/include -I$INC test.c -o test.tcc 2>/dev/null || exit 11

# Run both with arg '1' so transparent_crc prints per-global hashes.
./test.gcc 1 > test.gcc.out 2>/dev/null
gcc_exit=\$?
./test.tcc 1 > test.tcc.out 2>/dev/null
tcc_exit=\$?

# Both must exit successfully (no segv, no nonzero exit).
[ \$gcc_exit -eq 0 ] || exit 12
[ \$tcc_exit -eq 0 ] || exit 13

# Both outputs must end with a 'checksum = ...' line (so the program
# completed the checksum loop).
grep -q '^checksum = ' test.gcc.out || exit 14
grep -q '^checksum = ' test.tcc.out || exit 15

# We require they DISAGREE, specifically at g_359.f0 (or its later
# rollover into 'checksum = '). If the outputs match, reject — the
# bug is not preserved.
g_hash=\$(grep 'g_359.f0' test.gcc.out | head -1)
t_hash=\$(grep 'g_359.f0' test.tcc.out | head -1)
# If the source still has a g_359.f0 transparent_crc call, both should
# print a hash for it. If it's been stripped out, fall back to the
# final checksum.
if [ -n \"\$g_hash\" ] && [ -n \"\$t_hash\" ]; then
  [ \"\$g_hash\" != \"\$t_hash\" ] || exit 16
else
  g_final=\$(grep '^checksum = ' test.gcc.out)
  t_final=\$(grep '^checksum = ' test.tcc.out)
  [ \"\$g_final\" != \"\$t_final\" ] || exit 17
fi
exit 0
" >/dev/null 2>&1
exit $?
