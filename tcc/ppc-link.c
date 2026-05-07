/*
 * ppc-link.c — relocation handling for 32-bit PowerPC.
 *
 * Stub scaffolding (session 003). Real relocation logic lands as we
 * grow real codegen in ppc-gen.c.
 *
 * Copyright (c) 2026 the tcc-darwin8-ppc contributors.
 * MIT licensed; see ../LICENSE.
 */

#ifdef TARGET_DEFS_ONLY

#define EM_TCC_TARGET   EM_PPC

#define R_DATA_32       R_PPC_ADDR32
#define R_DATA_PTR      R_PPC_ADDR32
#define R_JMP_SLOT      R_PPC_JMP_SLOT
#define R_GLOB_DAT      R_PPC_GLOB_DAT
#define R_COPY          R_PPC_COPY
#define R_RELATIVE      R_PPC_RELATIVE

/* Synthetic relocation types used internally by the PowerPC backend
 * to mark a PIC-base-relative reference to a __nl_symbol_ptr slot for
 * an external data symbol. ppc-gen.c emits these; ppc-macho.c
 * translates them to scattered HA16_SECTDIFF / LO16_SECTDIFF + PAIR
 * record sequences for ld(1). They never appear in -run mode (PIC is
 * only emitted when output_type == TCC_OUTPUT_OBJ).
 *
 * Numeric values chosen to avoid collisions with the highest real
 * R_PPC_* codes (currently up to 255 = R_PPC_TOC16). 200 and 201
 * sit comfortably in the gap between the DIAB extensions (~185)
 * and IRELATIVE (248). */
#define R_PPC_HA16_PIC  200
#define R_PPC_LO16_PIC  201

/* PPC has no upstream R_PPC_NUM in elf.h; the highest base reloc we use
 * is 36 (R_PPC_SECTOFF_HA). 256 leaves comfortable room without colliding
 * with the high (>= 67) TLS / REL16 ranges. */
#define R_NUM           256

#define ELF_START_ADDR  0x10000000
#define ELF_PAGE_SIZE   0x1000

#define PCRELATIVE_DLLPLT 1
#define RELOCATE_DLLPLT 0

#else /* !TARGET_DEFS_ONLY */

#include "tcc.h"

#ifdef NEED_RELOC_TYPE
ST_FUNC int code_reloc(int reloc_type)
{
    switch (reloc_type) {
        case R_PPC_ADDR32:
        case R_PPC_ADDR16:
        case R_PPC_ADDR16_LO:
        case R_PPC_ADDR16_HI:
        case R_PPC_ADDR16_HA:
        case R_PPC_GLOB_DAT:
        case R_PPC_COPY:
        case R_PPC_RELATIVE:
        case R_PPC_HA16_PIC:
        case R_PPC_LO16_PIC:
            return 0;
        case R_PPC_REL24:
        case R_PPC_REL14:
        case R_PPC_PLTREL24:
        case R_PPC_LOCAL24PC:
        case R_PPC_JMP_SLOT:
            return 1;
    }
    return -1;
}

ST_FUNC int gotplt_entry_type(int reloc_type)
{
    switch (reloc_type) {
        case R_PPC_ADDR16:
        case R_PPC_ADDR16_LO:
        case R_PPC_ADDR16_HI:
        case R_PPC_ADDR16_HA:
        case R_PPC_GLOB_DAT:
        case R_PPC_COPY:
        case R_PPC_JMP_SLOT:
        case R_PPC_HA16_PIC:
        case R_PPC_LO16_PIC:
            return NO_GOTPLT_ENTRY;
        case R_PPC_ADDR32:
        case R_PPC_REL24:
        case R_PPC_REL14:
            return AUTO_GOTPLT_ENTRY;
        case R_PPC_PLTREL24:
        case R_PPC_LOCAL24PC:
            return ALWAYS_GOTPLT_ENTRY;
    }
    return -1;
}

#ifdef NEED_BUILD_GOT
/* Forward decl: defined below the relocate() block. */
static void ppc_link_write32be(unsigned char *p, uint32_t v);

