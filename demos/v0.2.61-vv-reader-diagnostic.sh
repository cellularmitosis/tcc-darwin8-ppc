#!/bin/sh
# v0.2.61-g3 milestone — `-vv` reader diagnostic.
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.61-vv-reader-diagnostic.sh
#
# Expected last line:
#     PASS: -vv reader diagnostic surfaces N reader lines; default verbosity silent
#
# What this demonstrates:
#   `macho_load_object_file` (and its helpers `macho_translate_relocs` /
#   `macho_translate_sym`) makes routing decisions silently on the read
#   path: section skips, relocation drops, symbol filtering. Pre-v0.2.61
#   those decisions were invisible — when a real bug shaped like one of
#   them slipped past tcc and surfaced as a mysterious link-time error
#   later (cf. v0.2.40 sed bug, v0.2.44 SECTDIFF shadow-section bug),
#   triage meant adding ad-hoc printfs and rebuilding.
#
#   This release adds a single ~30-line static helper `macho_reader_log`
#   gated on `s1->verbose >= 2` (i.e. `-vv`). 21 call sites cover every
#   silent decision the reader makes:
#
#     section-skip (2 sites):
#       - pass-1 section skip in macho_load_object_file
#       - (the underlying macho_section_to_elf returning 0)
#
#     reloc-drop (13 sites):
#       - 1 in pass 3 of macho_load_object_file (whole-section drop
#         when a section was skipped pass 1)
#       - 12 inside macho_translate_relocs:
#           * SECTDIFF illegal for output type, unknown scattered type,
#             SECTDIFF target VA maps to no section, SECTDIFF
#             stub-slot resolve failed, SECTDIFF translated sym is 0,
#             non-extern target sect OOR, instruction at file off past
#             file end, non-extern type/length unhandled, non-extern
#             stub-slot resolve failed, target sym OOR, extern target
#             sym filtered by translate_sym, extern r_type with no
#             ELF mapping
#
#     sym-skip (7 sites):
#       - 4 inside macho_translate_sym (bad n_strx, empty name,
#         n_sect OOR, unknown N_TYPE basic)
#       - 3 in pass-2 symbol loop of macho_load_object_file
#         (local with non-N_SECT type, n_sect OOR, local in discarded
#         section)
#
#   Two log sources are *intentionally* not wired up to the helper:
#   STAB symbols (would flood under `-g`; debug syms are routinely
#   skipped, not a routing decision) and standalone PPC_RELOC_PAIR
#   continues (iteration mechanics — the paired half was consumed by
#   the preceding iteration).
#
#   Output goes to stderr in `tcc: reader: <basename>: <category>: <details>`
#   format. Stream separation keeps `-vv` diagnostics greppable
#   separately from any `tcc -run` program output. At default
#   verbosity the helper short-circuits at its first line, zero
#   overhead.
#
#   Real-world example output from this very demo on imacg3:
#
#     tcc: reader: foo.o: section-skip: __TEXT,__eh_frame (sect 3, ...)
#     tcc: reader: foo.o: section-skip: __TEXT,__picsymbolstub1 (sect 4, ...)
#     tcc: reader: foo.o: reloc-drop: section __TEXT,__picsymbolstub1 skipped: 8 relocs dropped wholesale
#     tcc: reader: crt1.o: reloc-drop: section __text reloc 21: non-extern stub-slot resolve failed (target sect 3, value 0x330)
#     ...

set -e
cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.61-vv-reader-diagnostic}
mkdir -p "$WORK"

# Helper .c that defines helper(int) — compiled separately so the
# link exercises multi-.o loading (foo.o + bar.o + crt1.o).
cat > "$WORK/bar.c" <<'EOF'
int helper(int x) { return x * 2; }
EOF

"$TCC" -c demos/v0.2.61-vv-reader-diagnostic.c -o "$WORK/foo.o"
"$TCC" -c "$WORK/bar.c"                         -o "$WORK/bar.o"

