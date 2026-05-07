#!/bin/sh
# v0.2.30-alacarte.sh — selective archive loading via the BSD
# `__.SYMDEF SORTED` symbol table.
#
# Pre-v0.2.30, tcc force-loaded every member of a Mach-O `.a`
# archive (whole-archive). For an archive with a hundred members
# of which the user only references three, that meant pulling in
# 97 redundant `.o` files: bigger executables, and any one of
# those unrelated members crashing during load could fail an
# otherwise-fine link.
#
# v0.2.30 parses the archive's BSD `__.SYMDEF` / `__.SYMDEF SORTED`
# member to map each defined symbol to the file offset of the
# `.o` member that provides it. At link time tcc walks its own
# undefined-symbol list and pulls in only the members needed to
# resolve them, repeating until no new members get added.
#
# This demo: build a 3-member archive, link a program that uses
# only 2 of the 3 members, and verify only those 2 members are
# pulled in (third stays absent from the resulting binary).

set -e
cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.30-alacarte}

mkdir -p "$WORK"

cat > "$WORK/lib_a.c" <<'EOF'
int lib_a(int x) { return x + 1; }
EOF
cat > "$WORK/lib_b.c" <<'EOF'
int lib_b(int x) { return x * 2; }
EOF
cat > "$WORK/lib_c.c" <<'EOF'
int lib_c(int x) { return x - 3; }    /* used by NO ONE in this demo */
EOF

cat > "$WORK/use_lib.c" <<'EOF'
#include <stdio.h>
extern int lib_a(int);
extern int lib_b(int);
int main(void) {
    printf("lib_a(10)=%d\n", lib_a(10));    /* expect 11 */
    printf("lib_b(10)=%d\n", lib_b(10));    /* expect 20 */
    return 0;
}
EOF

cd "$WORK"
gcc-4.0 -arch ppc -c lib_a.c lib_b.c lib_c.c
ar rcs libtest.a lib_a.o lib_b.o lib_c.o
echo "==> archive symdef:"
ar t libtest.a

echo
echo "==> tcc -vv link (alacarte; expect lib_c.o NOT loaded):"
"$OLDPWD/$TCC" -B"$OLDPWD/tcc" -vv use_lib.c libtest.a -o use_lib 2>&1 \
    | egrep "^(   ->|->) " | head -20

echo
echo "==> nm of resulting executable (lib_c should NOT appear):"
nm use_lib | egrep "lib_[abc]" || true

echo
echo "==> running:"
./use_lib

if nm use_lib | grep -q "_lib_c"; then
    echo
    echo "FAIL — _lib_c was pulled in despite not being referenced"
    exit 1
fi

echo
echo "PASS — only lib_a / lib_b were pulled from libtest.a; lib_c skipped"