/* Each PLT entry is 16 bytes (4 PPC instructions):
 *
 *   lis   r12, ha(target)
 *   ori   r12, r12, lo(target)
 *   mtctr r12
 *   bctr
 *
 * The lis/ori immediates start as zeros (with `got_offset` stashed
 * in the first 4 bytes) and are patched in by relocate_plt once the
 * symbol's runtime address is known. PLT0 is a 16-byte zero
 * placeholder -- we don't do lazy resolution in JIT mode (all
 * symbols are eagerly resolved via dlsym before the patches
 * happen), so PLT0 is unused but present so PLT entry indexing
 * matches the convention build_got_entries expects (PLT entries at
 * offsets 16, 32, 48, ...). */
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr)
{
    Section *plt = s1->plt;
    unsigned plt_offset;
    unsigned char *p;

    /* PLT0: empty placeholder. */
    if (plt->data_offset == 0) {
        p = section_ptr_add(plt, 16);
        memset(p, 0, 16);
    }

    plt_offset = plt->data_offset;
    p = section_ptr_add(plt, 16);
    /* Stash got_offset in the first 4 bytes (BE) so relocate_plt
     * can recover the GOT slot for this entry. The whole 16 bytes
     * gets overwritten with the real instruction sequence then. */
    ppc_link_write32be(p, got_offset);
    memset(p + 4, 0, 12);
    (void)attr;
    return plt_offset;
}

/* relocate_plt: for each PLT entry, decode its got_offset from the
 * placeholder, find the corresponding symbol (via the GOT
 * relocations -- they're keyed by got_offset), and patch the
 * 4-instruction stub. We don't read from s1->got->data because
 * relocate_sections (which fills the GOT) runs AFTER us. We look
 * up sym->st_value directly, which relocate_syms set just before
 * we ran (via dlsym for SHN_UNDEF / via section base for defined). */
ST_FUNC void relocate_plt(TCCState *s1)
{
    unsigned char *p, *p_end;

    if (!s1->plt || s1->plt->data_offset == 0 || !s1->got || !s1->got->reloc)
        return;

    p = s1->plt->data + 16;        /* skip PLT0 */
    p_end = s1->plt->data + s1->plt->data_offset;

    while (p < p_end) {
        uint32_t got_offset =
            ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
            ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
        ElfW_Rel *rel;
        int sym_index = -1;
        uint32_t addr;
        uint32_t hi, lo;

        /* Find the GOT relocation whose r_offset matches our
         * got_offset; that gives us the symbol. */
        for_each_elem(s1->got->reloc, 0, rel, ElfW_Rel) {
            if ((uint32_t)rel->r_offset == got_offset) {
                sym_index = ELFW(R_SYM)(rel->r_info);
                break;
            }
        }
        if (sym_index < 0) {
            tcc_error_noabort(
                "ppc-link: PLT entry @ 0x%lx has got_offset 0x%x but "
                "no matching .got.rel entry found",
                (unsigned long)(s1->plt->sh_addr + (p - s1->plt->data)),
                (unsigned)got_offset);
            return;
        }

        addr = (uint32_t)((ElfW(Sym) *)s1->symtab->data)[sym_index].st_value;
        /* lis+ori, not lis+addi -- ori is zero-extending so we use
         * @hi (plain high half), NOT @ha (the +0x8000 adjustment that
         * compensates for addi's sign extension). Using @ha here
         * yields the wrong absolute address whenever lo's top bit is
         * set (i.e. for ~50% of libSystem entry points). */
        hi = (addr >> 16) & 0xffff;
        lo = addr & 0xffff;
        ppc_link_write32be(p,    0x3d800000u | hi);     /* lis  r12, hi */
        ppc_link_write32be(p+4,  0x618c0000u | lo);     /* ori  r12,r12,lo */
        ppc_link_write32be(p+8,  0x7d8903a6u);           /* mtctr r12 */
        ppc_link_write32be(p+12, 0x4e800420u);           /* bctr */
        p += 16;
    }
}
#endif
#endif

/* Same big-endian helpers as ppc-gen.c, repeated here so ppc-link.c
 * stays self-contained when compiled standalone (ONE_SOURCE=no). */
static void ppc_link_write32be(unsigned char *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8)  & 0xff;
    p[3] = v         & 0xff;
}

