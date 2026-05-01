/*
 * ppc-macho.c -- 32-bit PowerPC Mach-O object-file writer for tcc.
 *
 * Targets Mac OS X 10.4 (Tiger) on PPC G3/G4/G5. Produces MH_OBJECT
 * files (relocatable .o) only. Executables/dylibs are out of scope --
 * we rely on Tiger's gcc-4.0 / ld(1) to do final linking.
 *
 * Replaces ppc-macho-stubs.c. Provides the same exported symbols
 * (tcc_add_macos_sdkpath, macho_load_dll, macho_load_tbd,
 * macho_output_file, macho_tbd_soname, __clear_cache).
 *
 * This file deliberately does NOT include tccmacho.c -- that file is
 * heavily 64-bit-LE oriented (LC_SEGMENT_64, mach_header_64, write32le
 * everywhere, LC_DYLD_CHAINED_FIXUPS, LC_BUILD_VERSION 10.6+...).
 * Porting it incrementally is one route, but for an MH_OBJECT writer
 * the surface area is small enough that a stand-alone implementation
 * is clearer and easier to extend later.
 *
 * What we emit:
 *   - mach_header (32-bit, big-endian, magic 0xfeedface, cputype 18,
 *     filetype MH_OBJECT)
 *   - One unnamed LC_SEGMENT containing all sections (per the
 *     MH_OBJECT convention; see otool -hlv on any gcc-4.0 .o)
 *   - LC_SYMTAB with classic nlist table + string table
 *   - LC_DYSYMTAB (static-link dysymtab: ilocalsym/nlocalsym,
 *     iextdefsym/nextdefsym, iundefsym/nundefsym -- everything else 0)
 *   - Per-section relocations (classic relocation_info entries).
 *
 * Reference output: `gcc-4.0 -c hello.c -o hello.o ; otool -hlv hello.o`
 * on imacg3. Our layout matches that reasonably closely; we omit the
 * empty __picsymbolstub1 section that gcc emits.
 *
 * Endianness: PowerPC Mach-O is always big-endian. All multi-byte
 * fields in the header / load commands / section headers / nlist /
 * relocation_info structs are written via the write*be helpers
 * below. Section *data* is left untouched (tcc's code generator
 * already emits big-endian PPC instructions on a PPC host).
 *
 * Copyright (c) 2026 the tcc-darwin8-ppc contributors. MIT.
 */

#include "tcc.h"
#include <sys/stat.h>   /* chmod */

/* Defined in ppc-gen.c. We share this side table so the Mach-O writer
 * can recover the per-function PIC anchor associated with each
 * R_PPC_HA16_PIC / R_PPC_LO16_PIC reloc emitted by ppc-gen.c. */
ST_FUNC int  ppc_pic_pairs_lookup(int reloc_off);
ST_FUNC void ppc_pic_pairs_reset(void);

/* ====================================================================
 * Mach-O constants
 * ================================================================== */

/* Magic numbers. PPC32 uses MH_MAGIC (NOT MH_MAGIC_64). */
#define MH_MAGIC                0xfeedface

/* CPU types / subtypes. */
#define CPU_TYPE_POWERPC        18
#define CPU_SUBTYPE_POWERPC_ALL 0

/* File types. */
#define MH_OBJECT               1   /* relocatable .o */

/* Header flags. */
#define MH_SUBSECTIONS_VIA_SYMBOLS  0x2000  /* gcc-4.0 sets this on .o */

/* Load commands (32-bit variants). */
#define LC_SEGMENT              0x1
#define LC_SYMTAB               0x2
#define LC_DYSYMTAB             0xb
#define LC_LOAD_DYLIB           0xc
#define LC_LOAD_DYLINKER        0xe
#define LC_UNIXTHREAD           0x5
#define LC_TWOLEVEL_HINTS       0x16

/* MH_EXECUTE filetype + flags. */
#define MH_EXECUTE              2
#define MH_NOUNDEFS             0x1
#define MH_DYLDLINK             0x4
#define MH_BINDATLOAD           0x8
#define MH_TWOLEVEL             0x80

/* PPC thread state (for LC_UNIXTHREAD; matches /usr/include/mach/ppc/thread_status.h). */
#define PPC_THREAD_STATE        1
#define PPC_THREAD_STATE_COUNT  40

/* VM protections. */
#define VM_PROT_READ            0x1
#define VM_PROT_WRITE           0x2
#define VM_PROT_EXECUTE         0x4
#define VM_PROT_DEFAULT         (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)

/* Section types (bottom 8 bits of section.flags). */
#define S_REGULAR                  0x0
#define S_ZEROFILL                 0x1
#define S_CSTRING_LITERALS         0x2
#define S_NON_LAZY_SYMBOL_POINTERS 0x6
#define S_LAZY_SYMBOL_POINTERS     0x7
#define S_SYMBOL_STUBS             0x8
#define S_COALESCED                0xb

/* Section attributes (top 24 bits of section.flags). */
#define S_ATTR_PURE_INSTRUCTIONS    0x80000000
#define S_ATTR_SOME_INSTRUCTIONS    0x00000400

/* Reference flags (low 4 bits of n_desc). */
#define REFERENCE_FLAG_UNDEFINED_NON_LAZY   0
#define REFERENCE_FLAG_UNDEFINED_LAZY       1
#define INDIRECT_SYMBOL_LOCAL               0x80000000
#define INDIRECT_SYMBOL_ABS                 0x40000000

/* nlist n_type values. */
#define N_STAB                  0xe0  /* mask: any stab */
#define N_PEXT                  0x10  /* private external */
#define N_TYPE                  0x0e  /* mask */
#define   N_UNDF                0x0   /*   undefined */
#define   N_ABS                 0x2   /*   absolute */
#define   N_SECT                0xe   /*   defined in section n_sect */
#define   N_INDR                0xa   /*   indirect */
#define N_EXT                   0x01  /* external bit */

#define NO_SECT                 0     /* nlist n_sect when undefined */

/* PPC Mach-O relocation types (see <mach-o/ppc/reloc.h>). */
#define PPC_RELOC_VANILLA       0   /* generic relocation, length encodes width */
#define PPC_RELOC_PAIR          1   /* second half of a HA16/LO16/HI16 pair */
#define PPC_RELOC_BR14          2   /* 14-bit conditional branch */
#define PPC_RELOC_BR24          3   /* 24-bit branch (bl/b) */
#define PPC_RELOC_HI16          4   /* upper 16 bits of an address */
#define PPC_RELOC_LO16          5   /* lower 16 bits */
#define PPC_RELOC_HA16          6   /* upper 16 bits, "adjusted" for sign of low half */
#define PPC_RELOC_LO14          7   /* lower 14 bits (load/store offset) */
#define PPC_RELOC_SECTDIFF      8
#define PPC_RELOC_PB_LA_PTR     9
#define PPC_RELOC_HI16_SECTDIFF 10
#define PPC_RELOC_LO16_SECTDIFF 11
#define PPC_RELOC_HA16_SECTDIFF 12
#define PPC_RELOC_JBSR          13
#define PPC_RELOC_LO14_SECTDIFF 14
#define PPC_RELOC_LOCAL_SECTDIFF 15

/* Scattered-relocation high bit on r_address. */
#define R_SCATTERED             0x80000000

/* ====================================================================
 * Mach-O on-disk structures (32-bit, written big-endian via helpers).
 *
 * NOTE: we never read these structs back as raw bytes from the file we
 * just wrote; we always go field-by-field through the write*be
 * helpers. The structs exist so we have a place to compute sizeof().
 * ================================================================== */

struct macho_header_32 {
    uint32_t magic;
    int32_t  cputype;
    int32_t  cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
};

struct macho_segment_command_32 {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct macho_section_32 {
    char     sectname[16];
    char     segname[16];
    uint32_t addr;
    uint32_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
};

struct macho_symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

struct macho_dysymtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym;
    uint32_t nlocalsym;
    uint32_t iextdefsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
    uint32_t nundefsym;
    uint32_t tocoff;
    uint32_t ntoc;
    uint32_t modtaboff;
    uint32_t nmodtab;
    uint32_t extrefsymoff;
    uint32_t nextrefsyms;
    uint32_t indirectsymoff;
    uint32_t nindirectsyms;
    uint32_t extreloff;
    uint32_t nextrel;
    uint32_t locreloff;
    uint32_t nlocrel;
};

/* nlist entry, 12 bytes. */
struct macho_nlist {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    int16_t  n_desc;
    uint32_t n_value;
};

#define MACHO_HEADER_SIZE       28
#define MACHO_SEGMENT_CMD_SIZE  56
#define MACHO_SECTION_SIZE      68
#define MACHO_SYMTAB_CMD_SIZE   24
#define MACHO_DYSYMTAB_CMD_SIZE 80
#define MACHO_NLIST_SIZE        12
#define MACHO_RELOC_SIZE        8

/* ====================================================================
 * Big-endian write helpers.
 *
 * We write to a growing in-memory buffer to make patching offsets
 * straightforward (load commands need to know section file offsets
 * computed later in the layout pass). Buffer is realloc'd as needed.
 * ================================================================== */

typedef struct {
    unsigned char *buf;
    size_t         len;
    size_t         cap;
} obuf;

static void obuf_reserve(obuf *b, size_t want)
{
    if (b->len + want <= b->cap)
        return;
    while (b->cap < b->len + want)
        b->cap = b->cap ? b->cap * 2 : 4096;
    b->buf = tcc_realloc(b->buf, b->cap);
}

static void obuf_put(obuf *b, const void *data, size_t n)
{
    obuf_reserve(b, n);
    memcpy(b->buf + b->len, data, n);
    b->len += n;
}

static void obuf_zero(obuf *b, size_t n)
{
    obuf_reserve(b, n);
    memset(b->buf + b->len, 0, n);
    b->len += n;
}

static void put8(obuf *b, uint8_t v)
{
    obuf_reserve(b, 1);
    b->buf[b->len++] = v;
}

static void put32be(obuf *b, uint32_t v)
{
    obuf_reserve(b, 4);
    b->buf[b->len++] = (v >> 24) & 0xff;
    b->buf[b->len++] = (v >> 16) & 0xff;
    b->buf[b->len++] = (v >> 8) & 0xff;
    b->buf[b->len++] = v & 0xff;
}

/* ====================================================================
 * Section mapping: tcc internal sections -> Mach-O (segname, sectname)
 *
 * We only emit sections with SHF_ALLOC set (text/data/rodata/bss);
 * tcc's bookkeeping sections (.symtab, .strtab, .rel.text, ...) are
 * handled separately or skipped.
 * ================================================================== */

struct macho_secmap {
    Section    *elf;        /* tcc's Section (read-only); NULL for synthesized */
    const char *segname;    /* "__TEXT", "__DATA" */
    const char *sectname;   /* "__text", "__data", ... */
    uint32_t    flags;      /* Mach-O section flags */
    uint32_t    align_log2; /* 2^align_log2 */
    uint32_t    addr;       /* virtual address (set during layout) */
    uint32_t    offset;     /* file offset (set during layout) */
    uint32_t    size;       /* section size (= elf->data_offset) */
    uint32_t    reloff;     /* file offset of relocations */
    uint32_t    nreloc;     /* number of relocation entries */
    uint32_t    reserved1;  /* (indirect symbol table index for stubs/la_ptrs) */
    uint32_t    reserved2;  /* (stub size in bytes for S_SYMBOL_STUBS) */
    int         msect_no;   /* 1-based Mach-O section index */
    /* For synthesized sections (stubs / la_symbol_ptrs / nl_symbol_ptrs),
     * data is owned by us and freed at cleanup. For ELF-backed sections,
     * data is NULL and we read from elf->data instead. */
    unsigned char *data;
    int         is_stub;    /* 1 if this is __picsymbolstub1            */
    int         is_la_ptr;  /* 1 if this is __la_symbol_ptr             */
    int         is_nl_ptr;  /* 1 if this is __nl_symbol_ptr             */
};

/* Per-output extern-stub table. One entry per unique external function
 * called via R_PPC_REL24 from any section in this object file. */
struct extern_stub {
    int      elfsym_idx;    /* index into s1->symtab */
    uint32_t addr;          /* stub VA in __picsymbolstub1 (set at layout) */
    uint32_t la_ptr_addr;   /* la_symbol_ptr VA (set at layout) */
    int      machsym_idx;   /* idx into emitted nlist (set after build_symtab) */
};

/* Per-output non-lazy pointer table. One entry per unique external
 * data symbol referenced via R_PPC_HA16_PIC / R_PPC_LO16_PIC. The
 * synthesized __DATA,__nl_symbol_ptr section reserves one 4-byte slot
 * per entry; ld writes the symbol's resolved address into the slot at
 * static-link time (no lazy binding -- non-lazy semantics). */
struct extern_nlptr {
    int      elfsym_idx;
    uint32_t slot_addr;     /* __nl_symbol_ptr slot VA (set at layout) */
    int      machsym_idx;
};

/* Map a tcc section to its Mach-O (segname, sectname, flags).
 * Returns 1 if the section should be emitted, 0 to skip. */
static int classify_section(Section *s, const char **segname,
                            const char **sectname, uint32_t *flags)
{
    const char *n = s->name;

    /* Skip non-allocated and bookkeeping sections. */
    if (!(s->sh_flags & SHF_ALLOC))
        return 0;
    if (s->sh_type != SHT_PROGBITS && s->sh_type != SHT_NOBITS)
        return 0;
    /* Skip tcc's .common section (handled by resolve_common_syms). */
    if (s->sh_num == SHN_COMMON)
        return 0;

    if (!strcmp(n, ".text")) {
        *segname = "__TEXT";
        *sectname = "__text";
        *flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
        return 1;
    }
    if (!strcmp(n, ".data")) {
        *segname = "__DATA";
        *sectname = "__data";
        *flags = S_REGULAR;
        return 1;
    }
    if (!strcmp(n, ".bss")) {
        *segname = "__DATA";
        *sectname = "__bss";
        *flags = S_ZEROFILL;
        return 1;
    }
    if (!strcmp(n, ".rodata") || !strcmp(n, ".data.ro")) {
        *segname = "__TEXT";
        *sectname = "__const";
        *flags = S_REGULAR;
        return 1;
    }
    /* Sections that look like .data.foo / .text.foo / .rodata.foo:
     * fold into the parent. tcc rarely emits these for an .o, but
     * be defensive. */
    if (!strncmp(n, ".text.", 6)) {
        *segname = "__TEXT"; *sectname = "__text";
        *flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
        return 1;
    }
    if (!strncmp(n, ".data.", 6)) {
        *segname = "__DATA"; *sectname = "__data"; *flags = S_REGULAR;
        return 1;
    }
    if (!strncmp(n, ".rodata.", 8)) {
        *segname = "__TEXT"; *sectname = "__const"; *flags = S_REGULAR;
        return 1;
    }
    /* Unknown allocated section: skip with a warning-but-don't-fail
     * approach. (Tests can grep stderr to find unhandled cases.) */
    return 0;
}

static uint32_t addralign_log2(int align)
{
    /* tcc stores sh_addralign as the byte alignment (1, 2, 4, ...). */
    uint32_t l = 0;
    if (align <= 1) return 0;
    while ((1 << l) < align) l++;
    return l;
}

/* ====================================================================
 * Extern-stub collection.
 *
 * Walk all sections once; for every R_PPC_REL24 relocation whose target
 * is an undefined external symbol, register that symbol in a deduped
 * table. We later synthesize one __picsymbolstub1 entry and one
 * __la_symbol_ptr slot per registered external.
 * ================================================================== */
