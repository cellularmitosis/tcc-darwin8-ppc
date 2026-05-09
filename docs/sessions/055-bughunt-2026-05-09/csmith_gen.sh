#!/bin/bash
# Generate csmith programs locally on uranium, ship to imacg3.
# Args: $1 = first seed, $2 = last seed (inclusive)
set -e

FIRST=${1:-1}
LAST=${2:-100}
LOCAL=/tmp/csmith-tcc

mkdir -p "$LOCAL"

# Csmith options: keep moderately complex but compile-fast.
# --no-volatiles: their semantics are tricky and not high-yield for
#   PPC codegen issues.
# --no-packed-struct: tcc doesn't model packed-attribute layout
#   identically to gcc; legitimate divergence isn't a tcc bug.
# --max-funcs / --max-block-depth: keep compile time on a 700MHz G3
#   reasonable.
OPTS="--no-volatiles --no-packed-struct --max-funcs 6 --max-block-depth 3 --max-array-dim 2 --max-array-len-per-dim 5"

for s in $(seq $FIRST $LAST); do
    csmith $OPTS --seed $s > "$LOCAL/seed-$s.c" 2>/dev/null || echo "gen-fail $s"
done
echo "Generated seeds $FIRST..$LAST in $LOCAL"

# Ship in bulk.
~/bin/tiger-rsync.sh "$LOCAL/" imacg3:/Users/macuser/tmp/csmith/ 2>&1 | tail -3