static uint32_t ppc_link_read32be(unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr,
                      addr_t addr, addr_t val)
{
    int32_t disp;
    uint32_t orig;

    switch (type) {
    case R_PPC_NONE:
        return;

    case R_PPC_ADDR32:
        /* 32-bit absolute address. ELF Rel-format implicit-addend
         * convention: the in-place value IS the addend (e.g. init_putv
         * writes `4` for `int *p = &arr[1]`). ADD val, don't overwrite,
         * so the addend survives. Matches what i386-link.c does for
         * R_386_32. */
        ppc_link_write32be(ptr, ppc_link_read32be(ptr) + (uint32_t)val);
        return;

    case R_PPC_REL24:
        /* PC-relative 24-bit branch displacement, used by `b` and
         * `bl`. The encoding stores LI in bits 6-29 (mask 0x03fffffc)
         * and treats it as a signed 26-bit byte offset (low 2 bits
         * implicit zero).
         *
         * For absolute symbols (SHN_ABS, e.g. tcc_add_symbol-registered
         * callbacks) that land >32MB from the JIT region, build_got_entries
         * synthesizes a PLT trampoline near the JIT code and rewrites
         * the reloc to target it; so by the time we run, val is always
         * in range. The check below is a defensive guard. */
        disp = (int32_t)(val - addr);
        if (disp < -(1 << 25) || disp >= (1 << 25)) {
            int sym_idx = ELFW(R_SYM)(rel->r_info);
            ElfW(Sym) *sym = (ElfW(Sym) *)symtab_section->data + sym_idx;
            const char *sym_name = (char *)symtab_section->link->data + sym->st_name;
            tcc_error_noabort("R_PPC_REL24 out of range "
                              "(addr=0x%lx val=0x%lx sym='%s')",
                              (unsigned long)addr, (unsigned long)val,
                              sym_name ? sym_name : "(null)");
            return;
        }
        orig = ppc_link_read32be(ptr);
        ppc_link_write32be(ptr, (orig & ~0x03fffffc) | ((uint32_t)disp & 0x03fffffc));
        return;

    case R_PPC_ADDR16_HA: {
        /* High 16 bits, adjusted: (val + 0x8000) >> 16 -- compensates
         * for the sign-extension that `addi` will do on the LO half. */
        uint16_t ha = ((val + 0x8000) >> 16) & 0xffff;
        orig = ppc_link_read32be(ptr);
        ppc_link_write32be(ptr, (orig & ~0xffff) | ha);
        return;
    }
    case R_PPC_ADDR16_HI: {
        uint16_t hi = (val >> 16) & 0xffff;
        orig = ppc_link_read32be(ptr);
        ppc_link_write32be(ptr, (orig & ~0xffff) | hi);
        return;
    }
    case R_PPC_ADDR16_LO: {
        uint16_t lo = val & 0xffff;
        orig = ppc_link_read32be(ptr);
        ppc_link_write32be(ptr, (orig & ~0xffff) | lo);
        return;
    }

    case R_PPC_JMP_SLOT:
    case R_PPC_GLOB_DAT:
        /* Both kinds reduce to "store the resolved address into the
         * GOT slot". Used by the JIT path: GOT entries hold target
         * addresses for the PLT stubs to load. Big-endian write so
         * generated PPC code's `lwz` reads the right value. */
        ppc_link_write32be(ptr, (uint32_t)val);
        return;

    case R_PPC_HA16_PIC:
    case R_PPC_LO16_PIC:
        /* Synthetic PIC relocations are only emitted for Mach-O .o
         * output (TCC_OUTPUT_OBJ); they should never reach this path
         * in -run / -shared / executable output. If we get here it
         * almost certainly indicates a bug -- something emitted a PIC
         * reloc in a non-OBJ output. Surface it loudly. */
        tcc_error_noabort("ppc-link: R_PPC_HA16_PIC/LO16_PIC encountered "
                          "in non-OBJ output (type=%d)", type);
        return;

    default:
        tcc_error_noabort("ppc-link: relocate type 0x%x not implemented "
                          "(addr=0x%lx val=0x%lx)",
                          type, (unsigned long)addr, (unsigned long)val);
    }
}

#endif /* TARGET_DEFS_ONLY */