static int collect_extern_stubs(TCCState *s1,
                                struct extern_stub **out_stubs,
                                int **out_stub_for_elfsym)
{
    int nsyms = s1->symtab->data_offset / sizeof(ElfW(Sym));
    int *stub_for = tcc_mallocz(nsyms * sizeof(int));
    struct extern_stub *stubs = NULL;
    int nstubs = 0, capstubs = 0;
    int i, k;

    for (i = 0; i < nsyms; i++) stub_for[i] = -1;

    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        Section *sr;
        int nrel;
        if (!s) continue;
        sr = s->reloc;
        if (!sr) continue;
        nrel = sr->data_offset / sizeof(ElfW_Rel);
        for (k = 0; k < nrel; k++) {
            ElfW_Rel *rel = (ElfW_Rel *)sr->data + k;
            int type = ELFW(R_TYPE)(rel->r_info);
            int symidx = ELFW(R_SYM)(rel->r_info);
            ElfW(Sym) *esym;
            if (symidx <= 0 || symidx >= nsyms) continue;
            if (type != R_PPC_REL24) continue;
            esym = (ElfW(Sym) *)s1->symtab->data + symidx;
            if (esym->st_shndx != SHN_UNDEF) continue;
            if (stub_for[symidx] >= 0) continue;
            if (nstubs >= capstubs) {
                capstubs = capstubs ? capstubs * 2 : 8;
                stubs = tcc_realloc(stubs,
                                    capstubs * sizeof(struct extern_stub));
            }
            stubs[nstubs].elfsym_idx = symidx;
            stubs[nstubs].addr = 0;
            stubs[nstubs].la_ptr_addr = 0;
            stubs[nstubs].machsym_idx = -1;
            stub_for[symidx] = nstubs;
            nstubs++;
        }
    }
    *out_stubs = stubs;
    *out_stub_for_elfsym = stub_for;
    return nstubs;
}

/* Collect the unique set of external data symbols referenced via
 * R_PPC_HA16_PIC / R_PPC_LO16_PIC across all sections. Symbol order
 * matches first-encountered order (deterministic w.r.t. our reloc
 * walk). Returns the count and fills *out_nlptrs / *out_nl_for_elfsym
 * (both caller-owned). */
static int collect_extern_nlptrs(TCCState *s1,
                                 struct extern_nlptr **out_nlptrs,
                                 int **out_nl_for_elfsym)
{
    int nsyms = s1->symtab->data_offset / sizeof(ElfW(Sym));
    int *nl_for = tcc_mallocz(nsyms * sizeof(int));
    struct extern_nlptr *nlptrs = NULL;
    int n_nl = 0, cap_nl = 0;
    int i, k;

    for (i = 0; i < nsyms; i++) nl_for[i] = -1;

    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        Section *sr;
        int nrel;
        if (!s) continue;
        sr = s->reloc;
        if (!sr) continue;
        nrel = sr->data_offset / sizeof(ElfW_Rel);
        for (k = 0; k < nrel; k++) {
            ElfW_Rel *rel = (ElfW_Rel *)sr->data + k;
            int type = ELFW(R_TYPE)(rel->r_info);
            int symidx = ELFW(R_SYM)(rel->r_info);
            ElfW(Sym) *esym;
            if (symidx <= 0 || symidx >= nsyms) continue;
            if (type != R_PPC_HA16_PIC && type != R_PPC_LO16_PIC)
                continue;
            esym = (ElfW(Sym) *)s1->symtab->data + symidx;
            if (esym->st_shndx != SHN_UNDEF) continue;
            if (nl_for[symidx] >= 0) continue;
            if (n_nl >= cap_nl) {
                cap_nl = cap_nl ? cap_nl * 2 : 8;
                nlptrs = tcc_realloc(nlptrs,
                                     cap_nl * sizeof(struct extern_nlptr));
            }
            nlptrs[n_nl].elfsym_idx = symidx;
            nlptrs[n_nl].slot_addr = 0;
            nlptrs[n_nl].machsym_idx = -1;
            nl_for[symidx] = n_nl;
            n_nl++;
        }
    }
    *out_nlptrs = nlptrs;
    *out_nl_for_elfsym = nl_for;
    return n_nl;
}

/* Build the 32-byte PIC stub for a single external function.
 * `stub_addr` is the VA of the stub (= start of the 8-instruction
 * sequence). `la_ptr_addr` is the VA of the corresponding lazy-bind
 * pointer slot. The instructions are pre-filled with the displacement
 * `(la_ptr_addr - anchor)` split into HA16/LO16 halves; the linker
 * will recompute these via SECTDIFF relocs if it relocates either
 * section. */
static void emit_pic_stub_bytes(unsigned char *out, uint32_t stub_addr,
                                uint32_t la_ptr_addr)
{
    uint32_t anchor = stub_addr + 8;     /* address of mflr r11 */
    int32_t  delta = (int32_t)(la_ptr_addr - anchor);
    uint16_t ha = (uint16_t)((delta + 0x8000) >> 16);
    uint16_t lo = (uint16_t)(delta & 0xffff);
    static const uint32_t prefix[] = {
        0x7c0802a6,   /* mflr r0                */
        0x429f0005,   /* bcl  20, 31, .+4       */
        0x7d6802a6,   /* mflr r11               */
    };
    static const uint32_t suffix[] = {
        0x7c0803a6,   /* mtlr r0                */
        /* addis r11,r11,ha and lwz r12,lo(r11) inserted between */
        0x7d8903a6,   /* mtctr r12              */
        0x4e800420,   /* bctr                   */
    };
    uint32_t instrs[8];
    int i;

    instrs[0] = prefix[0];
    instrs[1] = prefix[1];
    instrs[2] = prefix[2];
    instrs[3] = 0x3d6b0000u | ha;        /* addis r11,r11,ha */
    instrs[4] = suffix[0];               /* mtlr r0 */
    /* lwzu r12,lo(r11) -- load *and* update r11 to la_ptr address.
     * dyld_stub_binding_helper expects r11 to hold the la_ptr slot
     * it should overwrite with the bound function address. */
    instrs[5] = 0x858b0000u | lo;
    instrs[6] = suffix[1];               /* mtctr r12 */
    instrs[7] = suffix[2];               /* bctr */
    for (i = 0; i < 8; i++) {
        out[i*4 + 0] = (instrs[i] >> 24) & 0xff;
        out[i*4 + 1] = (instrs[i] >> 16) & 0xff;
        out[i*4 + 2] = (instrs[i] >>  8) & 0xff;
        out[i*4 + 3] =  instrs[i]        & 0xff;
    }
}

/* ====================================================================
 * Relocation translation: ELF (R_PPC_*) -> Mach-O (PPC_RELOC_*)
 *
 * Mach-O classic reloc record (relocation_info, 8 bytes total):
 *   uint32_t r_address;
 *   uint32_t bitfield: r_symbolnum:24, r_pcrel:1, r_length:2,
 *                      r_extern:1, r_type:4;
 * On big-endian hosts with our hand-written serializer, we pack the
 * second word ourselves and write it via put32be().
 *
 * For HA16/LO16 relocations the Mach-O ABI requires a *pair* entry
 * immediately after, encoding the "other half" of the address so the
 * linker can compute the carry-correction. r_address of the PAIR
 * entry holds that other half (not a real address), and r_symbolnum
 * is 0xffffff.
 * ================================================================== */

/* Emit a scattered relocation entry (8 bytes) into b. The first word
 * carries the high R_SCATTERED bit + pcrel/length/type/address; the
 * second word carries the symbol's value (its address). */
static void emit_scattered(obuf *b, int pcrel, int length, int type,
                           uint32_t address, uint32_t value)
{
    uint32_t w0 = R_SCATTERED
                | ((pcrel & 1)     << 30)
                | ((length & 3)    << 28)
                | ((type & 0xf)    << 24)
                | (address & 0xffffff);
    put32be(b, w0);
    put32be(b, value);
}

/* Pack a non-scattered Mach-O relocation into the second 32-bit word.
 *
 * Apple's struct relocation_info uses C bitfields. On big-endian PPC
 * the *first* declared field occupies the *most-significant* bits, so
 * the on-disk layout (read as a big-endian uint32_t) is:
 *
 *   r_symbolnum : bits 31..8  (24 bits, top)
 *   r_pcrel     : bit  7
 *   r_length    : bits 6..5
 *   r_extern    : bit  4
 *   r_type      : bits 3..0   (bottom)
 *
 * (This is the opposite of the natural little-endian packing and was
 * the source of subtle bugs early in the port -- see "0x35000002"
 * mystery in the dev log.)
 */
static uint32_t pack_reloc_word(uint32_t r_symbolnum, int r_pcrel,
                                int r_length, int r_extern, int r_type)
{
    return ((r_symbolnum & 0x00ffffff) << 8)
         | ((r_pcrel & 1) << 7)
         | ((r_length & 3) << 5)
         | ((r_extern & 1) << 4)
         | ((r_type & 0xf));
}

/* Bundled per-output context handed to emit_section_relocs(). Bundling
 * keeps that helper at <=8 scalar parameters (a hard limit of tcc's
 * own PPC backend, since System V PPC passes only 8 GPRs by reg). */
struct reloc_ctx {
    TCCState               *s1;
    struct macho_secmap    *smap;
    int                     nsec;
    int                    *sym_xlat;
    int                    *sym_isext;
    int                    *xlat_present;
    int                    *stub_for_elfsym;
    int                     stub_msect_no;
    /* Non-lazy-pointer routing (for PIC data refs). */
    int                    *nl_for_elfsym;     /* sym -> nl_ptr index, or -1 */
    struct extern_nlptr    *nlptrs;            /* by nl_ptr index */
    int                     n_nlptrs;
};

/* Emit Mach-O relocations for one tcc section into b. Returns the
 * number of relocation_info records written. The mapping from tcc
 * symtab indices to Mach-O symtab indices is given by ctx->sym_xlat[].
 * For local (non-extern) relocs, the Mach-O r_symbolnum field holds
 * the *section* number (1-based) rather than a symtab index, so we
 * also accept a sym->elf-section table.
 *
 * `ctx->stub_for_elfsym[]` (may be NULL) and `ctx->stub_msect_no` route
 * external R_PPC_REL24 calls through the synthesized __picsymbolstub1
 * section: the reloc becomes a *section-relative* (extern_bit=0) BR24
 * against the stub section, with the in-instruction displacement
 * already encoded by the caller's pre-relocation pass. */
static int emit_section_relocs(obuf *b, Section *s, struct reloc_ctx *ctx)
{
    TCCState              *s1   = ctx->s1;
    struct macho_secmap   *smap = ctx->smap;
    int                    nsec = ctx->nsec;
    int                   *sym_xlat        = ctx->sym_xlat;
    int                   *sym_isext       = ctx->sym_isext;
    int                   *xlat_present    = ctx->xlat_present;
    int                   *stub_for_elfsym = ctx->stub_for_elfsym;
    int                    stub_msect_no   = ctx->stub_msect_no;
    int                   *nl_for_elfsym   = ctx->nl_for_elfsym;
    struct extern_nlptr   *nlptrs          = ctx->nlptrs;
    int i, count = 0;
    int nrel;
    /* Section base address of `s` -- needed because anchor offsets are
     * relative to text start, but SECTDIFF relocs need absolute VAs. */
    uint32_t sec_base = 0;
    for (i = 0; i < nsec; i++) {
        if (smap[i].elf == s) { sec_base = smap[i].addr; break; }
    }

    if (!s->reloc)
        return 0;
    nrel = s->reloc->data_offset / sizeof(ElfW_Rel);
    for (i = 0; i < nrel; i++) {
        ElfW_Rel *rel = (ElfW_Rel *)s->reloc->data + i;
        int type = ELFW(R_TYPE)(rel->r_info);
        int elfsym = ELFW(R_SYM)(rel->r_info);
        int msym;
        int extern_bit;
        uint32_t addr = (uint32_t)rel->r_offset;
        uint32_t other_half = 0;
        int emit_pair = 0;
        int mtype, mlength, mpcrel;
        ElfW(Sym) *esym;

        if (elfsym <= 0 || !xlat_present[elfsym]) {
            /* Skip relocs against symbols we couldn't translate. */
            continue;
        }
        esym = (ElfW(Sym) *)s1->symtab->data + elfsym;
        extern_bit = sym_isext[elfsym] ? 1 : 0;

        /* External BR24: retarget to the stub section. */
        if (extern_bit && type == R_PPC_REL24
            && stub_for_elfsym && stub_for_elfsym[elfsym] >= 0
            && stub_msect_no > 0) {
            extern_bit = 0;
            msym = stub_msect_no;
            put32be(b, addr);
            put32be(b, pack_reloc_word(msym, 1, 2, 0, PPC_RELOC_BR24));
            count++;
            continue;
        }

        /* PIC HA16/LO16 reference: emit scattered HA16/LO16 SECTDIFF
         * + PAIR. Two cases:
         *   (a) symbol is undef external -> r_value = __nl_symbol_ptr
         *       slot VA (the addis/lwz pair stays as load-from-slot).
         *   (b) symbol is defined locally -> r_value = symbol's VA
         *       directly (the lwz was rewritten to addi by the
         *       pre-relocation pass; we now compute the symbol's
         *       address inline without going through any slot).
         * In both cases the PAIR's r_value is the per-function PIC
         * anchor VA, and the PAIR's r_address carries the OTHER half
         * of the displacement (slot/sym - anchor) so ld can recompute
         * after section relocation. */
        if (type == R_PPC_HA16_PIC || type == R_PPC_LO16_PIC) {
            int anchor_off = ppc_pic_pairs_lookup((int)addr);
            uint32_t target_va = 0;
            uint32_t anchor_va;
            int32_t  delta;
            uint16_t lo, ha;

            if (anchor_off < 0) {
                tcc_error_noabort("ppc-macho: missing PIC anchor for "
                                  "reloc at 0x%x", (unsigned)addr);
                continue;
            }
            if (extern_bit
                && nl_for_elfsym && nl_for_elfsym[elfsym] >= 0) {
                /* External: target is the __nl_symbol_ptr slot. */
                target_va = nlptrs[nl_for_elfsym[elfsym]].slot_addr;
            } else if (!extern_bit) {
                /* Locally-defined: target is the symbol itself. */
                int j;
                for (j = 0; j < nsec; j++) {
                    if (smap[j].elf
                        && smap[j].elf->sh_num == esym->st_shndx) {
                        target_va = smap[j].addr + (uint32_t)esym->st_value;
                        break;
                    }
                }
                if (j == nsec) continue;
            } else {
                /* External but no slot? Skip -- shouldn't normally
                 * happen because collect_extern_nlptrs() registers a
                 * slot for every PIC reloc against an undef extern. */
                continue;
            }
            anchor_va = sec_base + (uint32_t)anchor_off;
            delta = (int32_t)(target_va - anchor_va);
            lo = (uint16_t)(delta & 0xffff);
            ha = (uint16_t)((delta + 0x8000) >> 16);

            if (type == R_PPC_HA16_PIC) {
                emit_scattered(b, 0, 2, PPC_RELOC_HA16_SECTDIFF,
                               addr, target_va);
                emit_scattered(b, 0, 2, PPC_RELOC_PAIR,
                               (uint32_t)lo, anchor_va);
            } else {
                emit_scattered(b, 0, 2, PPC_RELOC_LO16_SECTDIFF,
                               addr, target_va);
                emit_scattered(b, 0, 2, PPC_RELOC_PAIR,
                               (uint32_t)ha, anchor_va);
            }
            count += 2;
            continue;
        }

        if (extern_bit) {
            /* External: r_symbolnum is the Mach-O symtab index. */
            msym = sym_xlat[elfsym];
        } else {
            /* Local: r_symbolnum is the 1-based Mach-O section
             * number. Look it up via the elf->msect mapping. */
            int j;
            msym = 0;
            for (j = 0; j < nsec; j++) {
                if (smap[j].elf
                    && smap[j].elf->sh_num == esym->st_shndx) {
                    msym = smap[j].msect_no;
                    break;
                }
            }
            if (msym == 0)
                continue;  /* target section not emitted; skip */
        }

        /* For HA16/LO16/HI16 PAIR entries, the second record carries
         * the OTHER half of the symbol's resolved value so ld can do
         * the carry-correction across the 16-bit split. For a defined
         * symbol we already know its provisional VA in our layout; use
         * it. For an undef extern we have to leave it 0 (ld will
         * resolve the address but the addend contribution from us is
         * empty in tcc's Rel-without-addend format). */
        {
            uint32_t sym_val_for_pair = 0;
            if (!extern_bit) {
                int j;
                for (j = 0; j < nsec; j++) {
                    if (smap[j].elf
                        && smap[j].elf->sh_num == esym->st_shndx) {
                        sym_val_for_pair = smap[j].addr
                                         + (uint32_t)esym->st_value;
                        break;
                    }
                }
            }

            switch (type) {
            case R_PPC_ADDR32:
                mtype = PPC_RELOC_VANILLA;
                mlength = 2;     /* 4 bytes */
                mpcrel = 0;
                break;
            case R_PPC_REL24:
                mtype = PPC_RELOC_BR24;
                mlength = 2;
                mpcrel = 1;
                break;
            case R_PPC_ADDR16_HA:
                mtype = PPC_RELOC_HA16;
                mlength = 2;
                mpcrel = 0;
                emit_pair = 1;
                /* PAIR's r_address holds the LO half of the symbol's
                 * resolved address (so ld can apply the +1 if bit 15
                 * is set). */
                other_half = sym_val_for_pair & 0xffff;
                break;
            case R_PPC_ADDR16_HI:
                mtype = PPC_RELOC_HI16;
                mlength = 2;
                mpcrel = 0;
                emit_pair = 1;
                other_half = sym_val_for_pair & 0xffff;
                break;
            case R_PPC_ADDR16_LO:
                mtype = PPC_RELOC_LO16;
                mlength = 2;
                mpcrel = 0;
                emit_pair = 1;
                /* PAIR's r_address holds the HA half. */
                other_half = ((sym_val_for_pair + 0x8000) >> 16) & 0xffff;
                break;
            default:
                /* Unsupported reloc: skip rather than corrupt the .o. */
                continue;
            }
        }

        /* Main entry. */
        put32be(b, addr);
        put32be(b, pack_reloc_word(msym, mpcrel, mlength, extern_bit, mtype));
        count++;

        if (emit_pair) {
            /* PAIR entry: r_address holds the other half, r_symbolnum
             * is 0xffffff per spec. */
            put32be(b, other_half);
            put32be(b, pack_reloc_word(0xffffff, 0, mlength, 0, PPC_RELOC_PAIR));
            count++;
        }
    }
    return count;
}

