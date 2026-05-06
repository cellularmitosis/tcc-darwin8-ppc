#!/bin/sh
# v0.2.27-jit-heisenbug.sh — exercise the JIT loop fix.
#
# Builds the demo via tcc (so the test program itself uses the
# tcc-compiled __clear_cache that delegates to
# sys_icache_invalidate). Runs the loop 30 times, each iteration
# JIT-compiling a struct-returning function and calling it.
#
# Pre-v0.2.27: crashes at a random iteration with SIGILL/SIGBUS/
# SIGSEGV or returns wrong values.
# v0.2.27+: 30 iterations OK every run.

set -e

cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}

"$TCC" -B./tcc -I./tcc/include -I./tcc \
    -o /tmp/v0227-jit-heisenbug \
    demos/v0.2.27-jit-heisenbug.c \
    ./tcc/libtcc.c -lpthread -ldl -lm

echo "==> running 30-iteration JIT loop..."
/tmp/v0227-jit-heisenbug
