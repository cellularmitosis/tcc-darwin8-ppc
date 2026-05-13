#!/bin/sh
# v0.2.50-g3 milestone — self-link diagnostics (roadmap #7).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.50-self-link-diagnostics.sh
#
# Expected last line:
#     rodata works | data=42 | bss=0 33 77 | p=ok
#
# What this demonstrates:
#   The Mach-O EXE writer (tcc/ppc-macho.c::macho_output_exe) now runs
#   four pre-serialize sanity checks that catch broken-image classes
#   which historically only surfaced as cryptic runtime dyld errors:
#     (a) VM layout (segment alignment / overlap / vmsize >= filesize
#         / __LINKEDIT placement) — would have caught session-025's
#         "Cannot allocate memory" dyld failure directly.
#     (b) `__mh_execute_header` registered as N_ABS at __TEXT base
#         and entry_addr inside __text — would have caught the
#         "Symbol not found: __mh_execute_header" dyld error.
#     (c) Stub addresses inside __symbol_stub1, slot addresses
#         inside __nl_symbol_ptr, every ST_PPC_NEEDS_STUB symbol has
#         a stub — would have caught the session-025 crt1 SIGBUS.
#     (d) Every defined symbol's section is in the EXE writer's
#         emitted section list — would have caught the common-symbol
#         allocated to .bss with __bss not emitted case.
#   This demo's source exercises all four section classes (text,
#   rodata, data, bss, libSystem stubs, a function-pointer init) so
#   the happy path walks every check on every emitted section. The
#   program itself doesn't trigger the diagnostics — it proves the
#   checks are non-disruptive on a normal compile. See the session
#   068 README for the manual break-and-verify procedure that
#   confirms each check fires when its invariant is violated.
set -e
cd "$(dirname "$0")/.."
TCC=./tcc/tcc
[ -x "$TCC" ] || (
    cd tcc
    PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure || true
)
SRC="demos/v0.2.50-self-link-diagnostics.c"
OUT=/tmp/v0.2.50-self-link-diagnostics
$TCC -B./tcc -L./tcc -I./tcc/include -w "$SRC" -o "$OUT"
"$OUT"
