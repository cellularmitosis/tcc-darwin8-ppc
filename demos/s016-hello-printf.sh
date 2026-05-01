#!/bin/sh
# Session 016 milestone — first user-visible payoff: tcc-emitted
# code calls into libSystem (printf) and prints to stdout.
#
# Run on imacg3 (Tiger 10.4.11 PowerPC):
#
#     ./demos/s016-hello-printf.sh
#
# Expected last lines:
#     hello from tcc-built program
#     exit=0
#
# Why this matters: every prior demo either returned an exit code
# directly or ran a self-contained computation. This is the first
# demo where tcc-generated code calls a function that lives in a
# dynamic library (libSystem) — `printf`. Closing this loop required
# implementing PowerPC PIC stubs (`__picsymbolstub1` + lazy symbol
# pointers + indirect symbol table) in `tcc/ppc-macho.c`, the
# canonical Mach-O recipe for late-binding external function calls.
#
# What landed in 016 (in `tcc/ppc-macho.c`):
#   - collect_extern_stubs(): scan relocs, find every R_PPC_REL24
#     against an undef external, dedupe.
#   - Synthesized __TEXT,__picsymbolstub1 section: 32 bytes per
#     stub, S_SYMBOL_STUBS section type, canonical 8-instruction
#     PowerPC PIC stub:
#         mflr r0
#         bcl  20, 31, .+4    ; call to next insn (PC-relative)
#         mflr r11            ; r11 = address of next insn
#         addis r11, r11, ha(la_ptr - .)
#         mtlr r0             ; restore caller's LR
#         lwzu r12, lo(la_ptr - .)(r11)   ; r12 = *la_ptr
#         mtctr r12
#         bctr
#   - Synthesized __DATA,__la_symbol_ptr section: 4 bytes per stub.
#     Initial values are zero; ld writes pointers to
#     dyld_stub_binding_helper, which dyld replaces with the real
#     function pointer on first call.
#   - Indirect symbol table linking each stub + la_ptr slot to its
#     external symbol's nlist index.
#   - Scattered HA16/LO16-SECTDIFF + PAIR relocs on the addis/lwzu
#     so ld can fix up the displacement when the executable is laid
#     out.
#   - n_desc = REFERENCE_FLAG_UNDEFINED_LAZY on every external
#     function symbol so dyld treats them as lazy-bound.
#   - BR24 retargeting: relocs against external functions are
#     rewritten as section-relative R_PPC_REL24 against the local
#     __picsymbolstub1 section, with the displacement pre-computed
#     to the stub's offset.
#
# What's still TODO before tcc-self bootstraps:
#   - PIC indirection for external DATA symbols (e.g. `___sF`,
#     `_isspace[]`). Currently tcc emits `addis ; lwz` with direct
#     R_PPC_ADDR16_HA / R_PPC_ADDR16_LO relocations against the
#     extern data symbol; dyld can't patch a 16-bit instruction
#     immediate at load time on PPC. Fix requires a codegen change
#     in `ppc-gen.c` to route external-data accesses through a
#     `__nl_symbol_ptr` indirection. ~ next session.

set -e
cd "$(dirname "$0")/.."

cat > /tmp/s016.c <<'EOF'
#include <stdio.h>
int main(void)
{
    printf("hello from tcc-built program\n");
    return 0;
}
EOF

./tcc/tcc -B./tcc -c /tmp/s016.c -o /tmp/s016.o
file /tmp/s016.o
gcc-4.0 /tmp/s016.o -o /tmp/s016
/tmp/s016
echo "exit=$?"
