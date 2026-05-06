#!/bin/sh
# v0.2.26-link-dylib.sh — link-time dylib usage on Tiger PPC.
#
# Builds a tiny exe that calls into Tiger's bundled libz at link
# time (no dlopen/dlsym). Proves tcc parses the dylib's LC_SYMTAB,
# emits LC_LOAD_DYLIB for libz.1.dylib, and dyld resolves
# `_zlibVersion` / `_adler32` at runtime against libz.

set -e

cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.26-link-dylib}

mkdir -p "$WORK"

cat > "$WORK/test.c" <<'EOF'
#include <stdio.h>
#include <zlib.h>

int main(void) {
    printf("zlib version: %s\n", zlibVersion());

    uLong a = adler32(0L, NULL, 0);
    a = adler32(a, (Bytef *)"hello tiger ppc dylib", 21);
    printf("adler32(\"hello tiger ppc dylib\") = 0x%lx\n", a);
    return 0;
}
EOF

cd "$WORK"
"$OLDPWD/$TCC" -lz -o test test.c

echo "==> linkage:"
otool -L test

echo "==> run:"
./test