/* ====================================================================
 * Symbol table construction.
 *
 * We walk tcc's s1->symtab (Elf32_Sym entries) and emit a Mach-O
 * nlist for each. The classic Mach-O symbol table is partitioned as
 * three contiguous groups:
 *   [ local syms ] [ external defined syms ] [ external undefined syms ]
 *
 * LC_DYSYMTAB then names each group's offset and count.
 *
 * We also build a sym_xlat[] mapping from elf-sym-index -> mach-o
 * sym-index for use in relocation translation.
 * ================================================================== */

struct symtab_build {
    obuf nlist;     /* concatenated nlist[] entries */
    obuf strtab;    /* name strings */
    int  nlocal;
    int  nextdef;
    int  nundef;
    int  ntotal;
    int *xlat;       /* elf sym index -> mach-o sym index */
    int *isext;      /* elf sym index -> 1 if external (for relocs) */
    int *xlat_present;  /* elf sym index -> 1 if emitted */
};

static uint32_t add_string(obuf *st, const char *s)
{
    uint32_t off = st->len;
    obuf_put(st, s, strlen(s) + 1);
    return off;
}

/* Classify an Elf symbol. Returns:
 *   0 = local (defined, not external)
 *   1 = external defined
 *   2 = external undefined
 *  -1 = skip (e.g. STT_SECTION, STT_FILE, empty name)
 *
 * Also fills *out_n_sect with the 1-based Mach-O section number, or
 * 0 if undefined / absolute. */
static int classify_sym(TCCState *s1, ElfW(Sym) *sym,
                        struct macho_secmap *smap, int nsec,
                        int *out_n_sect, int *out_n_type)
{
    int bind = ELFW(ST_BIND)(sym->st_info);
    int stype = ELFW(ST_TYPE)(sym->st_info);
    const char *name = (char *)s1->symtab->link->data + sym->st_name;
    int j;

    if (stype == STT_SECTION || stype == STT_FILE)
        return -1;
    if (!name || !name[0])
        return -1;

    if (sym->st_shndx == SHN_UNDEF) {
        *out_n_sect = NO_SECT;
        *out_n_type = N_EXT | N_UNDF;
        return 2;
    }
    if (sym->st_shndx == SHN_ABS) {
        *out_n_sect = NO_SECT;
        *out_n_type = (bind == STB_GLOBAL ? N_EXT : 0) | N_ABS;
        return (bind == STB_GLOBAL) ? 1 : 0;
    }
    if (sym->st_shndx == SHN_COMMON) {
        /* Treat common as undefined external (linker resolves). */
        *out_n_sect = NO_SECT;
        *out_n_type = N_EXT | N_UNDF;
        return 2;
    }

    /* Defined in some section. Map elf section index to Mach-O. */
    *out_n_sect = NO_SECT;
    for (j = 0; j < nsec; j++) {
        if (smap[j].elf->sh_num == sym->st_shndx) {
            *out_n_sect = smap[j].msect_no;
            break;
        }
    }
    if (*out_n_sect == NO_SECT) {
        /* Symbol points to a section we're not emitting -- skip it. */
        return -1;
    }

    *out_n_type = (bind == STB_GLOBAL ? N_EXT : 0) | N_SECT;
    return (bind == STB_GLOBAL) ? 1 : 0;
}

/* Build the Mach-O symbol/string tables and xlat array from
 * s1->symtab. Symbols are emitted in three passes (locals, extdef,
 * undef) so that LC_DYSYMTAB's contiguous-group invariant holds.
 *
 * `stub_for_elfsym[]` (may be NULL) is the same dedup map produced by
 * collect_extern_stubs(); when an undef external symbol has a stub
 * registered we set its n_desc to REFERENCE_FLAG_UNDEFINED_LAZY so the
 * dynamic linker knows to bind it via the lazy pointer slot. */
static void build_symtab(TCCState *s1, struct symtab_build *sb,
                         struct macho_secmap *smap, int nsec,
                         int *stub_for_elfsym)
{
    int nsyms = s1->symtab->data_offset / sizeof(ElfW(Sym));
    int pass, i;

    sb->xlat = tcc_mallocz(nsyms * sizeof(int));
    sb->isext = tcc_mallocz(nsyms * sizeof(int));
    sb->xlat_present = tcc_mallocz(nsyms * sizeof(int));

    /* String table starts with a single NUL byte (per Mach-O
     * convention; n_strx == 0 means "no name"). */
    obuf_zero(&sb->strtab, 1);

    for (pass = 0; pass < 3; pass++) {
        for (i = 1; i < nsyms; i++) {  /* skip index 0 (UNDEF) */
            ElfW(Sym) *sym = (ElfW(Sym) *)s1->symtab->data + i;
            int n_sect = NO_SECT, n_type = 0;
            int kind = classify_sym(s1, sym, smap, nsec, &n_sect, &n_type);
            const char *name;
            uint32_t strx, value;
            int16_t n_desc = 0;

            if (kind != pass)
                continue;

            name = (char *)s1->symtab->link->data + sym->st_name;
            /* tcc's frontend honors s1->leading_underscore (set true
             * for Mach-O targets in libtcc.c), so names in s1->symtab
             * already start with '_' for ordinary C symbols. We just
             * copy the name verbatim. */
            strx = add_string(&sb->strtab, name);

            /* Compute n_value. tcc's symtab keeps st_value as the
             * symbol's offset within its defining section (we did
             * NOT call relocate_syms, which would have folded sh_addr
             * into st_value). To produce the segment-relative address
             * Mach-O wants, we add our layout's section base. */
            value = (uint32_t)sym->st_value;
            if (kind != 2 && n_sect != NO_SECT) {
                int idx = n_sect - 1;
                value += smap[idx].addr;
            }

            /* Lazy-bound externals: dyld looks for n_desc bit 0 set to
             * mean "bind via the la_symbol_ptr slot, not eagerly". */
            if (kind == 2 && stub_for_elfsym && stub_for_elfsym[i] >= 0)
                n_desc = REFERENCE_FLAG_UNDEFINED_LAZY;

            /* Emit nlist entry (12 bytes, big-endian). */
            obuf_reserve(&sb->nlist, MACHO_NLIST_SIZE);
            {
                obuf *b = &sb->nlist;
                /* n_strx (4) */
                b->buf[b->len + 0] = (strx >> 24) & 0xff;
                b->buf[b->len + 1] = (strx >> 16) & 0xff;
                b->buf[b->len + 2] = (strx >> 8) & 0xff;
                b->buf[b->len + 3] = strx & 0xff;
                /* n_type (1), n_sect (1), n_desc (2 BE), n_value (4 BE) */
                b->buf[b->len + 4] = n_type;
                b->buf[b->len + 5] = n_sect;
                b->buf[b->len + 6] = (n_desc >> 8) & 0xff;
                b->buf[b->len + 7] = n_desc & 0xff;
                b->buf[b->len + 8] = (value >> 24) & 0xff;
                b->buf[b->len + 9] = (value >> 16) & 0xff;
                b->buf[b->len + 10] = (value >> 8) & 0xff;
                b->buf[b->len + 11] = value & 0xff;
                b->len += MACHO_NLIST_SIZE;
            }

            sb->xlat[i] = sb->ntotal;
            /* In Mach-O reloc records, the "extern" bit means "the
             * symbol is *unresolved* in this object; the linker will
             * bind it later". Defined globals are NOT extern -- their
             * relocs use the section index. Only kind==2 (truly
             * undefined externals) deserve the extern bit. */
            sb->isext[i] = (kind == 2) ? 1 : 0;
            sb->xlat_present[i] = 1;
            sb->ntotal++;

            if (pass == 0) sb->nlocal++;
            else if (pass == 1) sb->nextdef++;
            else sb->nundef++;
        }
    }
}

static void free_symtab(struct symtab_build *sb)
{
    tcc_free(sb->nlist.buf);
    tcc_free(sb->strtab.buf);
    tcc_free(sb->xlat);
    tcc_free(sb->isext);
    tcc_free(sb->xlat_present);
}

/* ====================================================================
 * Main entry point: macho_output_file
 *
 * Layout:
 *   header
 *   load commands (LC_SEGMENT, LC_SYMTAB, LC_DYSYMTAB)
 *   section data (aligned)
 *   relocations (per section)
 *   symbol table
 *   string table
 *
 * We do this in two passes: first compute sizes & offsets, then
 * actually serialize.
 * ================================================================== */

/* ====================================================================
 * MH_EXECUTE writer
 *
 * Writes a self-contained Mach-O executable. Layout:
 *   __PAGEZERO  vmaddr 0           vmsize 0x1000     no file content
 *   __TEXT      vmaddr 0x1000      file 0..N
 *                 header+cmds, __text + crt-shim, __const, __picsymbolstub1
 *   __DATA      vmaddr next-page   file N..M
 *                 __data, __nl_symbol_ptr (function ptrs for libSystem),
 *                 __bss
 *   __LINKEDIT  vmaddr next-page   file M..
 *                 indirect symtab, symtab, strtab
 *
 * Plus LC_LOAD_DYLINKER, LC_LOAD_DYLIB libSystem, LC_SYMTAB,
 * LC_DYSYMTAB, LC_UNIXTHREAD with srr0 = entry.
 *
 * The crt-shim is a 4-instruction tail appended to .text:
 *     bl   _main              ; r3 = main's return value
 *     li   r0, 1              ; SYS_exit
 *     sc                      ; trap to kernel
 *     trap                    ; defensive
 * Entry point (LC_UNIXTHREAD srr0) is set to the shim.
 *
 * Stubs use NON-LAZY binding: each stub is 4 instructions
 * (lis/lwz/mtctr/bctr) that loads the function ptr from a __nl_symbol_ptr
 * slot; dyld resolves all such slots at load time.
 * ================================================================== */

#define EXE_PAGEZERO_VMSIZE     0x1000
#define EXE_TEXT_VMADDR_BASE    0x1000
#define EXE_PAGE_SIZE           0x1000

/* Helper: locate a symbol's offset within the .text section.
 * Searches with and without the leading underscore. */
static int exe_find_text_sym_offset(TCCState *s1, Section *text,
                                    const char *want)
{
    int nsyms = s1->symtab->data_offset / sizeof(ElfW(Sym));
    int wantlen = (int)strlen(want);
    int i;
    for (i = 1; i < nsyms; i++) {
        ElfW(Sym) *sym = &((ElfW(Sym) *)s1->symtab->data)[i];
        const char *name = (char *)s1->symtab->link->data + sym->st_name;
        if (sym->st_shndx != text->sh_num || !name) continue;
        if (!strcmp(name, want))
            return (int)sym->st_value;
        if (name[0] == '_' && !strcmp(name + 1, want))
            return (int)sym->st_value;
        /* and with both names having underscores */
        if (want[0] == '_' && !strcmp(name, want + 1))
            return (int)sym->st_value;
        (void)wantlen;
    }
    return -1;
}

static int exe_find_main_offset(TCCState *s1, Section *text)
{
    return exe_find_text_sym_offset(s1, text, "_main");
}

/* Helper: emit the crt-shim into `text` after the user code. The shim
 * does the bare-minimum stack setup the Apple PPC ABI requires
 * before any user-emitted function runs, then tail-calls a small
 * C-defined helper (`__tcc_start_main`, auto-injected by
 * tcc_output_file in tccelf.c) that initializes libSystem globals
 * and runs main.
 *
 *   addi r1, r1, -4           ; r1 was at argc on entry
 *   rlwinm r1, r1, 0, 0, 26   ; align r1 down to 32 bytes
 *   li   r0, 0
 *   stw  r0, 0(r1)            ; clear back-chain word
 *   stwu r1, -64(r1)          ; reserve a linkage frame
 *   bl   __tcc_start_main     ; takes argc, argv, envp in r3, r4, r5
 *   trap                      ; __tcc_start_main calls _exit, never returns
 *
 * 7 instructions, 28 bytes. `target_off` is the byte-offset within
 * `text` of the function we should call (`__tcc_start_main`, falling
 * back to `_main` if the helper wasn't injected — which matters for
 * Phase A programs that don't actually use libSystem).
 */
static int exe_emit_crt_shim(TCCState *s1, unsigned char *text_data,
                             int *text_size, int target_off)
{
    int shim_off = *text_size;
    int disp;
    uint32_t bl_word;
    uint32_t i;
    int bl_off;

    /* bl is the 6th instruction of the shim (offset 20). */
    bl_off = shim_off + 20;
    disp = target_off - bl_off;
    if (disp < -(1 << 25) || disp >= (1 << 25)) {
        tcc_error_noabort("ppc-macho: crt shim too far from target "
                          "(disp=0x%x)", (unsigned)disp);
        return -1;
    }
    bl_word = 0x48000001u | ((uint32_t)disp & 0x03fffffc);

    i = shim_off;
    /* addi r1, r1, -4   ->  0x3821fffc */
    text_data[i+0]  = 0x38; text_data[i+1]  = 0x21;
    text_data[i+2]  = 0xff; text_data[i+3]  = 0xfc;
    /* rlwinm r1, r1, 0, 0, 26  ->  0x54210034 */
    text_data[i+4]  = 0x54; text_data[i+5]  = 0x21;
    text_data[i+6]  = 0x00; text_data[i+7]  = 0x34;
    /* li r0, 0  ->  0x38000000 */
    text_data[i+8]  = 0x38; text_data[i+9]  = 0x00;
    text_data[i+10] = 0x00; text_data[i+11] = 0x00;
    /* stw r0, 0(r1)  ->  0x90010000 */
    text_data[i+12] = 0x90; text_data[i+13] = 0x01;
    text_data[i+14] = 0x00; text_data[i+15] = 0x00;
    /* stwu r1, -64(r1)  ->  0x9421ffc0 */
    text_data[i+16] = 0x94; text_data[i+17] = 0x21;
    text_data[i+18] = 0xff; text_data[i+19] = 0xc0;
    /* bl target */
    text_data[i+20] = (bl_word >> 24) & 0xff;
    text_data[i+21] = (bl_word >> 16) & 0xff;
    text_data[i+22] = (bl_word >>  8) & 0xff;
    text_data[i+23] =  bl_word        & 0xff;
    /* trap (_exit doesn't return; this is defensive) */
    text_data[i+24] = 0x7f; text_data[i+25] = 0xe0;
    text_data[i+26] = 0x00; text_data[i+27] = 0x08;

    *text_size = shim_off + 28;
    return shim_off;
}

