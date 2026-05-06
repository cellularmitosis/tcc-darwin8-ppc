#!/bin/sh
# v0.2.17-g3 demo runner.
set -e
cd "$(dirname "$0")/.."
TCC=./tcc/tcc
[ -x "$TCC" ] || { echo "ERROR: $TCC not found. Run scripts/build-tiger.sh"; exit 1; }
"$TCC" -B./tcc -o /tmp/v0217-alloca demos/v0.2.17-alloca.c
/tmp/v0217-alloca
echo "exit=$?"
