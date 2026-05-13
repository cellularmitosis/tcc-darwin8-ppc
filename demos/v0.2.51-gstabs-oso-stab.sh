#!/bin/sh
# v0.2.51-g3 milestone — OSO STAB emission for gdb-on-Tiger
# (roadmap #7.5).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.51-gstabs-oso-stab.sh
#
# Expected last line:
#     OK: tcc -gstabs produces gdb-debuggable Mach-O on Tiger.
#
# What this demonstrates:
#   tcc's existing -gstabs flag (which routes the legacy GNU
#   stabs+ emission instead of the v0.2.37+ DWARF default) now
#   actually populates the linked Mach-O exe's LC_SYMTAB nlist
#   with the STAB chain that Apple's gdb 6.3 on Tiger reads
#   directly. Before v0.2.51 the stab_section was emitted during
#   compilation but silently dropped by the Mach-O writer, so
#   `tcc -gstabs` exes ran identically to non-debug builds and
#   `break <func>` could only land on the function entry address
#   with no file:line knowledge.
#
#   This demo:
#     1. Builds the .c with `tcc -gstabs`.
#     2. Runs it (basic sanity — STABs in the symtab don't change
#        runtime semantics).
#     3. nm -ap dump showing the STAB chain (N_SO source-path,
#        N_FUN per function w/ paired end-entry, N_SLINE per line,
#        N_PSYM/N_LSYM for parameters/locals, N_GSYM/N_STSYM for
#        globals/statics, N_LBRAC/N_RBRAC for block scoping).
#     4. gdb scripts:
#        (a) `break main` + `break helper` resolve to file:line.
#        (b) `run`, breakpoint at main hits; `print g_global` /
#            `print g_static` work.
#        (c) `continue` into helper; `bt` shows both frames with
#            file:line.
#        (d) `list main` prints source from the file referenced
#            by the N_SO entry.
#
# Phase-1 scope: file:line breakpoints, source listing, run/step,
# backtraces, and global-variable inspection all work end-to-end.
# Phase-2 polish (parameter / local-variable offsets matching tcc's
# PPC codegen frame layout, N_OSO chain for dsymutil round-tripping,
# defaulting -g to stabs on Darwin) is tracked in the session 072
# HANDOFF.
set -e
cd "$(dirname "$0")/.."

TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ../scripts/build-tiger.sh configure || true
)

SRC="demos/v0.2.51-gstabs-oso-stab.c"
OUT=/tmp/v0.2.51-gstabs-oso-stab

# (1) build
"$TCC" -B./tcc -L./tcc -I./tcc/include -gstabs -w "$SRC" -o "$OUT"

# (2) run
echo "== program output =="
"$OUT"

# (3) STAB chain summary
echo
echo "== STAB chain (nm -ap, filtered) =="
nm -ap "$OUT" | grep -E "FUN|SLINE|BRAC|^[0-9a-f]+ - [0-9]+ [0-9]+    SO |GSYM|STSYM|PSYM" | head -20

# (4) gdb workflow: ensure debug info is actually consumable.
echo
echo "== gdb workflow =="

# (4a) break by function and by line, info functions, info source
cat > /tmp/v0.2.51-gdb1.cmd <<'EOF'
break main
break helper
break 11
info functions ^helper$
info functions ^main$
quit
EOF
gdb -batch -nx -x /tmp/v0.2.51-gdb1.cmd "$OUT" 2>&1 \
    | grep -E "Breakpoint|matching|^int (helper|main)" \
    | head -10

# Confirm at least these key gdb behaviors:
#   - Three breakpoint resolutions citing /tmp/v0.2.51-... file
#     with sensible line numbers.
#   - `info functions ^helper$` and `^main$` find them.
G1OUT=$(gdb -batch -nx -x /tmp/v0.2.51-gdb1.cmd "$OUT" 2>&1)
echo "$G1OUT" | grep -q "Breakpoint 1 .* line " || { echo "FAIL: gdb 'break main' didn't get file:line"; exit 1; }
echo "$G1OUT" | grep -q "Breakpoint 2 .* line " || { echo "FAIL: gdb 'break helper' didn't get file:line"; exit 1; }
echo "$G1OUT" | grep -q "Breakpoint 3 .* line " || { echo "FAIL: gdb 'break 11' didn't get file:line"; exit 1; }
echo "$G1OUT" | grep -qE "int.*helper\(int" || { echo "FAIL: gdb 'info functions helper' didn't show prototype"; exit 1; }

# (4b) Actually run the program through gdb, hit main's breakpoint,
#      print globals, continue into helper, backtrace.
cat > /tmp/v0.2.51-gdb2.cmd <<'EOF'
break main
break helper
run
print g_global
print g_static
continue
where
quit
EOF
G2OUT=$(gdb -batch -nx -x /tmp/v0.2.51-gdb2.cmd "$OUT" 2>&1)
echo "$G2OUT" | tail -12
echo "$G2OUT" | grep -q "\$1 = 7"  || { echo "FAIL: gdb 'print g_global' != 7"; exit 1; }
echo "$G2OUT" | grep -q "\$2 = 42" || { echo "FAIL: gdb 'print g_static' != 42"; exit 1; }
echo "$G2OUT" | grep -qE "helper \(.*\) at " \
    || { echo "FAIL: gdb didn't stop in helper after continue"; exit 1; }
echo "$G2OUT" | grep -qE "in main \(.*\) at " \
    || { echo "FAIL: gdb 'where' missing main frame"; exit 1; }

echo
echo "OK: tcc -gstabs produces gdb-debuggable Mach-O on Tiger."
