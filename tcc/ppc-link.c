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

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr,
                      addr_t addr, addr_t val)
{
    /* Real implementation lands as codegen grows. For now any relocation
     * arriving here means the gen layer emitted something we don't yet
     * know how to fix up. */
    tcc_error_noabort("ppc-link: relocate type 0x%x not implemented (addr=0x%lx val=0x%lx)",
              type, (unsigned long)addr, (unsigned long)val);
}

#endif /* TARGET_DEFS_ONLY */
