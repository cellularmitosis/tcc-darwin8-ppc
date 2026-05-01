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
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr)
{
    tcc_error_noabort("ppc-link: create_plt_entry not implemented");
    return 0;
}

ST_FUNC void relocate_plt(TCCState *s1)
{
    /* Nothing for now. Real implementation lands with codegen. */
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
        /* 32-bit absolute address (used for data references and the
         * GOT-style indirection paths we don't yet exercise). */
        ppc_link_write32be(ptr, (uint32_t)val);
        return;

    case R_PPC_REL24:
        /* PC-relative 24-bit branch displacement, used by `b` and
         * `bl`. The encoding stores LI in bits 6-29 (mask 0x03fffffc)
         * and treats it as a signed 26-bit byte offset (low 2 bits
         * implicit zero). */
        disp = (int32_t)(val - addr);
        if (disp < -(1 << 25) || disp >= (1 << 25)) {
            tcc_error_noabort("R_PPC_REL24 out of range "
                              "(addr=0x%lx val=0x%lx disp=0x%x)",
                              (unsigned long)addr, (unsigned long)val,
                              (unsigned)disp);
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

    default:
        tcc_error_noabort("ppc-link: relocate type 0x%x not implemented "
                          "(addr=0x%lx val=0x%lx)",
                          type, (unsigned long)addr, (unsigned long)val);
    }
}

#endif /* TARGET_DEFS_ONLY */
