#!/bin/sh
# Session 025 milestone — full self-link via tcc, no gcc required.
#
# Run on ibookg37 (or any Tiger 10.4.11 PowerPC):
#
#     ./demos/s025-self-link.sh
#
# Expected last lines:
#     hello from tcc-built and tcc-linked program
#     exit=0
#
# Why this matters: every prior libSystem-using demo (016 onwards)
# still went through gcc-4.0 for the LINK step (`gcc-4.0 /tmp/foo.o -o
# /tmp/foo`). gcc was needed to pull in /usr/lib/crt1.o (Apple's
# Darwin startup that initializes libSystem state), since tcc had no
# way to read classic Mach-O .o files. Session 025 implements that
# reader, so `tcc -o exe file.c` now produces a working executable
# end-to-end with no gcc involvement.
#
# What landed in 025 (in `tcc/ppc-macho.c` + `tcc/tccelf.c`):
#   - macho_load_object_file: ~700 LOC reader for classic Mach-O .o.
#     Parses Mach header + load commands, fat (universal) binaries
#     (since /usr/lib/crt1.o is a 4-arch fat archive), section list,
#     symbol table, indirect symbol table, relocations.
#   - Maps Mach-O sections to tcc's ELF-style names: __text → .text,
#     __cstring/__const → .data.ro, __data → .data, __bss → .bss.
#     Skips __symbol_stub1 / __nl_symbol_ptr / __la_symbol_ptr —
#     the EXE writer re-synthesizes them from scratch.
#   - Translates relocations: VANILLA, BR24/JBSR, HA16/HI16/LO16
#     (with PAIR), VANILLA. Indirect refs through skipped sections
#     resolve via the indirect symbol table to the underlying extern;
#     local-section refs synthesize anonymous "anchor" symbols.
#   - tcc_load_archive extended for Mach-O: BSD-style #1/N long
#     names, dispatches MACHO_REL members to the new reader.
#   - tcc_output_file auto-loads /usr/lib/crt1.o for executable
#     output on PPC; the older `__tcc_start_main` injection is gated
#     to only fire when crt1's `start` symbol isn't present.
#   - EXE writer fixes: __bss layout as zerofill, __mh_execute_header
#     pre-defined as N_ABS in s1->symtab, SHN_ABS handling in
#     exe_sym_addr, non-lazy-pointer slot fallback for ADDR16_*
#     relocs to extern data, ST_PPC_NEEDS_STUB hint for crt1's
#     __symbol_stub1 references, entry-point selection prefers crt1's
#     `start`, dropped LC_TWOLEVEL_HINTS hack.
#
# Limitations (still TODO):
#   - tcc-self itself still needs gcc-4.0 to link because tcc.c uses
#     libgcc helpers (___udivdi3, ___ashldi3, etc.) that aren't yet
#     in libtcc1.a. Adding them (or fixing the libgcc.a whole-archive
#     load that currently produces a broken binary) closes the loop.

set -e
cd "$(dirname "$0")/.."

cat > /tmp/s025.c <<'EOF'
int printf(const char *,...);
void *malloc(unsigned long);
void  free(void *);
char *strcpy(char *, const char *);
int main(int argc, char **argv)
{
    char *p = malloc(64);
    strcpy(p, "hello from tcc-built and tcc-linked program\n");
    printf("%s", p);
    free(p);
    return 0;
}
EOF

# No `gcc-4.0` anywhere in this pipeline. tcc compiles, tcc links.
./tcc/tcc -B./tcc -I./tcc -o /tmp/s025 /tmp/s025.c
file /tmp/s025
/tmp/s025
echo "exit=$?"
