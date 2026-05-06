#!/bin/sh
# v0.2.25-dylib.sh — build a tcc-produced dylib, dlopen it from
# a tcc-produced exe, prove the round trip works on Tiger PPC.
#
# Run on a real Tiger G3/G4. With no args, uses the in-tree tcc.

set -e

cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.25-dylib}

mkdir -p "$WORK"

# Step 1: dylib source — defines `greet` and `get_count`.
cat > "$WORK/libgreet.c" <<'EOF'
#include <stdio.h>
#include <string.h>

static int call_count = 0;

int greet(const char *who) {
    call_count++;
    printf("Hello, %s! (call #%d)\n", who, call_count);
    return strlen(who);
}

int get_count(void) {
    return call_count;
}
EOF

# Step 2: build libgreet.dylib via `tcc -shared`.
cd "$WORK"
"$OLDPWD/$TCC" -shared -o libgreet.dylib libgreet.c
file libgreet.dylib

# Step 3: build the dlopen test exe from demos/v0.2.25-dylib.c.
"$OLDPWD/$TCC" -o dlopen_greet "$OLDPWD/demos/v0.2.25-dylib.c"

# Step 4: run it (cwd = $WORK so dlopen finds ./libgreet.dylib).
./dlopen_greet
