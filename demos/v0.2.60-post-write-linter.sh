#!/bin/sh
# v0.2.60-g3 milestone — post-write linter
# (roadmap follow-on to v0.2.50 / v0.2.59 pre-write blocks).
#
# Run on imacg3 (or any Tiger 10.4 PowerPC G3/G4):
#
#     ./demos/v0.2.60-post-write-linter.sh
#
# Expected last line:
#     PASS: exe + dylib both built and ran; post-write linter silent on both paths
#
# What this demonstrates:
#   The Mach-O writers (macho_output_exe and macho_output_dylib) now
#   call macho_post_write_lint after fwrite+fclose. The linter
#   re-reads the file through a fresh open/read path that does NOT
#   share state with the writer, walks the Mach header + load
#   commands + sections, and verifies high-level shape invariants:
#
#     - magic / cputype / cpusubtype / filetype as expected
#     - sum of per-command cmdsizes == header.sizeofcmds
#     - walked command count == header.ncmds
#     - each LC_SEGMENT's cmdsize == 56 + nsects*68
#     - each LC_SEGMENT's [fileoff, fileoff+filesize) fits in the file
#     - each section's file/vm range inside its owning segment
#       (zerofill / vmsize==0 segments handled correctly)
#     - LC_SYMTAB / LC_DYSYMTAB cmdsize, exactly-one presence,
#       partition arithmetic (ilocalsym + nlocalsym == iextdefsym etc.)
#     - LC_SYMTAB nlist + strtab ranges, LC_DYSYMTAB indirectsym /
#       extreloff / locreloff ranges all fit in the file
#     - LC_LOAD_DYLIB / LC_LOAD_DYLINKER / LC_ID_DYLIB / LC_UNIXTHREAD
#       counts match what the writer intended (formula derives from
#       has_externs + n_extra_dylibs)
#
#   Pre-write checks (v0.2.50 EXE, v0.2.59 dylib) verify the writer's
#   *design* — variables in memory. This linter verifies the *emission* —
#   the bytes on disk. Catches obuf corruption, byteorder regressions,
#   command-size mismatches, truncated fwrites — bug classes the pre-
#   write blocks can't see because they fire before any bytes leave
#   memory.
#
#   The build below exercises both call sites: an EXE with externs
#   (printf -> libSystem stub) goes through macho_output_exe and the
#   EXE branch of the linter; a dylib with externs goes through
#   macho_output_dylib and the dylib branch. Both lints are silent
#   on the happy path; if either ever fires on a future regression
#   you'll see `tcc: error: ppc-macho: post-write: ...` instead of
#   a dyld run-time crash.

set -e
cd "$(dirname "$0")/.."
TCC=${TCC:-./tcc/tcc}
WORK=${WORK:-/tmp/v0.2.60-post-write-linter}
mkdir -p "$WORK"

# (a) EXE with externs — exercises macho_output_exe post-write lint.
"$TCC" demos/v0.2.60-post-write-linter.c -o "$WORK/v0260-exe"
EXE_OUT="$("$WORK/v0260-exe")"
echo "exe: $EXE_OUT"

# (b) Dylib with externs — exercises macho_output_dylib post-write
#     lint. A dlopen driver loads it back so we know the dylib is
#     actually well-formed end-to-end (not just shape-valid).
cat > "$WORK/lib.c" <<'EOF'
#include <stdio.h>
int sum3(int a, int b, int c)
{
    printf("dylib sum3(%d, %d, %d)\n", a, b, c);
    return a + b + c;
}
EOF

cat > "$WORK/driver.c" <<'EOF'
#include <stdio.h>
#include <dlfcn.h>
int main(void)
{
    void *h = dlopen("/tmp/v0.2.60-post-write-linter/v0260.dylib", RTLD_NOW);
    int (*sum3)(int, int, int);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    sum3 = (int (*)(int, int, int))dlsym(h, "sum3");
    if (!sum3) { fprintf(stderr, "dlsym: %s\n", dlerror()); return 1; }
    printf("dylib returned %d\n", sum3(10, 20, 30));
    dlclose(h);
    return 0;
}
EOF

"$TCC" -shared "$WORK/lib.c" -o "$WORK/v0260.dylib"
"$TCC" "$WORK/driver.c" -o "$WORK/v0260-driver"
"$WORK/v0260-driver"

echo "PASS: exe + dylib both built and ran; post-write linter silent on both paths"
