#!/bin/sh
# v0.2.28-sqlite-dylib.sh — build sqlite3 (the entire 1.9MB
# amalgamation) as a tcc-produced dylib, then dlopen it from a
# tcc-produced exe and exercise sqlite3_open / exec / close.
#
# Real-world validation of the dylib output path: sqlite3.c is
# ~250K lines, exercises a wide swath of codegen, and the
# resulting dylib is loaded by dyld via dlopen.

set -e

cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
SQLDIR=${SQLDIR:-/Users/macuser/tmp/sqlite-amalgamation-3460100}

if [ ! -f "$SQLDIR/sqlite3.c" ]; then
    echo "skipping: SQLDIR=$SQLDIR/sqlite3.c not found"
    exit 0
fi

WORK=${WORK:-/tmp/v0.2.28-sqlite-dylib}
mkdir -p "$WORK"

echo "==> step 1/3: build sqlite3 amalgamation as a dylib"
"$OLDPWD/$TCC" -B"$OLDPWD/tcc" -I"$OLDPWD/tcc/include" \
    -shared -o "$WORK/libsqlite3.dylib" \
    "$SQLDIR/sqlite3.c" -lpthread -ldl -lm
ls -la "$WORK/libsqlite3.dylib"
file "$WORK/libsqlite3.dylib"

echo
echo "==> step 2/3: build a dlopen test exe"
cat > "$WORK/test.c" <<'EOF'
#include <stdio.h>
#include <dlfcn.h>
typedef struct sqlite3 sqlite3;
int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "./libsqlite3.dylib";
    void *h = dlopen(path, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

    const char *(*libversion)(void) = dlsym(h, "sqlite3_libversion");
    int (*open_)(const char *, sqlite3 **) = dlsym(h, "sqlite3_open");
    int (*close_)(sqlite3 *) = dlsym(h, "sqlite3_close");
    int (*exec_)(sqlite3 *, const char *, void *, void *, char **) =
        dlsym(h, "sqlite3_exec");

    if (!libversion || !open_ || !close_ || !exec_) {
        fprintf(stderr, "dlsym failed\n"); return 1;
    }

    printf("sqlite3 version: %s\n", libversion());

    sqlite3 *db = 0;
    if (open_(":memory:", &db) != 0) { fprintf(stderr, "open failed\n"); return 1; }
    if (exec_(db, "create table t(a int); insert into t values(42);",
              0, 0, 0) != 0) {
        fprintf(stderr, "exec failed\n"); return 1;
    }
    printf("sqlite3 dylib end-to-end OK\n");
    close_(db);
    dlclose(h);
    return 0;
}
EOF
cd "$WORK"
"$OLDPWD/$TCC" -o test test.c

echo
echo "==> step 3/3: run the test"
./test
