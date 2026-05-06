#!/bin/sh
# v0.2.28-clean-abitest.sh — show abitest-tcc passing all 24 tests
# end-to-end. Pre-v0.2.28, the upstream `abitest-tcc` stage tripped
# the long-deferred JIT heisenbug (variable failure count 5..19 of
# 24) plus, after that fix, a deterministic crash at
# `many_struct_test_3` from a save_regs() bug in `gfunc_call` that
# left the indirect-call function pointer in a register that arg
# setup then clobbered. v0.2.28 fixes both.

set -e

cd "$(dirname "$0")/.."

cd tcc/tests
rm -f abitest-tcc.exe abitest-cc.exe
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-tcc abitest-cc 2>&1
echo
echo "==> All abitest-tcc sub-tests passed."