/* Per-section layout info used during EXE writing. */
struct exe_sect {
    Section  *elf;          /* tcc Section, or NULL for synthesized */
    uint32_t  vmaddr;       /* assigned VM address */
    uint32_t  size;         /* size in bytes */
    uint32_t  file_off;     /* file offset (or 0 for zerofill) */
    int       is_zerofill;
};

/* Look up the VM address of a symbol given the laid-out sections.
 * For external (undef) symbols that have a stub assigned, returns
 * the stub address. Returns 0 and sets *err on error. */
static uint32_t exe_sym_addr(TCCState *s1, int symidx,
                             struct exe_sect *sects, int nsec,
                             struct extern_stub *stubs, int nstubs,
                             int *stub_for_elfsym,
                             const char **err_name)
{
    ElfW(Sym) *sym = &((ElfW(Sym) *)s1->symtab->data)[symidx];
    int j;
    if (sym->st_shndx == SHN_UNDEF) {
        if (stub_for_elfsym && stub_for_elfsym[symidx] >= 0)
            return stubs[stub_for_elfsym[symidx]].addr;
        *err_name = (char *)s1->symtab->link->data + sym->st_name;
        return 0;
    }
    for (j = 0; j < nsec; j++) {
        if (sects[j].elf && sects[j].elf->sh_num == sym->st_shndx)
            return sects[j].vmaddr + sym->st_value;
    }
    *err_name = (char *)s1->symtab->link->data + sym->st_name;
    return 0;
}

/* Bundled context for exe_resolve_section_relocs(); keeps the param
 * count to 5 so it stays within tcc's PPC backend's 8-GPR-slot
 * function call limit. */
struct exe_reloc_ctx {
    struct exe_sect      *all_sects;
    int                   nsec;
    struct extern_stub   *stubs;
    int                   nstubs;
    int                  *stub_for_elfsym;
    struct extern_nlptr  *nlptrs;
    int                   n_nlptrs;
    int                  *nl_for_elfsym;
};

/* Resolve relocations within `s` against the laid-out sections and
 * stubs. Modifies the section data buffer in place. Returns 0 on
 * success, -1 on error. */
static int exe_resolve_section_relocs(TCCState *s1, Section *s,
                                      uint32_t sect_vmaddr,
                                      unsigned char *sect_data,
                                      struct exe_reloc_ctx *ctx)
{
    struct exe_sect *all_sects = ctx->all_sects;
    int nsec = ctx->nsec;
    struct extern_stub *stubs = ctx->stubs;
    int nstubs = ctx->nstubs;
    int *stub_for_elfsym = ctx->stub_for_elfsym;
    struct extern_nlptr *nlptrs = ctx->nlptrs;
    int n_nlptrs = ctx->n_nlptrs;
    int *nl_for_elfsym = ctx->nl_for_elfsym;
    (void)n_nlptrs; (void)nlptrs;
    Section *rel = s->reloc;
    int nrel, i;
    ElfW_Rel *rels;
    if (!rel || !rel->data_offset)
        return 0;
    nrel = rel->data_offset / sizeof(ElfW_Rel);
    rels = (ElfW_Rel *)rel->data;
    for (i = 0; i < nrel; i++) {
        int type = ELFW(R_TYPE)(rels[i].r_info);
        int symidx = ELFW(R_SYM)(rels[i].r_info);
        uint32_t reloc_off = rels[i].r_offset;
        uint32_t target_addr;
        const char *err_name = NULL;
        uint32_t inst;

        /* PIC relocations target an external data symbol via a
         * __nl_symbol_ptr slot reached through the per-function PIC
         * base (r30, set up at function entry). The instruction is
         *   addis rT, r30, ha(slot - anchor)     ; HA16_PIC
         *   lwz   rT, lo(slot - anchor)(rT)      ; LO16_PIC
         * where `anchor` is the value of r30 (= address of the mtlr
         * that follows the bcl in the PIC base setup). We look up
         * anchor_off via the side table populated during codegen,
         * compute the absolute anchor + slot VAs, and patch the
         * 16-bit immediate in the instruction. */
        if (type == R_PPC_HA16_PIC || type == R_PPC_LO16_PIC) {
            int slot_idx = nl_for_elfsym ? nl_for_elfsym[symidx] : -1;
            int anchor_off;
            uint32_t slot_va, anchor_va, delta;
            int16_t halfword;
            if (slot_idx < 0) {
                const char *name = (char *)s1->symtab->link->data
                  + ((ElfW(Sym) *)s1->symtab->data)[symidx].st_name;
                tcc_error_noabort("ppc-macho: PIC reloc with no "
                                  "nlptr slot for sym '%s'",
                                  name && name[0] ? name : "?");
                return -1;
            }
            slot_va = nlptrs[slot_idx].slot_addr;
            anchor_off = ppc_pic_pairs_lookup((int)reloc_off);
            if (anchor_off < 0) {
                tcc_error_noabort("ppc-macho: no PIC anchor recorded "
                                  "for reloc at 0x%x", reloc_off);
                return -1;
            }
            anchor_va = sect_vmaddr + (uint32_t)anchor_off;
            delta = slot_va - anchor_va;
            inst = ((uint32_t)sect_data[reloc_off] << 24)
                 | ((uint32_t)sect_data[reloc_off+1] << 16)
                 | ((uint32_t)sect_data[reloc_off+2] <<  8)
                 |  (uint32_t)sect_data[reloc_off+3];
            if (type == R_PPC_HA16_PIC)
                halfword = (int16_t)(((delta + 0x8000) >> 16) & 0xffff);
            else
                halfword = (int16_t)(delta & 0xffff);
            inst = (inst & ~0xffffu) | (uint16_t)halfword;
            sect_data[reloc_off+0] = (inst >> 24) & 0xff;
            sect_data[reloc_off+1] = (inst >> 16) & 0xff;
            sect_data[reloc_off+2] = (inst >>  8) & 0xff;
            sect_data[reloc_off+3] =  inst        & 0xff;
            continue;
        }

        target_addr = exe_sym_addr(s1, symidx, all_sects, nsec,
                                    stubs, nstubs, stub_for_elfsym,
                                    &err_name);
        if (err_name) {
            tcc_error_noabort("ppc-macho: undefined symbol '%s' "
                              "(no stub allocated; only function "
                              "calls via REL24 get stubs in -o exe)",
                              err_name);
            return -1;
        }

        switch (type) {
        case R_PPC_REL24: {
            int32_t bdisp = (int32_t)(target_addr - (sect_vmaddr + reloc_off));
            if (bdisp < -(1 << 25) || bdisp >= (1 << 25)) {
                tcc_error_noabort("ppc-macho: REL24 out of range");
                return -1;
            }
            inst = ((uint32_t)sect_data[reloc_off] << 24)
                 | ((uint32_t)sect_data[reloc_off+1] << 16)
                 | ((uint32_t)sect_data[reloc_off+2] <<  8)
                 |  (uint32_t)sect_data[reloc_off+3];
            inst = (inst & ~0x03fffffc) | ((uint32_t)bdisp & 0x03fffffc);
            sect_data[reloc_off+0] = (inst >> 24) & 0xff;
            sect_data[reloc_off+1] = (inst >> 16) & 0xff;
            sect_data[reloc_off+2] = (inst >>  8) & 0xff;
            sect_data[reloc_off+3] =  inst        & 0xff;
            break;
        }
        case R_PPC_ADDR16_HA:
        case R_PPC_ADDR16_LO:
        case R_PPC_ADDR32: {
            uint16_t imm;
            inst = ((uint32_t)sect_data[reloc_off] << 24)
                 | ((uint32_t)sect_data[reloc_off+1] << 16)
                 | ((uint32_t)sect_data[reloc_off+2] <<  8)
                 |  (uint32_t)sect_data[reloc_off+3];
            if (type == R_PPC_ADDR32) {
                inst = target_addr;
            } else if (type == R_PPC_ADDR16_HA) {
                imm = ((target_addr + 0x8000) >> 16) & 0xffff;
                inst = (inst & ~0xffff) | imm;
            } else {
                imm = target_addr & 0xffff;
                inst = (inst & ~0xffff) | imm;
            }
            sect_data[reloc_off+0] = (inst >> 24) & 0xff;
            sect_data[reloc_off+1] = (inst >> 16) & 0xff;
            sect_data[reloc_off+2] = (inst >>  8) & 0xff;
            sect_data[reloc_off+3] =  inst        & 0xff;
            break;
        }
        case R_PPC_NONE:
            break;
        default:
            tcc_error_noabort("ppc-macho: reloc type %d in -o exe "
                              "not yet handled (sym '%s')", type,
                              (char *)s1->symtab->link->data
                              + ((ElfW(Sym) *)s1->symtab->data)[symidx].st_name);
            return -1;
        }
    }
    return 0;
}

/* Write a 16-byte section name padded with NULs. */
static void put_sectname(obuf *out, const char *name)
{
    char nm[16];
    memset(nm, 0, sizeof(nm));
    strncpy(nm, name, sizeof(nm));
    obuf_put(out, nm, 16);
}

/* Write a Mach-O nlist entry (12 bytes). */
static void put_nlist(obuf *nlist, uint32_t strx, uint8_t n_type,
                      uint8_t n_sect, uint16_t n_desc, uint32_t n_value)
{
    put32be(nlist, strx);
    put8(nlist, n_type);
    put8(nlist, n_sect);
    put8(nlist, (n_desc >> 8) & 0xff);
    put8(nlist, n_desc & 0xff);
    put32be(nlist, n_value);
}

