#!/bin/sh
# v0.2.21-g3 demo runner: cJSON 1.7.18.
set -e
cd "$(dirname "$0")/.."

CJSON_VER=1.7.18
CJSON_SRC=/tmp/cJSON-${CJSON_VER}

if [ ! -d "$CJSON_SRC" ]; then
    echo "==> fetching cJSON-${CJSON_VER}.tar.gz"
    cd /tmp
    if [ ! -f "cjson-${CJSON_VER}.tar.gz" ]; then
        curl -sL "https://github.com/DaveGamble/cJSON/archive/refs/tags/v${CJSON_VER}.tar.gz" \
            -o "cjson-${CJSON_VER}.tar.gz" \
            || { echo "Need a tarball at /tmp/cjson-${CJSON_VER}.tar.gz"; exit 1; }
    fi
    tar xzf "cjson-${CJSON_VER}.tar.gz"
    cd - >/dev/null
fi

TCC=./tcc/tcc
B=./tcc

echo "==> building cJSON demo"
"$TCC" -B"$B" -I "$CJSON_SRC" -o /tmp/jdemo \
    "$CJSON_SRC/cJSON.c" demos/v0.2.21-cjson.c -lm

echo "==> running"
/tmp/jdemo
echo "exit=$?"
