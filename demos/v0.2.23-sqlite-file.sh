#!/bin/sh
# v0.2.23-sqlite-file.sh — full sqlite3 + tcc-darwin8-ppc round trip
# against a real on-disk database. Pre-v0.2.23 this SEGV'd inside
# unixOpen → osFcntl(...) because tcc was storing the
# __nl_symbol_ptr SLOT address (not callable) into the data slot
# for `(sqlite3_syscall_ptr)fcntl`. v0.2.23 allocates a stub for
# extern functions referenced via R_PPC_ADDR32, so the slot now
# holds a callable stub address.
#
# Expected: prints rows ordered by age and exits 0.

set -e
cd "$(dirname "$0")/.."

SQLDIR=${SQLDIR:-/Users/macuser/tmp/sqlite-amalgamation-3460100}
TCC=${TCC:-tcc/tcc}
TMP=/tmp/v0223-sqlite

if [ ! -d "$SQLDIR" ]; then
    echo "ERROR: \$SQLDIR not found ($SQLDIR)."
    echo "Drop the sqlite amalgamation source there or set SQLDIR=/path/to/sqlite-amalgamation-XYZ"
    exit 1
fi

mkdir -p "$TMP"
cat > "$TMP"/sqlite_demo.c <<'EOF'
#include <stdio.h>
#include "sqlite3.h"

static int row_cb(void *unused, int n, char **vals, char **cols) {
    int i;
    for (i = 0; i < n; i++)
        printf("%s=%s%s", cols[i], vals[i] ? vals[i] : "(null)",
               i+1<n ? ", " : "\n");
    return 0;
}

int main(void) {
    sqlite3 *db; char *err;
    int rc;
    rc = sqlite3_open("/tmp/v0223-sqlite-demo.db", &db);
    if (rc) { fprintf(stderr, "open fail %d\n", rc); return 1; }
    rc = sqlite3_exec(db,
        "DROP TABLE IF EXISTS people;"
        "CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT, age INT);"
        "INSERT INTO people(name,age) VALUES "
        "  ('alice', 30), ('bob', 25), ('cara', 40);"
        "SELECT id, name, age FROM people ORDER BY age;",
        row_cb, NULL, &err);
    if (rc) {
        fprintf(stderr, "exec fail rc=%d err=%s\n",
                rc, err ? err : "(none)");
        sqlite3_free(err);
        return 2;
    }
    sqlite3_close(db);
    return 0;
}
EOF

rm -f /tmp/v0223-sqlite-demo.db

"$TCC" -B"$(dirname "$TCC")" \
       -I"$(dirname "$TCC")/include" -I"$SQLDIR" \
       -DSQLITE_OS_UNIX=1 -DSQLITE_THREADSAFE=0 -DHAVE_USLEEP=1 \
       -o "$TMP"/sqlite_demo "$SQLDIR"/sqlite3.c "$TMP"/sqlite_demo.c \
       -lpthread

"$TMP"/sqlite_demo
echo "exit=$?"