/* The big one. */
static int macho_output_exe(TCCState *s1, const char *filename)
{
    obuf out;
    obuf nlist, strtab, indirect;
    Section *text = NULL, *rodata = NULL, *data = NULL;
    int main_off, shim_off, target_off;
    int i, ret = -1;
    FILE *fp = NULL;
    int fd;

    /* External-stub bookkeeping (matches the OBJ writer). */
    struct extern_stub *stubs = NULL;
    int *stub_for_elfsym = NULL;
    int nstubs = 0;
    unsigned char *stub_data = NULL;
    unsigned char *nl_ptr_data = NULL;

    /* Section index ranges (1-based: __text=1). */
    enum { SECT_TEXT = 1 };
    int sect_const = 0, sect_stub = 0, sect_nlptr = 0;
    int n_text_sects, n_data_sects, total_msects;

    struct exe_sect sects[8];   /* generous; we never use more than 4 */
    int nsec = 0;
    int sect_idx_text = -1, sect_idx_rodata = -1, sect_idx_data = -1;
    int sect_idx_stub = -1, sect_idx_nlptr = -1, sect_idx_dyld = -1;
    /* External-data __nl_symbol_ptr bookkeeping. The function-stub
     * path uses its own slots in the same __nl_symbol_ptr section;
     * data symbols append after them. */
    struct extern_nlptr *nlptrs = NULL;
    int *nl_for_elfsym = NULL;
    int n_nlptrs = 0;
    int *data_sym_idx = NULL;     /* per nlptr, index into our nlist */

    /* Layout. */
    uint32_t text_seg_vmaddr = EXE_TEXT_VMADDR_BASE;
    uint32_t text_sect_vmaddr;
    uint32_t hdr_and_lc_size, ncmds, total_cmds_size;
    uint32_t text_data_size;
    uint32_t cur_va, cur_off;
    uint32_t text_seg_filesize, text_seg_vmsize;
    uint32_t data_seg_vmaddr = 0, data_seg_file_off = 0;
    uint32_t data_seg_filesize = 0;
    uint32_t linkedit_vmaddr, linkedit_file_off, linkedit_filesize;
    uint32_t entry_addr;
    uint32_t indirect_file_off = 0, sym_file_off, str_file_off;

    /* LC sizes. */
    uint32_t pagezero_cmd_size = 56;
    uint32_t text_seg_cmd_size, data_seg_cmd_size = 0;
    uint32_t linkedit_cmd_size = 56;
    uint32_t symtab_cmd_size = 24;
    uint32_t dysymtab_cmd_size = 80;
    uint32_t unixthread_cmd_size = 8 + 8 + 40 * 4;
    /* dyld and dylib paths (always nul-terminated, then padded to 4). */
    static const char dylib_path[] = "/usr/lib/libSystem.B.dylib";
    static const char dyld_path[]  = "/usr/lib/dyld";
    uint32_t dylib_path_aligned = (sizeof(dylib_path) + 3u) & ~3u;
    uint32_t dyld_path_aligned  = (sizeof(dyld_path)  + 3u) & ~3u;
    uint32_t dylib_cmd_size = 24 + dylib_path_aligned;
    uint32_t dyld_cmd_size  = 12 + dyld_path_aligned;

    /* Symtab counts. */
    int n_localsym = 0, n_extdefsym = 0, n_undefsym = 0;
    /* For each stub, the index of its symbol within nlist. */
    int *stub_sym_idx = NULL;

    unsigned char *text_data = NULL, *rodata_data = NULL, *data_data = NULL;
    int new_text_size;

    memset(&out,      0, sizeof(out));
    memset(&nlist,    0, sizeof(nlist));
    memset(&strtab,   0, sizeof(strtab));
    memset(&indirect, 0, sizeof(indirect));
    memset(sects,     0, sizeof(sects));

    /* ---- Find sections. ---- */
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        if (!s) continue;
        if (s->sh_type == SHT_PROGBITS && (s->sh_flags & SHF_EXECINSTR)
            && !strcmp(s->name, ".text"))
            text = s;
        else if (s->sh_type == SHT_PROGBITS && s->data_offset > 0
            && (!strcmp(s->name, ".rodata") || !strcmp(s->name, ".data.ro")))
            rodata = s;
        else if (s->sh_type == SHT_PROGBITS && s->data_offset > 0
            && !strcmp(s->name, ".data"))
            data = s;
    }
    if (!text || text->data_offset == 0) {
        tcc_error_noabort("ppc-macho: empty or missing .text section");
        return -1;
    }
    main_off = exe_find_main_offset(s1, text);
    if (main_off < 0) {
        tcc_error_noabort("ppc-macho: _main not defined in .text");
        return -1;
    }
    /* Prefer the auto-injected libSystem-init helper (set up by
     * tcc_output_file in tccelf.c). It calls main and chains into
     * libSystem's _exit. Fall back to _main directly for programs
     * that don't use libSystem stdio (Phase A path). */
    target_off = exe_find_text_sym_offset(s1, text, "__tcc_start_main");
    if (target_off < 0)
        target_off = main_off;

    /* ---- Collect external function calls. ---- */
    nstubs = collect_extern_stubs(s1, &stubs, &stub_for_elfsym);
    /* ---- Collect external data references. ---- */
    n_nlptrs = collect_extern_nlptrs(s1, &nlptrs, &nl_for_elfsym);

    /* ---- Plan section layout. ---- */
    n_text_sects = 1;       /* __text */
    if (rodata)
        n_text_sects++;     /* __const */
    if (nstubs > 0)
        n_text_sects++;     /* __picsymbolstub1 (well, plain stub) */
    n_data_sects = 0;
    if (data)
        n_data_sects++;     /* __data */
    if (nstubs > 0 || n_nlptrs > 0)
        n_data_sects++;     /* __nl_symbol_ptr (function ptrs + data ptrs) */
    if (nstubs > 0 || n_nlptrs > 0)
        n_data_sects++;     /* __dyld (8 bytes; dyld writes pointers here) */

    total_msects = n_text_sects + n_data_sects;

    /* ---- LC sizes -> hdr_and_lc_size. ---- */
    text_seg_cmd_size = 56 + 68u * n_text_sects;
    if (n_data_sects > 0)
        data_seg_cmd_size = 56 + 68u * n_data_sects;

    ncmds = 4;  /* PAGEZERO, TEXT, LINKEDIT, UNIXTHREAD */
    total_cmds_size = pagezero_cmd_size + text_seg_cmd_size
                    + linkedit_cmd_size + symtab_cmd_size
                    + unixthread_cmd_size;
    if (n_data_sects > 0) {
        ncmds++;
        total_cmds_size += data_seg_cmd_size;
    }
    /* LC_SYMTAB is included in total_cmds_size above and ncmds+1 below. */
    ncmds++;
    if (nstubs > 0 || n_nlptrs > 0) {
        ncmds += 4;  /* LC_LOAD_DYLINKER, LC_LOAD_DYLIB, LC_DYSYMTAB,
                      * LC_TWOLEVEL_HINTS */
        total_cmds_size += dyld_cmd_size + dylib_cmd_size
                         + dysymtab_cmd_size + 16 /* LC_TWOLEVEL_HINTS */;
    }
    hdr_and_lc_size = 28 + total_cmds_size;

    /* ---- Layout sections within __TEXT. ---- */
    text_sect_vmaddr = text_seg_vmaddr + hdr_and_lc_size;
    /* Reserve room for the crt-shim (36 bytes); pad up to 4-byte
     * alignment for the next section's start. */
    text_data_size = (text->data_offset + 28u + 3u) & ~3u;
    cur_va = text_sect_vmaddr + text_data_size;
    cur_off = hdr_and_lc_size + text_data_size;

    sect_idx_text = nsec;
    sects[nsec].elf = text;
    sects[nsec].vmaddr = text_sect_vmaddr;
    sects[nsec].size = text_data_size;
    sects[nsec].file_off = hdr_and_lc_size;
    sects[nsec].is_zerofill = 0;
    nsec++;

    if (rodata) {
        cur_va = (cur_va + 3u) & ~3u;
        cur_off = (cur_off + 3u) & ~3u;
        sect_idx_rodata = nsec;
        sects[nsec].elf = rodata;
        sects[nsec].vmaddr = cur_va;
        sects[nsec].size = rodata->data_offset;
        sects[nsec].file_off = cur_off;
        sects[nsec].is_zerofill = 0;
        nsec++;
        cur_va += rodata->data_offset;
        cur_off += rodata->data_offset;
    }

    if (nstubs > 0) {
        /* Stubs are 16 bytes each (4 instructions). Align to 16. */
        cur_va  = (cur_va  + 15u) & ~15u;
        cur_off = (cur_off + 15u) & ~15u;
        sect_idx_stub = nsec;
        sects[nsec].elf = NULL;
        sects[nsec].vmaddr = cur_va;
        sects[nsec].size = nstubs * 16u;
        sects[nsec].file_off = cur_off;
        sects[nsec].is_zerofill = 0;
        nsec++;
        cur_va  += nstubs * 16u;
        cur_off += nstubs * 16u;
    }

    /* Round up __TEXT segment to page. */
    text_seg_filesize = (cur_off + EXE_PAGE_SIZE - 1u) & ~(uint32_t)(EXE_PAGE_SIZE - 1);
    text_seg_vmsize   = text_seg_filesize;

    /* ---- Layout __DATA. ---- */
    if (n_data_sects > 0) {
        uint32_t total_nlptr_size = (nstubs + n_nlptrs) * 4u;
        uint32_t data_cur, data_end;
        data_seg_vmaddr   = text_seg_vmaddr + text_seg_filesize;
        data_seg_file_off = text_seg_filesize;
        data_cur = 0;

        if (data) {
            sect_idx_data = nsec;
            sects[nsec].elf = data;
            sects[nsec].vmaddr = data_seg_vmaddr + data_cur;
            sects[nsec].size = data->data_offset;
            sects[nsec].file_off = data_seg_file_off + data_cur;
            sects[nsec].is_zerofill = 0;
            nsec++;
            data_cur += data->data_offset;
            /* Align to 4 for the next section. */
            data_cur = (data_cur + 3u) & ~3u;
        }

        if (nstubs > 0 || n_nlptrs > 0) {
            sect_idx_nlptr = nsec;
            sects[nsec].elf = NULL;
            sects[nsec].vmaddr = data_seg_vmaddr + data_cur;
            sects[nsec].size = total_nlptr_size;
            sects[nsec].file_off = data_seg_file_off + data_cur;
            sects[nsec].is_zerofill = 0;
            nsec++;
            data_cur += total_nlptr_size;
        }

        /* __dyld: 8 bytes that dyld writes at load time. Empirically
         * its presence affects whether dyld runs library
         * initializers; without it libSystem stays uninitialized. */
        if (nstubs > 0 || n_nlptrs > 0) {
            data_cur = (data_cur + 3u) & ~3u;
            sect_idx_dyld = nsec;
            sects[nsec].elf = NULL;
            sects[nsec].vmaddr = data_seg_vmaddr + data_cur;
            sects[nsec].size = 8;
            sects[nsec].file_off = data_seg_file_off + data_cur;
            sects[nsec].is_zerofill = 0;
            nsec++;
            data_cur += 8;
        }

        data_end = data_cur;
        data_seg_filesize = (data_end + EXE_PAGE_SIZE - 1u)
                          & ~(uint32_t)(EXE_PAGE_SIZE - 1);
    }

    /* ---- Layout __LINKEDIT. ---- */
    linkedit_file_off = text_seg_filesize + data_seg_filesize;
    linkedit_vmaddr   = text_seg_vmaddr + text_seg_filesize + data_seg_filesize;

    /* Now we know the stub addresses; set them in `stubs[]`.
     * Function-stub slots come first in __nl_symbol_ptr, data slots
     * follow them. */
    if (nstubs > 0) {
        for (i = 0; i < nstubs; i++) {
            stubs[i].addr = sects[sect_idx_stub].vmaddr + i * 16u;
            stubs[i].la_ptr_addr = sects[sect_idx_nlptr].vmaddr + i * 4u;
        }
    }
    if (n_nlptrs > 0) {
        for (i = 0; i < n_nlptrs; i++) {
            nlptrs[i].slot_addr = sects[sect_idx_nlptr].vmaddr
                                  + (nstubs + i) * 4u;
        }
    }

    /* ---- Allocate writable copies of section data. ---- */
    new_text_size = text->data_offset;
    text_data = tcc_malloc(new_text_size + 64);
    memcpy(text_data, text->data, new_text_size);
    memset(text_data + new_text_size, 0, 64);

    if (rodata) {
        rodata_data = tcc_malloc(rodata->data_offset);
        memcpy(rodata_data, rodata->data, rodata->data_offset);
    }
    if (data) {
        data_data = tcc_malloc(data->data_offset);
        memcpy(data_data, data->data, data->data_offset);
    }

    /* ---- Resolve relocations in text and rodata. ---- */
    {
        struct exe_reloc_ctx ctx;
        ctx.all_sects = sects;
        ctx.nsec = nsec;
        ctx.stubs = stubs;
        ctx.nstubs = nstubs;
        ctx.stub_for_elfsym = stub_for_elfsym;
        ctx.nlptrs = nlptrs;
        ctx.n_nlptrs = n_nlptrs;
        ctx.nl_for_elfsym = nl_for_elfsym;
        if (exe_resolve_section_relocs(s1, text, text_sect_vmaddr,
                                        text_data, &ctx) < 0)
            goto cleanup;
        if (rodata && exe_resolve_section_relocs(s1, rodata,
                                                  sects[sect_idx_rodata].vmaddr,
                                                  rodata_data, &ctx) < 0)
            goto cleanup;
        if (data && exe_resolve_section_relocs(s1, data,
                                                sects[sect_idx_data].vmaddr,
                                                data_data, &ctx) < 0)
            goto cleanup;
    }

    /* ---- Append crt-shim. ---- */
    shim_off = exe_emit_crt_shim(s1, text_data, &new_text_size, target_off);
    if (shim_off < 0)
        goto cleanup;
    /* text_data_size already accounts for the +16 shim; size is fixed. */
    entry_addr = text_sect_vmaddr + (uint32_t)shim_off;

    /* ---- Generate stub bytes (4 instructions per stub):
     *      lis   r12, ha(slot)
     *      lwz   r12, lo(slot)(r12)
     *      mtctr r12
     *      bctr
     */
    if (nstubs > 0 || n_nlptrs > 0)
        nl_ptr_data = tcc_mallocz((nstubs + n_nlptrs) * 4);  /* dyld fills */
    if (nstubs > 0) {
        stub_data = tcc_mallocz(nstubs * 16);
        for (i = 0; i < nstubs; i++) {
            uint32_t slot = stubs[i].la_ptr_addr;
            uint16_t ha = (uint16_t)((slot + 0x8000) >> 16);
            uint16_t lo = (uint16_t)(slot & 0xffff);
            uint32_t insns[4];
            int j;
            insns[0] = 0x3d800000u | ha;        /* lis r12, ha */
            insns[1] = 0x818c0000u | lo;        /* lwz r12, lo(r12) */
            insns[2] = 0x7d8903a6u;             /* mtctr r12 */
            insns[3] = 0x4e800420u;             /* bctr */
            for (j = 0; j < 4; j++) {
                stub_data[i*16 + j*4 + 0] = (insns[j] >> 24) & 0xff;
                stub_data[i*16 + j*4 + 1] = (insns[j] >> 16) & 0xff;
                stub_data[i*16 + j*4 + 2] = (insns[j] >>  8) & 0xff;
                stub_data[i*16 + j*4 + 3] =  insns[j]        & 0xff;
            }
        }
    }

    /* ---- Build symbol table.
     *
     * Three groups required by Mach-O DYSYMTAB:
     *   1. local syms (defined, non-external)
     *   2. extdef syms (defined, external) — _main, _start
     *   3. undef syms — one per stub (e.g. _printf)
     *
     * We only emit the canonical entries: _main, _start, and one undef
     * per stub. Local syms not strictly required for an executable. */
    obuf_put(&strtab, "", 1);

    /* No locals in our minimal symtab. */
    n_localsym = 0;

    /* Externally-defined: enumerate all globally-visible symbols
     * defined in our text/data/rodata sections, so dyld can find
     * them when libSystem looks up _NXArgc / _environ / etc. We
     * always include synthetic _start (the entry shim) regardless
     * of whether the user's code defined it. */
    {
        int nsyms = s1->symtab->data_offset / sizeof(ElfW(Sym));
        int si;
        for (si = 1; si < nsyms; si++) {
            ElfW(Sym) *esym = &((ElfW(Sym) *)s1->symtab->data)[si];
            const char *name = (char *)s1->symtab->link->data + esym->st_name;
            int stype = ELFW(ST_TYPE)(esym->st_info);
            int sbind = ELFW(ST_BIND)(esym->st_info);
            uint32_t n_value = 0;
            uint8_t n_sect = NO_SECT;
            uint32_t strx;
            if (esym->st_shndx == SHN_UNDEF) continue;
            if (esym->st_shndx == SHN_COMMON) continue;
            if (stype == STT_SECTION || stype == STT_FILE) continue;
            if (!name || !name[0]) continue;
            if (sbind != STB_GLOBAL) continue;
            /* Map ELF section index to our Mach-O section. */
            if (text && esym->st_shndx == text->sh_num) {
                n_sect = (uint8_t)(sect_idx_text + 1);
                n_value = sects[sect_idx_text].vmaddr + esym->st_value;
            } else if (rodata && esym->st_shndx == rodata->sh_num) {
                n_sect = (uint8_t)(sect_idx_rodata + 1);
                n_value = sects[sect_idx_rodata].vmaddr + esym->st_value;
            } else if (data && esym->st_shndx == data->sh_num) {
                n_sect = (uint8_t)(sect_idx_data + 1);
                n_value = sects[sect_idx_data].vmaddr + esym->st_value;
            } else {
                continue;       /* defined in a section we don't emit */
            }
            strx = (uint32_t)strtab.len;
            obuf_put(&strtab, name, strlen(name) + 1);
            put_nlist(&nlist, strx, N_SECT | N_EXT, n_sect, 0, n_value);
            n_extdefsym++;
        }
    }
    /* Synthetic _start (the entry shim). */
    {
        uint32_t start_strx = (uint32_t)strtab.len;
        obuf_put(&strtab, "_start", 7);
        put_nlist(&nlist, start_strx, N_SECT | N_EXT, SECT_TEXT, 0,
                  entry_addr);
        n_extdefsym++;
    }
    /* `__mh_execute_header` is a magic symbol dyld recognizes: it's
     * the absolute address of the Mach-O header (= base of the __TEXT
     * segment, since the header sits at the start of the segment).
     * libSystem's `_NSGetEnviron` and friends use it to identify the
     * executable image when looking up _environ / _NXArgc / etc. We
     * emit it as an N_ABS external so dyld picks it up. */
    {
        uint32_t mh_strx = (uint32_t)strtab.len;
        obuf_put(&strtab, "__mh_execute_header", 20);
        put_nlist(&nlist, mh_strx, N_EXT | N_ABS, NO_SECT, 0,
                  text_seg_vmaddr);
        n_extdefsym++;
    }

    /* Undef externals: one per stub, then one per data nlptr. n_desc
     * carries the two-level namespace library ordinal in its high
     * byte; ord 1 = first LC_LOAD_DYLIB (libSystem in our case).
     * Without this dyld looks for the symbol in our own executable
     * and fails. */
    if (nstubs > 0) {
        stub_sym_idx = tcc_mallocz(nstubs * sizeof(int));
        for (i = 0; i < nstubs; i++) {
            ElfW(Sym) *esym = (ElfW(Sym) *)s1->symtab->data + stubs[i].elfsym_idx;
            const char *name = (char *)s1->symtab->link->data + esym->st_name;
            uint32_t strx = (uint32_t)strtab.len;
            uint16_t n_desc = (1u << 8);
            obuf_put(&strtab, name, strlen(name) + 1);
            stub_sym_idx[i] = n_localsym + n_extdefsym + n_undefsym;
            put_nlist(&nlist, strx, N_EXT | N_UNDF, NO_SECT, n_desc, 0);
            n_undefsym++;
        }
    }
    if (n_nlptrs > 0) {
        data_sym_idx = tcc_mallocz(n_nlptrs * sizeof(int));
        for (i = 0; i < n_nlptrs; i++) {
            ElfW(Sym) *esym = (ElfW(Sym) *)s1->symtab->data + nlptrs[i].elfsym_idx;
            const char *name = (char *)s1->symtab->link->data + esym->st_name;
            uint32_t strx = (uint32_t)strtab.len;
            uint16_t n_desc = (1u << 8);
            obuf_put(&strtab, name, strlen(name) + 1);
            data_sym_idx[i] = n_localsym + n_extdefsym + n_undefsym;
            put_nlist(&nlist, strx, N_EXT | N_UNDF, NO_SECT, n_desc, 0);
            n_undefsym++;
        }
    }

    /* ---- Build indirect symbol table.
     *
     * One entry per __nl_symbol_ptr slot: function-stub slots first
     * (in stub order), then data slots (in nlptr order). Each entry
     * is the symbol's index in our nlist; dyld uses this to know
     * which symbol to bind each slot to. */
    if (nstubs > 0) {
        for (i = 0; i < nstubs; i++)
            put32be(&indirect, stub_sym_idx[i]);
    }
    if (n_nlptrs > 0) {
        for (i = 0; i < n_nlptrs; i++)
            put32be(&indirect, data_sym_idx[i]);
    }

    /* ---- Compute __LINKEDIT layout. ---- */
    {
        uint32_t loff = linkedit_file_off;
        if (nstubs > 0 || n_nlptrs > 0) {
            indirect_file_off = loff;
            loff += (uint32_t)indirect.len;
        }
        sym_file_off = loff;
        loff += (uint32_t)nlist.len;
        str_file_off = loff;
        loff += (uint32_t)strtab.len;
        linkedit_filesize = loff - linkedit_file_off;
    }

    /* ---- Serialize: Mach header. ---- */
    put32be(&out, MH_MAGIC);
    put32be(&out, CPU_TYPE_POWERPC);
    put32be(&out, CPU_SUBTYPE_POWERPC_ALL);
    put32be(&out, MH_EXECUTE);
    put32be(&out, ncmds);
    put32be(&out, total_cmds_size);
    put32be(&out, (nstubs > 0 || n_nlptrs > 0)
                  ? (MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL)
                  : MH_NOUNDEFS);

    /* ---- LC_SEGMENT __PAGEZERO. ---- */
    put32be(&out, LC_SEGMENT);
    put32be(&out, pagezero_cmd_size);
    put_sectname(&out, "__PAGEZERO");
    put32be(&out, 0);
    put32be(&out, EXE_PAGEZERO_VMSIZE);
    put32be(&out, 0);
    put32be(&out, 0);
    put32be(&out, 0);
    put32be(&out, 0);
    put32be(&out, 0);
    put32be(&out, 0);

    /* ---- LC_SEGMENT __TEXT. ---- */
    put32be(&out, LC_SEGMENT);
    put32be(&out, text_seg_cmd_size);
    put_sectname(&out, "__TEXT");
    put32be(&out, text_seg_vmaddr);
    put32be(&out, text_seg_vmsize);
    put32be(&out, 0);
    put32be(&out, text_seg_filesize);
    put32be(&out, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    put32be(&out, VM_PROT_READ | VM_PROT_EXECUTE);
    put32be(&out, n_text_sects);
    put32be(&out, 0);
    /* __text section header */
    put_sectname(&out, "__text");
    put_sectname(&out, "__TEXT");
    put32be(&out, sects[sect_idx_text].vmaddr);
    put32be(&out, sects[sect_idx_text].size);
    put32be(&out, sects[sect_idx_text].file_off);
    put32be(&out, 2);   /* align 2^2 */
    put32be(&out, 0);
    put32be(&out, 0);
    put32be(&out, S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
    put32be(&out, 0);
    put32be(&out, 0);
    /* __const if present */
    if (rodata) {
        put_sectname(&out, "__const");
        put_sectname(&out, "__TEXT");
        put32be(&out, sects[sect_idx_rodata].vmaddr);
        put32be(&out, sects[sect_idx_rodata].size);
        put32be(&out, sects[sect_idx_rodata].file_off);
        put32be(&out, 2);
        put32be(&out, 0);
        put32be(&out, 0);
        put32be(&out, S_REGULAR);
        put32be(&out, 0);
        put32be(&out, 0);
    }
    /* stub section if present. We use plain S_REGULAR (not
     * S_SYMBOL_STUBS) because our stubs are simple PIC-free loader
     * code that reads from the __nl_symbol_ptr slot — they don't
     * need binding themselves; only the slots do. That keeps the
     * indirect symbol table to N entries (one per nl_ptr). */
    if (nstubs > 0) {
        put_sectname(&out, "__symbol_stub");
        put_sectname(&out, "__TEXT");
        put32be(&out, sects[sect_idx_stub].vmaddr);
        put32be(&out, sects[sect_idx_stub].size);
        put32be(&out, sects[sect_idx_stub].file_off);
        put32be(&out, 4);     /* align 2^4 = 16 */
        put32be(&out, 0);
        put32be(&out, 0);
        put32be(&out, S_REGULAR | S_ATTR_PURE_INSTRUCTIONS
                       | S_ATTR_SOME_INSTRUCTIONS);
        put32be(&out, 0);
        put32be(&out, 0);
    }

    /* ---- LC_SEGMENT __DATA. ---- */
    if (n_data_sects > 0) {
        put32be(&out, LC_SEGMENT);
        put32be(&out, data_seg_cmd_size);
        put_sectname(&out, "__DATA");
        put32be(&out, data_seg_vmaddr);
        put32be(&out, data_seg_filesize);
        put32be(&out, data_seg_file_off);
        put32be(&out, data_seg_filesize);
        put32be(&out, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
        put32be(&out, VM_PROT_READ | VM_PROT_WRITE);
        put32be(&out, n_data_sects);
        put32be(&out, 0);
        /* __data (initialized globals) — comes first within __DATA. */
        if (data) {
            put_sectname(&out, "__data");
            put_sectname(&out, "__DATA");
            put32be(&out, sects[sect_idx_data].vmaddr);
            put32be(&out, sects[sect_idx_data].size);
            put32be(&out, sects[sect_idx_data].file_off);
            put32be(&out, 2);
            put32be(&out, 0);
            put32be(&out, 0);
            put32be(&out, S_REGULAR);
            put32be(&out, 0);
            put32be(&out, 0);
        }
        /* __nl_symbol_ptr */
        if (nstubs > 0 || n_nlptrs > 0) {
            put_sectname(&out, "__nl_symbol_ptr");
            put_sectname(&out, "__DATA");
            put32be(&out, sects[sect_idx_nlptr].vmaddr);
            put32be(&out, sects[sect_idx_nlptr].size);
            put32be(&out, sects[sect_idx_nlptr].file_off);
            put32be(&out, 2);
            put32be(&out, 0);
            put32be(&out, 0);
            put32be(&out, S_NON_LAZY_SYMBOL_POINTERS);
            put32be(&out, 0);     /* reserved1: indirect symtab base index */
            put32be(&out, 0);
        }
        /* __dyld */
        if (sect_idx_dyld >= 0) {
            put_sectname(&out, "__dyld");
            put_sectname(&out, "__DATA");
            put32be(&out, sects[sect_idx_dyld].vmaddr);
            put32be(&out, sects[sect_idx_dyld].size);
            put32be(&out, sects[sect_idx_dyld].file_off);
            put32be(&out, 2);
            put32be(&out, 0);
            put32be(&out, 0);
            put32be(&out, S_REGULAR);
            put32be(&out, 0);
            put32be(&out, 0);
        }
    }

    /* ---- LC_SEGMENT __LINKEDIT. ---- */
    put32be(&out, LC_SEGMENT);
    put32be(&out, linkedit_cmd_size);
    put_sectname(&out, "__LINKEDIT");
    put32be(&out, linkedit_vmaddr);
    put32be(&out, (linkedit_filesize + EXE_PAGE_SIZE - 1u)
                  & ~(uint32_t)(EXE_PAGE_SIZE - 1));
    put32be(&out, linkedit_file_off);
    put32be(&out, linkedit_filesize);
    put32be(&out, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    put32be(&out, VM_PROT_READ);
    put32be(&out, 0);
    put32be(&out, 0);

    /* ---- LC_LOAD_DYLINKER (only if we use libSystem). ---- */
    if (nstubs > 0 || n_nlptrs > 0) {
        uint32_t k;
        put32be(&out, LC_LOAD_DYLINKER);
        put32be(&out, dyld_cmd_size);
        put32be(&out, 12);    /* offset of name in this lc */
        obuf_put(&out, dyld_path, sizeof(dyld_path));
        for (k = sizeof(dyld_path); k < dyld_path_aligned; k++)
            put8(&out, 0);
    }

    /* ---- LC_LOAD_DYLIB libSystem. ---- */
    if (nstubs > 0 || n_nlptrs > 0) {
        uint32_t k;
        put32be(&out, LC_LOAD_DYLIB);
        put32be(&out, dylib_cmd_size);
        put32be(&out, 24);    /* name offset */
        put32be(&out, 0);     /* timestamp */
        put32be(&out, 0x00580000);   /* current_version (88.0.0) */
        put32be(&out, 0x00010000);   /* compat_version  (1.0.0) */
        obuf_put(&out, dylib_path, sizeof(dylib_path));
        for (k = sizeof(dylib_path); k < dylib_path_aligned; k++)
            put8(&out, 0);
    }

    /* ---- LC_SYMTAB. ---- */
    put32be(&out, LC_SYMTAB);
    put32be(&out, symtab_cmd_size);
    put32be(&out, sym_file_off);
    put32be(&out, n_localsym + n_extdefsym + n_undefsym);
    put32be(&out, str_file_off);
    put32be(&out, (uint32_t)strtab.len);

    /* ---- LC_DYSYMTAB. ---- */
    if (nstubs > 0 || n_nlptrs > 0) {
        put32be(&out, LC_DYSYMTAB);
        put32be(&out, dysymtab_cmd_size);
        put32be(&out, 0);                            /* ilocalsym */
        put32be(&out, n_localsym);                   /* nlocalsym */
        put32be(&out, n_localsym);                   /* iextdefsym */
        put32be(&out, n_extdefsym);                  /* nextdefsym */
        put32be(&out, n_localsym + n_extdefsym);     /* iundefsym */
        put32be(&out, n_undefsym);                   /* nundefsym */
        put32be(&out, 0);                            /* tocoff */
        put32be(&out, 0);                            /* ntoc */
        put32be(&out, 0);                            /* modtaboff */
        put32be(&out, 0);                            /* nmodtab */
        put32be(&out, 0);                            /* extrefsymoff */
        put32be(&out, 0);                            /* nextrefsyms */
        put32be(&out, indirect_file_off);            /* indirectsymoff */
        put32be(&out, nstubs + n_nlptrs);            /* nindirectsyms */
        put32be(&out, 0);                            /* extreloff */
        put32be(&out, 0);                            /* nextrel */
        put32be(&out, 0);                            /* locreloff */
        put32be(&out, 0);                            /* nlocrel */
    }

    /* ---- LC_TWOLEVEL_HINTS. Empty (zero hints), but its presence
     * appears to be required for dyld to call libSystem's
     * initializers on Tiger PPC. Without it, libSystem stays
     * uninitialized and stdio (puts, printf) crashes. */
    if (nstubs > 0 || n_nlptrs > 0) {
        put32be(&out, LC_TWOLEVEL_HINTS);
        put32be(&out, 16);                           /* cmdsize */
        put32be(&out, linkedit_file_off);            /* offset (zero hints, but valid) */
        put32be(&out, 0);                            /* nhints */
    }

    /* ---- LC_UNIXTHREAD with PPC_THREAD_STATE. ---- */
    put32be(&out, LC_UNIXTHREAD);
    put32be(&out, unixthread_cmd_size);
    put32be(&out, PPC_THREAD_STATE);
    put32be(&out, PPC_THREAD_STATE_COUNT);
    {
        int wi;
        for (wi = 0; wi < PPC_THREAD_STATE_COUNT; wi++) {
            uint32_t v = 0;
            if (wi == 0)
                v = entry_addr;       /* srr0 = entry PC */
            put32be(&out, v);
        }
    }

    /* ---- Pad to text data offset, then write text. ---- */
    while (out.len < hdr_and_lc_size)
        put8(&out, 0);
    obuf_put(&out, text_data, text_data_size);

    /* ---- rodata. ---- */
    if (rodata) {
        while (out.len < sects[sect_idx_rodata].file_off)
            put8(&out, 0);
        obuf_put(&out, rodata_data, sects[sect_idx_rodata].size);
    }

    /* ---- stubs. ---- */
    if (nstubs > 0) {
        while (out.len < sects[sect_idx_stub].file_off)
            put8(&out, 0);
        obuf_put(&out, stub_data, sects[sect_idx_stub].size);
    }

    /* ---- Pad to __DATA boundary, then write data sections. ---- */
    if (n_data_sects > 0) {
        while (out.len < data_seg_file_off)
            put8(&out, 0);
        if (data) {
            while (out.len < sects[sect_idx_data].file_off)
                put8(&out, 0);
            obuf_put(&out, data_data, sects[sect_idx_data].size);
        }
        if (nstubs > 0 || n_nlptrs > 0) {
            while (out.len < sects[sect_idx_nlptr].file_off)
                put8(&out, 0);
            obuf_put(&out, nl_ptr_data, sects[sect_idx_nlptr].size);
        }
        if (sect_idx_dyld >= 0) {
            while (out.len < sects[sect_idx_dyld].file_off)
                put8(&out, 0);
            obuf_zero(&out, sects[sect_idx_dyld].size);
        }
    }

    /* ---- Pad to __LINKEDIT, then indirect/sym/strtab. ---- */
    while (out.len < linkedit_file_off)
        put8(&out, 0);
    if (nstubs > 0) {
        while (out.len < indirect_file_off)
            put8(&out, 0);
        obuf_put(&out, indirect.buf, indirect.len);
    }
    while (out.len < sym_file_off)
        put8(&out, 0);
    obuf_put(&out, nlist.buf, nlist.len);
    while (out.len < str_file_off)
        put8(&out, 0);
    obuf_put(&out, strtab.buf, strtab.len);

    /* ---- Write file. ---- */
    unlink(filename);
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0755);
    if (fd < 0 || (fp = fdopen(fd, "wb")) == NULL) {
        tcc_error_noabort("could not write '%s': %s", filename, strerror(errno));
        if (fd >= 0) close(fd);
        goto cleanup;
    }
    if (fwrite(out.buf, 1, out.len, fp) != out.len) {
        tcc_error_noabort("short write to '%s'", filename);
        goto cleanup;
    }
    fclose(fp); fp = NULL;
    chmod(filename, 0755);
    if (s1->verbose)
        printf("<- %s (exe %u bytes, entry 0x%x, %d stubs)\n",
               filename, (unsigned)out.len, entry_addr, nstubs);
    ret = 0;

cleanup:
    if (fp) fclose(fp);
    tcc_free(out.buf);
    tcc_free(nlist.buf);
    tcc_free(strtab.buf);
    tcc_free(indirect.buf);
    tcc_free(text_data);
    tcc_free(rodata_data);
    tcc_free(data_data);
    tcc_free(stub_data);
    tcc_free(nl_ptr_data);
    tcc_free(stubs);
    tcc_free(stub_for_elfsym);
    tcc_free(stub_sym_idx);
    tcc_free(nlptrs);
    tcc_free(nl_for_elfsym);
    tcc_free(data_sym_idx);
    return ret;
}


ST_FUNC int macho_output_file(TCCState *s1, const char *filename)
{
    obuf out;
    struct macho_secmap *smap = NULL;
    int nsec = 0, smap_cap = 0;
    int i, j, ret = -1;
    FILE *fp = NULL;
    int fd;
    uint32_t hdr_and_lc_size;
    uint32_t segment_cmd_size;
    uint32_t cur_off;
    uint32_t vmaddr = 0;
    uint32_t total_filesize = 0, total_vmsize = 0;
    obuf relbuf;
    obuf indirect_buf;     /* indirect symbol table (4-byte words) */
    struct symtab_build sb;
    /* Patch offsets within the load-command region: */
    size_t   lc_segment_off;
    size_t   *section_lc_off = NULL;  /* per-emitted-section, offset in
                                       * out.buf of the section header,
                                       * for back-patching reloff/offset. */
    size_t   lc_symtab_off, lc_dysymtab_off;
    uint32_t reloc_file_off, sym_file_off, str_file_off;
    uint32_t indirect_file_off = 0, n_indirect = 0;
    /* External-stub bookkeeping (set up below). */
    struct extern_stub *stubs = NULL;
    int *stub_for_elfsym = NULL;
    int  nstubs = 0;
    int  stub_msect_no = 0;       /* 1-based mach-o section index of __picsymbolstub1 */
    int  la_ptr_msect_no = 0;     /* ditto __la_symbol_ptr */
    int  helper_elfsym_idx = 0;   /* dyld_stub_binding_helper in s1->symtab */
    /* External-data PIC bookkeeping. */
    struct extern_nlptr *nlptrs = NULL;
    int *nl_for_elfsym = NULL;
    int  n_nlptrs = 0;
    int  nl_ptr_msect_no = 0;     /* 1-based mach-o section index of __nl_symbol_ptr */

    memset(&out, 0, sizeof(out));
    memset(&relbuf, 0, sizeof(relbuf));
    memset(&indirect_buf, 0, sizeof(indirect_buf));
    memset(&sb, 0, sizeof(sb));

    /* ---- Step 0: dispatch by output type. ---- */
    if (s1->output_type == TCC_OUTPUT_EXE)
        return macho_output_exe(s1, filename);
    if (s1->output_type != TCC_OUTPUT_OBJ) {
        tcc_error_noabort("ppc-macho: only -c (object) and -o exe "
                          "(executable) outputs are implemented");
        return -1;
    }

    /* ---- Step 0.5: collect external functions called via R_PPC_REL24
     * and inject dyld_stub_binding_helper if any stubs are needed.
     * Also collect external data symbols referenced via the synthetic
     * R_PPC_HA16_PIC / R_PPC_LO16_PIC PIC relocs from ppc-gen.c. */
    nstubs = collect_extern_stubs(s1, &stubs, &stub_for_elfsym);
    if (nstubs > 0) {
        helper_elfsym_idx = set_elf_sym(s1->symtab, 0, 0,
                                        ELFW(ST_INFO)(STB_GLOBAL, STT_NOTYPE),
                                        0, SHN_UNDEF,
                                        "dyld_stub_binding_helper");
        (void)helper_elfsym_idx;
    }
    n_nlptrs = collect_extern_nlptrs(s1, &nlptrs, &nl_for_elfsym);

    /* ---- Step 1: collect Mach-O sections from tcc's section list. ----
     * Three extra slots reserved at the end for synthesized
     * __picsymbolstub1, __nl_symbol_ptr, and __la_symbol_ptr sections.
     * Sections appear in this order to match gcc-4.0 output. */
    smap_cap = s1->nb_sections + 3;
    smap = tcc_mallocz(smap_cap * sizeof(*smap));
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        const char *segname = NULL, *sectname = NULL;
        uint32_t flags = 0;
        if (!classify_section(s, &segname, &sectname, &flags))
            continue;
        if (s->data_offset == 0 && s->sh_type != SHT_NOBITS)
            continue;
        smap[nsec].elf = s;
        smap[nsec].segname = segname;
        smap[nsec].sectname = sectname;
        smap[nsec].flags = flags;
        smap[nsec].size = (uint32_t)s->data_offset;
        smap[nsec].align_log2 = addralign_log2(s->sh_addralign);
        smap[nsec].msect_no = nsec + 1;
        nsec++;
    }

    /* ---- Step 1a: synthesize __picsymbolstub1, __nl_symbol_ptr,
     * and __la_symbol_ptr in section-header order matching gcc-4.0:
     *
     *   __TEXT,__picsymbolstub1     (one entry per extern function call)
     *   __DATA,__nl_symbol_ptr      (one entry per extern data symbol)
     *   __DATA,__la_symbol_ptr      (one entry per extern function call)
     *
     * The indirect symbol table is laid out in the same section order:
     *   [0 .. nstubs)                     stub symbols
     *   [nstubs .. nstubs + n_nlptrs)     nl_ptr symbols
     *   [nstubs + n_nlptrs ..)            la_ptr symbols
     * Each section's reserved1 field gives its starting index in that
     * table. */
    if (nstubs > 0) {
        struct macho_secmap *m;
        m = &smap[nsec];
        m->elf = NULL;
        m->segname = "__TEXT";
        m->sectname = "__picsymbolstub1";
        m->flags = S_SYMBOL_STUBS | S_ATTR_PURE_INSTRUCTIONS
                 | S_ATTR_SOME_INSTRUCTIONS;
        m->size = (uint32_t)nstubs * 32u;
        m->align_log2 = 5;
        m->msect_no = nsec + 1;
        m->is_stub = 1;
        m->reserved2 = 32;          /* bytes per stub */
        m->reserved1 = 0;           /* first stub at indirect-symtab index 0 */
        m->data = tcc_mallocz(m->size);
        stub_msect_no = m->msect_no;
        nsec++;
    }
    if (n_nlptrs > 0) {
        struct macho_secmap *m;
        m = &smap[nsec];
        m->elf = NULL;
        m->segname = "__DATA";
        m->sectname = "__nl_symbol_ptr";
        m->flags = S_NON_LAZY_SYMBOL_POINTERS;
        m->size = (uint32_t)n_nlptrs * 4u;
        m->align_log2 = 2;
        m->msect_no = nsec + 1;
        m->is_nl_ptr = 1;
        m->reserved1 = (uint32_t)nstubs;    /* nl_ptrs start after stubs */
        m->data = tcc_mallocz(m->size);
        nl_ptr_msect_no = m->msect_no;
        nsec++;
    }
    if (nstubs > 0) {
        struct macho_secmap *m;
        m = &smap[nsec];
        m->elf = NULL;
        m->segname = "__DATA";
        m->sectname = "__la_symbol_ptr";
        m->flags = S_LAZY_SYMBOL_POINTERS;
        m->size = (uint32_t)nstubs * 4u;
        m->align_log2 = 2;
        m->msect_no = nsec + 1;
        m->is_la_ptr = 1;
        m->reserved1 = (uint32_t)(nstubs + n_nlptrs);
        m->data = tcc_mallocz(m->size);
        la_ptr_msect_no = m->msect_no;
        nsec++;
    }

    /* (build_symtab is deferred until after layout so n_value can
     * include each section's segment-relative addr.) */

    /* ---- Step 2: compute layout. ----
     *
     * Header + LC_SEGMENT(+nsec sections) + LC_SYMTAB + LC_DYSYMTAB.
     * Then section data starting after the load commands, aligned to
     * each section's alignment. Then per-section relocs, then symtab,
     * then strtab. */
    segment_cmd_size = MACHO_SEGMENT_CMD_SIZE + nsec * MACHO_SECTION_SIZE;
    hdr_and_lc_size = MACHO_HEADER_SIZE
                    + segment_cmd_size
                    + MACHO_SYMTAB_CMD_SIZE
                    + MACHO_DYSYMTAB_CMD_SIZE;

    /* Section data starts immediately after load commands, aligned
     * to at least 4 bytes (PPC instructions are 4-byte aligned). */
    cur_off = hdr_and_lc_size;
    cur_off = (cur_off + 3) & ~3u;

    /* Assign each section its file offset and vmaddr. Zerofill (BSS)
     * sections take vmsize but no file space. We lay zerofills *last*
     * within the segment because Mach-O kernels won't load file data
     * after a zerofill. */
    for (i = 0; i < nsec; i++) {
        uint32_t a = 1u << smap[i].align_log2;
        if ((smap[i].flags & 0xff) == S_ZEROFILL)
            continue;
        cur_off = (cur_off + (a - 1)) & ~(a - 1);
        vmaddr = (vmaddr + (a - 1)) & ~(a - 1);
        smap[i].offset = cur_off;
        smap[i].addr = vmaddr;
        cur_off += smap[i].size;
        vmaddr += smap[i].size;
    }
    total_filesize = cur_off - hdr_and_lc_size;
    /* Now zerofill sections (vmsize only, no file offset). */
    for (i = 0; i < nsec; i++) {
        uint32_t a;
        if ((smap[i].flags & 0xff) != S_ZEROFILL)
            continue;
        a = 1u << smap[i].align_log2;
        vmaddr = (vmaddr + (a - 1)) & ~(a - 1);
        smap[i].offset = 0;
        smap[i].addr = vmaddr;
        vmaddr += smap[i].size;
    }
    total_vmsize = vmaddr;

    /* ---- Step 2.4: now that section addresses are set, fill in
     * stubs[].addr / stubs[].la_ptr_addr / nlptrs[].slot_addr. Done
     * before the relocate-bytes pass so the addis/lwz instruction
     * displacements (slot - PIC anchor) and BR24 displacements (stub -
     * call site) can be pre-encoded into the section bytes. */
    if (nstubs > 0) {
        int sidx;
        struct macho_secmap *stub_sect = NULL, *la_sect = NULL;
        for (j = 0; j < nsec; j++) {
            if (smap[j].is_stub)   stub_sect = &smap[j];
            if (smap[j].is_la_ptr) la_sect   = &smap[j];
        }
        for (sidx = 0; sidx < nstubs; sidx++) {
            stubs[sidx].addr        = stub_sect->addr + (uint32_t)sidx * 32u;
            stubs[sidx].la_ptr_addr = la_sect->addr   + (uint32_t)sidx * 4u;
        }
    }
    if (n_nlptrs > 0) {
        int nidx;
        struct macho_secmap *nl_sect = NULL;
        for (j = 0; j < nsec; j++)
            if (smap[j].is_nl_ptr) { nl_sect = &smap[j]; break; }
        for (nidx = 0; nidx < n_nlptrs; nidx++)
            nlptrs[nidx].slot_addr = nl_sect->addr + (uint32_t)nidx * 4u;
    }

    /* ---- Step 2.5: pre-apply same-section relocations to section
     * bytes.
     *
     * We need the in-section instruction bits to encode the resolved
     * branch displacement (and absolute references) assuming the
     * final layout. Apple's classic linker treats the existing bits
     * in the instruction as the *addend* and only adds a delta if it
     * moves a section -- so a zeroed `bl` would produce a loop. We
     * walk each reloc and call tcc's per-target relocate() helper to
     * patch the bytes in place.
     *
     * For external symbols (st_shndx == SHN_UNDEF) we leave the bits
     * as the addend would be -- 0 with no addend in ELF Rel format.
     * The Mach-O linker resolves them at link time using the symbol
     * value. */
    for (i = 0; i < nsec; i++) {
        Section *sr;
        Section *s = smap[i].elf;
        int k;
        if (!s) continue;     /* synthesized section: no relocs of its own */
        sr = s->reloc;
        if (!sr || !s->data)
            continue;
        for (k = 0; k < (int)(sr->data_offset / sizeof(ElfW_Rel)); k++) {
            ElfW_Rel *rel = (ElfW_Rel *)sr->data + k;
            int type = ELFW(R_TYPE)(rel->r_info);
            int symidx = ELFW(R_SYM)(rel->r_info);
            ElfW(Sym) *esym = (ElfW(Sym) *)s1->symtab->data + symidx;
            unsigned char *ptr;
            addr_t reloc_addr, sym_val;

            if (esym->st_shndx == SHN_UNDEF) {
                /* External REL24: route to a stub if we have one. The
                 * `bl` displacement encoded here (stub_addr - reloc_addr)
                 * is what the BR24 reloc carries as its addend. */
                if (type == R_PPC_REL24
                    && stub_for_elfsym
                    && stub_for_elfsym[symidx] >= 0) {
                    int sidx = stub_for_elfsym[symidx];
                    sym_val = stubs[sidx].addr;
                    reloc_addr = smap[i].addr + rel->r_offset;
                    ptr = s->data + rel->r_offset;
                    relocate(s1, rel, type, ptr, reloc_addr, sym_val);
                }
                /* PIC HA16/LO16 to extern data: encode the
                 * (slot_va - anchor_va) displacement directly into
                 * the addis/lwz immediate. ld will recompute via
                 * SECTDIFF if either section moves. */
                if ((type == R_PPC_HA16_PIC || type == R_PPC_LO16_PIC)
                    && nl_for_elfsym
                    && nl_for_elfsym[symidx] >= 0) {
                    int nidx = nl_for_elfsym[symidx];
                    int anchor_off = ppc_pic_pairs_lookup((int)rel->r_offset);
                    uint32_t slot_va, anchor_va;
                    int32_t  delta;
                    uint32_t orig, imm;
                    if (anchor_off < 0) {
                        tcc_error_noabort("ppc-macho: missing PIC anchor "
                                          "for reloc at 0x%lx",
                                          (unsigned long)rel->r_offset);
                        continue;
                    }
                    slot_va  = nlptrs[nidx].slot_addr;
                    anchor_va = smap[i].addr + (uint32_t)anchor_off;
                    delta = (int32_t)(slot_va - anchor_va);
                    ptr = s->data + rel->r_offset;
                    orig = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16)
                         | ((uint32_t)ptr[2] << 8)  | (uint32_t)ptr[3];
                    if (type == R_PPC_HA16_PIC) {
                        imm = ((uint32_t)((delta + 0x8000) >> 16)) & 0xffff;
                    } else {
                        imm = ((uint32_t)delta) & 0xffff;
                    }
                    orig = (orig & ~0xffffu) | imm;
                    ptr[0] = (orig >> 24) & 0xff;
                    ptr[1] = (orig >> 16) & 0xff;
                    ptr[2] = (orig >>  8) & 0xff;
                    ptr[3] =  orig        & 0xff;
                }
                /* Other external relocs (HA16/LO16 to globals, ADDR32):
                 * leave the bits as their addend; ld will fix up. */
                continue;
            }

            /* PIC reloc against a LOCALLY-DEFINED symbol. tcc's
             * ppc_need_pic_for_sym() decided to emit PIC at codegen
             * time based on st_shndx == SHN_UNDEF, but the symbol
             * was DEFINED later in the same TU (a forward-reference).
             * No __nl_symbol_ptr slot was reserved for it. We rewrite
             * the bytes to compute the address directly via SECTDIFF
             * from the PIC base:
             *   addis r12, r2, ha(sym - anchor)     ; unchanged opcode
             *   lwz   r12, lo(sym - anchor)(r12)  ->  addi r12, r12, lo(...)
             * The `lwz` (op 0x80) becomes `addi` (op 0x38). Both have
             * the same RT/RA/SIMM field layout. */
            if (type == R_PPC_HA16_PIC || type == R_PPC_LO16_PIC) {
                int anchor_off = ppc_pic_pairs_lookup((int)rel->r_offset);
                uint32_t target_va, anchor_va;
                int32_t  delta;
                uint32_t orig, imm;
                int j;
                if (anchor_off < 0) {
                    tcc_error_noabort("ppc-macho: missing PIC anchor "
                                      "for local reloc at 0x%lx",
                                      (unsigned long)rel->r_offset);
                    continue;
                }
                /* Compute symbol's final VA. */
                target_va = 0;
                for (j = 0; j < nsec; j++) {
                    if (smap[j].elf
                        && smap[j].elf->sh_num == esym->st_shndx) {
                        target_va = smap[j].addr + esym->st_value;
                        break;
                    }
                }
                if (j == nsec) continue;  /* unmapped section: skip */
                anchor_va = smap[i].addr + (uint32_t)anchor_off;
                delta = (int32_t)(target_va - anchor_va);
                ptr = s->data + rel->r_offset;
                orig = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16)
                     | ((uint32_t)ptr[2] << 8)  | (uint32_t)ptr[3];
                if (type == R_PPC_HA16_PIC) {
                    /* Keep addis opcode, just patch immediate. */
                    imm = ((uint32_t)((delta + 0x8000) >> 16)) & 0xffff;
                    orig = (orig & ~0xffffu) | imm;
                } else {
                    /* Convert lwz (0x80xxxxxx) to addi (0x38xxxxxx).
                     * Patch immediate to lo(delta). */
                    imm = ((uint32_t)delta) & 0xffff;
                    orig = (orig & ~0xfc00ffffu) | 0x38000000u | imm;
                }
                ptr[0] = (orig >> 24) & 0xff;
                ptr[1] = (orig >> 16) & 0xff;
                ptr[2] = (orig >>  8) & 0xff;
                ptr[3] =  orig        & 0xff;
                /* The reloc record itself will be emitted as
                 * SECTDIFF-against-symbol-VA in emit_section_relocs;
                 * we just patched the bytes here. */
                continue;
            }
            /* Synthetic PIC types should never be applied via the
             * generic relocate() path -- they have no in-section
             * meaning that relocate() understands. They should also
             * never reference defined symbols (ppc_need_pic_for_sym
             * returns 0 in that case), but if the symbol got defined
             * later (forward decl + later definition in the same TU)
             * we'd see one here. Skip silently rather than crash. */
            if (type == R_PPC_HA16_PIC || type == R_PPC_LO16_PIC)
                continue;
            /* Compute the symbol's final address in our layout. */
            sym_val = 0;
            {
                int j;
                for (j = 0; j < nsec; j++) {
                    if (smap[j].elf
                        && smap[j].elf->sh_num == esym->st_shndx) {
                        sym_val = smap[j].addr + esym->st_value;
                        break;
                    }
                }
                if (j == nsec) continue;  /* unmapped section: skip */
            }
            reloc_addr = smap[i].addr + rel->r_offset;
            ptr = s->data + rel->r_offset;
            (void)type;
            relocate(s1, rel, type, ptr, reloc_addr, sym_val);
        }
    }

    /* Materialize the synthesized stub bytes (la_symbol_ptr slots
     * stay zero; ld fixes them via VANILLA reloc). */
    if (nstubs > 0) {
        int sidx;
        struct macho_secmap *stub_sect = NULL;
        for (j = 0; j < nsec; j++)
            if (smap[j].is_stub) { stub_sect = &smap[j]; break; }
        for (sidx = 0; sidx < nstubs; sidx++)
            emit_pic_stub_bytes(stub_sect->data + sidx * 32,
                                stubs[sidx].addr,
                                stubs[sidx].la_ptr_addr);
    }

    /* Build symbol table now that section addresses are known. */
    build_symtab(s1, &sb, smap, nsec, stub_for_elfsym);
    if (nstubs > 0) {
        int sidx;
        for (sidx = 0; sidx < nstubs; sidx++) {
            int eidx = stubs[sidx].elfsym_idx;
            stubs[sidx].machsym_idx = sb.xlat[eidx];
        }
    }
    if (n_nlptrs > 0) {
        int nidx;
        for (nidx = 0; nidx < n_nlptrs; nidx++) {
            int eidx = nlptrs[nidx].elfsym_idx;
            nlptrs[nidx].machsym_idx = sb.xlat[eidx];
        }
    }

    /* Per-section relocations. We emit them in section order; record
     * each section's reloff/nreloc. Mach-O relocation entries are 8-byte
     * structs (two 32-bit words), and ld requires reloff to be 4-byte
     * aligned, so pad section-data end up to a 4-byte boundary first. */
    cur_off = (cur_off + 3u) & ~3u;
    reloc_file_off = cur_off;
    for (i = 0; i < nsec; i++) {
        size_t before = relbuf.len;
        int n = 0;

        if (smap[i].is_stub) {
            /* For each stub: emit scattered HA16_SECTDIFF + PAIR + LO16
             * + PAIR. The Mach-O ABI splits the displacement
             * (la_ptr - anchor) across the pair: the *primary* entry's
             * r_value is the la_ptr (target) address, and the PAIR
             * entry's r_value is the anchor address. The PAIR's
             * r_address (a 24-bit field, repurposed in scattered relocs)
             * carries the *other half* of the displacement so the
             * linker can recompute it after section relocation. */
            int sidx;
            for (sidx = 0; sidx < nstubs; sidx++) {
                uint32_t stub_off = (uint32_t)sidx * 32u;
                uint32_t addis_addr = stub_off + 0xc;
                uint32_t lwz_addr   = stub_off + 0x14;
                uint32_t anchor     = stubs[sidx].addr + 8;
                uint32_t la_ptr     = stubs[sidx].la_ptr_addr;
                int32_t  delta      = (int32_t)(la_ptr - anchor);
                uint16_t lo         = (uint16_t)(delta & 0xffff);
                uint16_t ha         = (uint16_t)((delta + 0x8000) >> 16);

                /* HA16_SECTDIFF on the addis: PAIR's r_address = lo. */
                emit_scattered(&relbuf, 0, 2, PPC_RELOC_HA16_SECTDIFF,
                               addis_addr, la_ptr);
                emit_scattered(&relbuf, 0, 2, PPC_RELOC_PAIR,
                               (uint32_t)lo, anchor);
                /* LO16_SECTDIFF on the lwz: PAIR's r_address = ha. */
                emit_scattered(&relbuf, 0, 2, PPC_RELOC_LO16_SECTDIFF,
                               lwz_addr, la_ptr);
                emit_scattered(&relbuf, 0, 2, PPC_RELOC_PAIR,
                               (uint32_t)ha, anchor);
                n += 4;
            }
        } else if (smap[i].is_la_ptr) {
            /* Each la_symbol_ptr slot needs a VANILLA reloc against
             * dyld_stub_binding_helper so dyld knows what to write
             * the first time the stub is called. */
            int sidx;
            int helper_machsym = sb.xlat[helper_elfsym_idx];
            for (sidx = 0; sidx < nstubs; sidx++) {
                uint32_t la_off = (uint32_t)sidx * 4u;
                put32be(&relbuf, la_off);
                put32be(&relbuf, pack_reloc_word((uint32_t)helper_machsym,
                                                 0, 2, 1, PPC_RELOC_VANILLA));
                n++;
            }
        } else if (smap[i].elf) {
            struct reloc_ctx ctx;
            ctx.s1 = s1;
            ctx.smap = smap;
            ctx.nsec = nsec;
            ctx.sym_xlat = sb.xlat;
            ctx.sym_isext = sb.isext;
            ctx.xlat_present = sb.xlat_present;
            ctx.stub_for_elfsym = stub_for_elfsym;
            ctx.stub_msect_no = stub_msect_no;
            ctx.nl_for_elfsym = nl_for_elfsym;
            ctx.nlptrs = nlptrs;
            ctx.n_nlptrs = n_nlptrs;
            n = emit_section_relocs(&relbuf, smap[i].elf, &ctx);
        }
        if (n > 0) {
            smap[i].reloff = reloc_file_off + (uint32_t)before;
            smap[i].nreloc = n;
        } else {
            smap[i].reloff = 0;
            smap[i].nreloc = 0;
        }
    }
    cur_off += (uint32_t)relbuf.len;

    /* Build the indirect symbol table. Layout matches gcc-4.0:
     *   [0 .. nstubs)               stub entries  (one per extern fn call)
     *   [nstubs .. +n_nlptrs)       nl_ptr entries (one per extern data ref)
     *   [+nstubs)                   la_ptr entries (one per extern fn call)
     * Each entry is a 4-byte word holding the index of the corresponding
     * undef-external symbol in our nlist. */
    {
        int sidx;
        for (sidx = 0; sidx < nstubs; sidx++)
            put32be(&indirect_buf, (uint32_t)stubs[sidx].machsym_idx);
        for (sidx = 0; sidx < n_nlptrs; sidx++)
            put32be(&indirect_buf, (uint32_t)nlptrs[sidx].machsym_idx);
        for (sidx = 0; sidx < nstubs; sidx++)
            put32be(&indirect_buf, (uint32_t)stubs[sidx].machsym_idx);
        n_indirect = (uint32_t)(nstubs * 2 + n_nlptrs);
    }
    cur_off = (cur_off + 3u) & ~3u;
    indirect_file_off = cur_off;
    cur_off += (uint32_t)indirect_buf.len;

    /* Symbol table, string table. ld requires symoff to be 4-byte aligned;
     * relbuf is always a multiple of 8 bytes, so cur_off is already aligned,
     * but pad defensively in case future changes break that invariant. */
    cur_off = (cur_off + 3u) & ~3u;
    sym_file_off = cur_off;
    cur_off += sb.nlist.len;
    str_file_off = cur_off;
    cur_off += sb.strtab.len;

    /* ---- Step 3: serialize the file. ---- */

    /* Mach header. */
    put32be(&out, MH_MAGIC);
    put32be(&out, CPU_TYPE_POWERPC);
    put32be(&out, CPU_SUBTYPE_POWERPC_ALL);
    put32be(&out, MH_OBJECT);
    put32be(&out, 3);  /* ncmds: SEGMENT + SYMTAB + DYSYMTAB */
    put32be(&out, segment_cmd_size + MACHO_SYMTAB_CMD_SIZE
                  + MACHO_DYSYMTAB_CMD_SIZE);
    put32be(&out, MH_SUBSECTIONS_VIA_SYMBOLS);

    /* LC_SEGMENT. For MH_OBJECT, segname is empty (16 NUL bytes) and
     * vmaddr/vmsize cover all sections, fileoff/filesize cover the
     * non-zerofill parts. */
    lc_segment_off = out.len;
    put32be(&out, LC_SEGMENT);
    put32be(&out, segment_cmd_size);
    obuf_zero(&out, 16);  /* segname */
    put32be(&out, 0);              /* vmaddr (we use 0 base) */
    put32be(&out, total_vmsize);   /* vmsize */
    put32be(&out, hdr_and_lc_size + ((4u - (hdr_and_lc_size & 3u)) & 3u));
                                   /* fileoff: where section data starts */
    /* Correct fileoff: it's the smallest section's file offset.
     * Recompute as the post-pad cur_off we used. */
    /* (Patch below using actual smap[0].offset of first non-zerofill.) */
    {
        uint32_t fileoff = 0;
        for (j = 0; j < nsec; j++) {
            if ((smap[j].flags & 0xff) != S_ZEROFILL) {
                fileoff = smap[j].offset;
                break;
            }
        }
        /* Replace last write (fileoff). */
        out.len -= 4;
        put32be(&out, fileoff);
    }
    put32be(&out, total_filesize);     /* filesize */
    put32be(&out, VM_PROT_DEFAULT);    /* maxprot */
    put32be(&out, VM_PROT_DEFAULT);    /* initprot */
    put32be(&out, nsec);               /* nsects */
    put32be(&out, 0);                  /* flags */

    /* Section headers. Track each one's offset in out.buf so we can
     * back-patch reloff after all sections are written. (We've
     * already computed everything, so just write straight through.) */
    section_lc_off = tcc_mallocz(nsec * sizeof(*section_lc_off));
    for (i = 0; i < nsec; i++) {
        char nm[16];
        section_lc_off[i] = out.len;

        /* sectname (16 bytes, NUL-padded). */
        memset(nm, 0, sizeof(nm));
        strncpy(nm, smap[i].sectname, 16);
        obuf_put(&out, nm, 16);
        /* segname (16 bytes). */
        memset(nm, 0, sizeof(nm));
        strncpy(nm, smap[i].segname, 16);
        obuf_put(&out, nm, 16);
        put32be(&out, smap[i].addr);
        put32be(&out, smap[i].size);
        put32be(&out, smap[i].offset);
        put32be(&out, smap[i].align_log2);
        put32be(&out, smap[i].reloff);
        put32be(&out, smap[i].nreloc);
        put32be(&out, smap[i].flags);
        put32be(&out, smap[i].reserved1);
        put32be(&out, smap[i].reserved2);
    }

    /* LC_SYMTAB. */
    lc_symtab_off = out.len;
    put32be(&out, LC_SYMTAB);
    put32be(&out, MACHO_SYMTAB_CMD_SIZE);
    put32be(&out, sym_file_off);
    put32be(&out, sb.ntotal);
    put32be(&out, str_file_off);
    put32be(&out, (uint32_t)sb.strtab.len);

    /* LC_DYSYMTAB. */
    lc_dysymtab_off = out.len;
    put32be(&out, LC_DYSYMTAB);
    put32be(&out, MACHO_DYSYMTAB_CMD_SIZE);
    put32be(&out, 0);                /* ilocalsym  */
    put32be(&out, sb.nlocal);        /* nlocalsym  */
    put32be(&out, sb.nlocal);        /* iextdefsym */
    put32be(&out, sb.nextdef);       /* nextdefsym */
    put32be(&out, sb.nlocal + sb.nextdef);  /* iundefsym  */
    put32be(&out, sb.nundef);        /* nundefsym  */
    put32be(&out, 0);                /* tocoff       */
    put32be(&out, 0);                /* ntoc         */
    put32be(&out, 0);                /* modtaboff    */
    put32be(&out, 0);                /* nmodtab      */
    put32be(&out, 0);                /* extrefsymoff */
    put32be(&out, 0);                /* nextrefsyms  */
    put32be(&out, n_indirect ? indirect_file_off : 0); /* indirectsymoff */
    put32be(&out, n_indirect);       /* nindirectsyms */
    put32be(&out, 0);                /* extreloff    */
    put32be(&out, 0);                /* nextrel      */
    put32be(&out, 0);                /* locreloff    */
    put32be(&out, 0);                /* nlocrel      */

    /* (suppress unused warnings) */
    (void)lc_segment_off; (void)lc_symtab_off; (void)lc_dysymtab_off;

    /* Pad to where section data starts. */
    while (out.len < (size_t)hdr_and_lc_size)
        put8(&out, 0);
    while ((out.len & 3u) != 0)
        put8(&out, 0);

    /* Section data, in the order we assigned offsets. */
    for (i = 0; i < nsec; i++) {
        uint32_t a;
        const unsigned char *src = NULL;
        if ((smap[i].flags & 0xff) == S_ZEROFILL)
            continue;
        a = 1u << smap[i].align_log2;
        while (out.len < smap[i].offset)
            put8(&out, 0);
        (void)a;
        if (smap[i].data)
            src = smap[i].data;             /* synthesized section */
        else if (smap[i].elf && smap[i].elf->data)
            src = smap[i].elf->data;
        if (src && smap[i].size)
            obuf_put(&out, src, smap[i].size);
    }

    /* Relocations. */
    if (relbuf.len) {
        while (out.len < reloc_file_off)
            put8(&out, 0);
        obuf_put(&out, relbuf.buf, relbuf.len);
    }

    /* Indirect symbol table (if any stubs). */
    if (n_indirect) {
        while (out.len < indirect_file_off)
            put8(&out, 0);
        obuf_put(&out, indirect_buf.buf, indirect_buf.len);
    }

    /* Symbol table, string table. */
    while (out.len < sym_file_off)
        put8(&out, 0);
    if (sb.nlist.len)
        obuf_put(&out, sb.nlist.buf, sb.nlist.len);
    while (out.len < str_file_off)
        put8(&out, 0);
    if (sb.strtab.len)
        obuf_put(&out, sb.strtab.buf, sb.strtab.len);

    /* ---- Step 4: write to disk. ---- */
    unlink(filename);
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd < 0 || (fp = fdopen(fd, "wb")) == NULL) {
        tcc_error_noabort("could not write '%s': %s", filename, strerror(errno));
        if (fd >= 0) close(fd);
        goto cleanup;
    }
    if (fwrite(out.buf, 1, out.len, fp) != out.len) {
        tcc_error_noabort("short write to '%s'", filename);
        goto cleanup;
    }
    if (s1->verbose)
        printf("<- %s\n", filename);
    ret = 0;