# (1) Default verbosity — stderr must be empty for reader (the loader
#     was silent here pre-v0.2.61, and must remain silent at default).
DEFAULT_STDERR="$WORK/default.stderr"
"$TCC" "$WORK/foo.o" "$WORK/bar.o" -o "$WORK/prog" 2>"$DEFAULT_STDERR"
if [ -s "$DEFAULT_STDERR" ]; then
    echo "FAIL: default verbosity produced stderr output:"
    cat "$DEFAULT_STDERR"
    exit 1
fi

# (2) -v (level 1) — writer summary on stdout, no reader logs.
V1_STDERR="$WORK/v1.stderr"
"$TCC" -v "$WORK/foo.o" "$WORK/bar.o" -o "$WORK/prog" 2>"$V1_STDERR" >/dev/null
if grep -q "tcc: reader:" "$V1_STDERR"; then
    echo "FAIL: -v (level 1) emitted reader diagnostics; should require -vv"
    grep "tcc: reader:" "$V1_STDERR"
    exit 1
fi

# (3) -vv (level 2) — reader logs to stderr, structured format.
VV_STDERR="$WORK/vv.stderr"
"$TCC" -vv "$WORK/foo.o" "$WORK/bar.o" -o "$WORK/prog" 2>"$VV_STDERR" >/dev/null

READER_LINES=$(grep -c "^tcc: reader:" "$VV_STDERR" || true)
if [ "$READER_LINES" -lt 5 ]; then
    echo "FAIL: -vv produced only $READER_LINES reader lines, expected >= 5"
    cat "$VV_STDERR"
    exit 1
fi

# Both major categories should appear at least once.
SECTION_SKIPS=$(grep -c "section-skip:" "$VV_STDERR" || true)
RELOC_DROPS=$(grep -c "reloc-drop:" "$VV_STDERR" || true)
if [ "$SECTION_SKIPS" -lt 1 ]; then
    echo "FAIL: -vv produced 0 section-skip lines"
    cat "$VV_STDERR"
    exit 1
fi
if [ "$RELOC_DROPS" -lt 1 ]; then
    echo "FAIL: -vv produced 0 reloc-drop lines"
    cat "$VV_STDERR"
    exit 1
fi

# Reader lines should reference the .o files we passed.
if ! grep -q "tcc: reader: foo.o:" "$VV_STDERR"; then
    echo "FAIL: -vv stderr doesn't reference foo.o (basename stripping broken?)"
    head -20 "$VV_STDERR"
    exit 1
fi

# Reader log format invariant: every line is
#   tcc: reader: <basename>: <category>: <details>
BAD_LINES=$(grep "^tcc: reader:" "$VV_STDERR" \
            | grep -vE "^tcc: reader: [^:]+: (section-skip|reloc-drop|sym-skip): " \
            | head -3 || true)
if [ -n "$BAD_LINES" ]; then
    echo "FAIL: -vv produced lines not matching expected format:"
    printf '%s\n' "$BAD_LINES"
    exit 1
fi

# (4) STAB filter check — same link with -g must NOT explode sym-skip
#     count. STAB symbols are skipped intentionally and below the log.
"$TCC" -g -c demos/v0.2.61-vv-reader-diagnostic.c -o "$WORK/foo-g.o"
"$TCC" -g -c "$WORK/bar.c"                         -o "$WORK/bar-g.o"
G_STDERR="$WORK/g.stderr"
"$TCC" -vv "$WORK/foo-g.o" "$WORK/bar-g.o" -o "$WORK/prog-g" 2>"$G_STDERR" >/dev/null
SYM_SKIPS_G=$(grep -c "sym-skip:" "$G_STDERR" || true)
if [ "$SYM_SKIPS_G" -gt 0 ]; then
    echo "FAIL: -g + -vv produced $SYM_SKIPS_G sym-skip lines; STAB filter regressed"
    grep "sym-skip:" "$G_STDERR" | head -5
    exit 1
fi

# (5) The program itself must still run correctly.
ACTUAL="$("$WORK/prog")"
if [ "$ACTUAL" != "v=14" ]; then
    echo "FAIL: program output '$ACTUAL', expected 'v=14'"
    exit 1
fi

echo "PASS: -vv reader diagnostic surfaces $READER_LINES reader lines ($SECTION_SKIPS section-skip, $RELOC_DROPS reloc-drop); default verbosity silent"
