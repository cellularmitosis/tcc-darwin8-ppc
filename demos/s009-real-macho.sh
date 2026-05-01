#!/bin/sh
# Session 009 milestone — tcc emits real PowerPC Mach-O .o files
# that Tiger's gcc-4.0 / ld can link into runnable executables.
#
# Run on imacg3 (Tiger 10.4.11 PowerPC):
#
#     ./demos/s009-real-macho.sh
#
# Expected last line: "exit=42"
#
# Why this matters: every prior session ran code via `tcc -B. -run`,
# the JIT path. This is the first time tcc emits a real on-disk .o
# file that the system linker can consume. It's the bedrock for:
#   - Linking against libSystem (printf, malloc, ...).
#   - Bootstrapping (compile tcc with our tcc).
#   - Shipping a /opt-installable tarball.
#
# What landed in 009: tcc/ppc-macho.c (~1100 lines) — a stand-alone
# 32-bit PPC Mach-O object writer (MH_OBJECT only). It sidesteps the
# x86_64/arm64-only tccmacho.c entirely and uses the ppc-link.c
# relocation handler to pre-resolve same-section relocs. Final
# linking is delegated to Tiger's gcc-4.0 / ld.
#
# Surface that works through this demo:
#   - mach_header / segment_command / section / nlist (32-bit) emit
#   - Big-endian field ordering throughout
#   - Local + global symbol table
#   - R_PPC_REL24 (intra-section bl) translated to PPC_RELOC_BR24
#   - Multi-function programs link correctly
#
# Surface deferred to later sessions:
#   - Calls into dynamic libraries (printf via dyld)
#   - PIC stubs / __la_symbol_ptr
#   - Executable output (we emit .o, gcc finishes the link)

set -e
cd "$(dirname "$0")/.."

cat > /tmp/s009.c <<'EOF'
int sq(int x) { return x * x; }
int sum_to(int n) {
    int s = 0;
    int i = 1;
    while (i <= n) { s = s + i; i = i + 1; }
    return s;
}
int main(void) {
    /* sq(5) = 25; sum_to(7) = 28; 25 - 28 + 45 = 42 */
    return sq(5) - sum_to(7) + 45;
}
EOF

./tcc/tcc -c /tmp/s009.c -o /tmp/s009.o
file /tmp/s009.o
gcc-4.0 /tmp/s009.o -o /tmp/s009
/tmp/s009
echo "exit=$?"