cleanup:
    if (fp) fclose(fp);
    free_symtab(&sb);
    tcc_free(relbuf.buf);
    tcc_free(indirect_buf.buf);
    tcc_free(out.buf);
    if (smap) {
        for (i = 0; i < nsec; i++)
            tcc_free(smap[i].data);
        tcc_free(smap);
    }
    tcc_free(stubs);
    tcc_free(stub_for_elfsym);
    tcc_free(nlptrs);
    tcc_free(nl_for_elfsym);
    tcc_free(section_lc_off);
    /* Reset PIC pair side-table for the next translation unit. */
    ppc_pic_pairs_reset();
    return ret;
}

/* ====================================================================
 * Other exports (unchanged from ppc-macho-stubs.c).
 * ================================================================== */

ST_FUNC void tcc_add_macos_sdkpath(TCCState *s)
{
    /* Tiger has no SDK indirection: libraries live in /usr/lib,
     * headers in /usr/include. configure sets up /usr/include; we
     * just need to add /usr/lib as a library search path. */
    tcc_add_library_path(s, "/usr/lib");
}

ST_FUNC int macho_load_dll(TCCState *s1, int fd, const char *filename, int lev)
{
    (void)s1; (void)fd; (void)lev;
    tcc_error_noabort("ppc-macho: dynamic library loading not yet "
                      "supported (file: %s)", filename);
    return -1;
}

