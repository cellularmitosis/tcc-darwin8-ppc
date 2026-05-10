#!/bin/sh
# v0.2.47-g3 milestone — FP-arg GPR-shadow no longer clobbers a
# previously computed int arg.
#
# Run on imacg3 (or any Tiger 10.4.11 PowerPC G3/G4):
#
#     ./demos/v0.2.47-fp-arg-shadow.sh
#
# Expected last line:
#     All PASS
#
# What this demonstrates:
#   When a function call mixes an int arg whose evaluation goes
#   through helper-call side effects (e.g. `(x &= helper(...))`)
#   with a float/double arg, tcc's gfunc_call now correctly
#   spills the int arg before the FP arg's variadic GPR-shadow
#   lwz overwrites the same register. Pre-fix, the int arg
#   would silently turn into the float arg's bit pattern.
#
# Found via csmith --float seed 3228. Reduced to a small,
# self-contained case here.
set -e
cd "$(dirname "$0")/.."
TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure || true
)
SRC="demos/v0.2.47-fp-arg-shadow.c"
OUT=/tmp/v0.2.47-fp-arg-shadow
$TCC -B./tcc -L./tcc -I./tcc/include -w "$SRC" -o "$OUT"
"$OUT"
