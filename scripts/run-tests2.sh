#!/bin/sh
# run-tests2.sh — run TCC's own tests2/ suite (~127 tests covering
# language and codegen edge cases) using our PPC tcc, with NORUN=true
# to force the -o exe + ./exe path (since tcc -run isn't fully wired
# on PPC yet).
#
# Captures the full make log to scripts/../docs/sessions/.../tests2.log
# (or wherever LOG points) and prints a pass/fail summary.

set -e

cd "$(dirname "$0")/.."

LOG="${LOG:-/tmp/tests2.log}"
TCCDIR=tcc

if [ ! -x "$TCCDIR/tcc" ]; then
    echo "ERROR: $TCCDIR/tcc not found. Run scripts/build-tiger.sh first." >&2
    exit 1
fi

echo "==> Running tests2 (NORUN=true; force -o exe path)..."
/opt/make-4.3/bin/make -C "$TCCDIR/tests/tests2" NORUN=true -k > "$LOG" 2>&1 || true

# Count pass/fail using python (Tiger has python 2).
python <<PYEOF
import re
log = open("$LOG").read()
lines = log.split("\n")
tests = []
i = 0
while i < len(lines):
    m = re.match(r"^Test: ([^\.]+)\.\.\.", lines[i])
    if m:
        name = m.group(1)
        j = i + 1
        passed = True
        while j < len(lines):
            if lines[j].startswith("Test:"):
                break
            if lines[j].startswith("---") or "Error" in lines[j] or "error:" in lines[j]:
                passed = False
                break
            j += 1
        tests.append((name, passed))
    i += 1

passed_count = 0
failed_names = []
for nm, p in tests:
    if p: passed_count += 1
    else: failed_names.append(nm)

total = len(tests)
if total:
    pct = 100.0 * passed_count / total
else:
    pct = 0
print "Total: %d  Pass: %d  Fail: %d  (%.1f%% pass)" % (
    total, passed_count, total-passed_count, pct)
if failed_names:
    print
    print "Failed (%d):" % len(failed_names)
    for nm in failed_names:
        print "  ", nm
PYEOF

echo
echo "Full log: $LOG"