ST_FUNC int macho_load_tbd(TCCState *s1, int fd, const char *filename, int lev)
{
    (void)s1; (void)fd; (void)lev;
    tcc_error_noabort("ppc-macho: .tbd library loading not yet "
                      "supported (file: %s)", filename);
    return -1;
}

ST_FUNC char *macho_tbd_soname(int fd)
{
    (void)fd;
    return NULL;
}

/* PowerPC icache flush. tccrun.c calls __clear_cache() after writing
 * JITted code; libgcc on Tiger doesn't ship this symbol so we provide
 * it ourselves. The dcbst/sync/icbi/sync/isync sequence is the
 * canonical PPC cache-coherency dance. */
#if defined __APPLE__ && defined __ppc__
#ifndef __TINYC__
/* GCC version: real PPC cache-flush sequence in inline asm. */
void __clear_cache(void *begin, void *end)
{
    char *p = (char *)begin;
    char *e = (char *)end;
    /* Round down to cache-line boundary (32 bytes covers G3/G4/G5). */
    char *p0 = (char *)((uintptr_t)p & ~31);
    char *q;
    for (q = p0; q < e; q += 32)
        __asm__ volatile("dcbst 0, %0" : : "r"(q) : "memory");
    __asm__ volatile("sync" : : : "memory");
    for (q = p0; q < e; q += 32)
        __asm__ volatile("icbi 0, %0" : : "r"(q) : "memory");
    __asm__ volatile("sync" : : : "memory");
    __asm__ volatile("isync" : : : "memory");
}
#else
/* When tcc compiles itself: tcc has no PPC inline-asm parser yet
 * (ppc-asm.c deferred). Stub it so the bootstrap completes; the
 * resulting tcc-self can produce object files but its own -run
 * mode would skip cache flushing. To re-enable -run on tcc-self,
 * link the gcc-built __clear_cache as a separate object. */
void __clear_cache(void *begin, void *end)
{
    (void)begin; (void)end;
}
#endif
#endif
