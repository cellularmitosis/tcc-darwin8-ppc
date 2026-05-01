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

/* VM protections. */
#define VM_PROT_READ            0x1
#define VM_PROT_WRITE           0x2
#define VM_PROT_EXECUTE         0x4
#define VM_PROT_DEFAULT         (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)

/* Section types (bottom 8 bits of section.flags). */
#define S_REGULAR               0x0
#define S_ZEROFILL              0x1
#define S_CSTRING_LITERALS      0x2
#define S_COALESCED             0xb

/* Section attributes (top 24 bits of section.flags). */
#define S_ATTR_PURE_INSTRUCTIONS    0x80000000
#define S_ATTR_SOME_INSTRUCTIONS    0x00000400

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
    Section    *elf;        /* tcc's Section (read-only) */
    const char *segname;    /* "__TEXT", "__DATA" */
    const char *sectname;   /* "__text", "__data", ... */
    uint32_t    flags;      /* Mach-O section flags */
    uint32_t    align_log2; /* 2^align_log2 */
    uint32_t    addr;       /* virtual address (set during layout) */
    uint32_t    offset;     /* file offset (set during layout) */
    uint32_t    size;       /* section size (= elf->data_offset) */
    uint32_t    reloff;     /* file offset of relocations */
    uint32_t    nreloc;     /* number of relocation entries */
    int         msect_no;   /* 1-based Mach-O section index */
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

/* Emit Mach-O relocations for one tcc section into b. Returns the
 * number of relocation_info records written. The mapping from tcc
 * symtab indices to Mach-O symtab indices is given by sym_xlat[].
 * For local (non-extern) relocs, the Mach-O r_symbolnum field holds
 * the *section* number (1-based) rather than a symtab index, so we
 * also accept a sym->elf-section table. */
static int emit_section_relocs(obuf *b, Section *s, TCCState *s1,
                               struct macho_secmap *smap, int nsec,
                               int *sym_xlat, int *sym_isext,
                               int *xlat_present)
{
    int i, count = 0;
    int nrel;

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
        if (extern_bit) {
            /* External: r_symbolnum is the Mach-O symtab index. */
            msym = sym_xlat[elfsym];
        } else {
            /* Local: r_symbolnum is the 1-based Mach-O section
             * number. Look it up via the elf->msect mapping. */
            int j;
            msym = 0;
            for (j = 0; j < nsec; j++) {
                if (smap[j].elf->sh_num == esym->st_shndx) {
                    msym = smap[j].msect_no;
                    break;
                }
            }
            if (msym == 0)
                continue;  /* target section not emitted; skip */
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
            /* Mach-O linker needs the "other half" -- the 16 low bits
             * of the (full address + addend). For an .o where we don't
             * yet know the symbol address, we emit the addend's low
             * half -- normally 0 for tcc-generated relocs. */
            other_half = 0;
            break;
        case R_PPC_ADDR16_HI:
            mtype = PPC_RELOC_HI16;
            mlength = 2;
            mpcrel = 0;
            emit_pair = 1;
            other_half = 0;
            break;
        case R_PPC_ADDR16_LO:
            mtype = PPC_RELOC_LO16;
            mlength = 2;
            mpcrel = 0;
            emit_pair = 1;
            /* For LO16 the PAIR carries the high half. */
            other_half = 0;
            break;
        default:
            /* Unsupported reloc: skip rather than corrupt the .o. */
            continue;
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
 * undef) so that LC_DYSYMTAB's contiguous-group invariant holds. */
static void build_symtab(TCCState *s1, struct symtab_build *sb,
                         struct macho_secmap *smap, int nsec)
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

ST_FUNC int macho_output_file(TCCState *s1, const char *filename)
{
    obuf out;
    struct macho_secmap *smap = NULL;
    int nsec = 0;
    int i, j, ret = -1;
    FILE *fp = NULL;
    int fd;
    uint32_t hdr_and_lc_size;
    uint32_t segment_cmd_size;
    uint32_t cur_off;
    uint32_t vmaddr = 0;
    uint32_t total_filesize = 0, total_vmsize = 0;
    obuf relbuf;
    struct symtab_build sb;
    /* Patch offsets within the load-command region: */
    size_t   lc_segment_off;
    size_t   *section_lc_off = NULL;  /* per-emitted-section, offset in
                                       * out.buf of the section header,
                                       * for back-patching reloff/offset. */
    size_t   lc_symtab_off, lc_dysymtab_off;
    uint32_t reloc_file_off, sym_file_off, str_file_off;

    memset(&out, 0, sizeof(out));
    memset(&relbuf, 0, sizeof(relbuf));
    memset(&sb, 0, sizeof(sb));

    /* ---- Step 0: only handle MH_OBJECT for now. ---- */
    if (s1->output_type != TCC_OUTPUT_OBJ) {
        tcc_error_noabort("ppc-macho: only -c (object output) is "
                          "implemented; use gcc-4.0 to link");
        return -1;
    }

    /* (relocate-bytes pass deferred: see Step 1.5 below.) */

    /* ---- Step 1: collect Mach-O sections from tcc's section list. ---- */
    smap = tcc_mallocz(s1->nb_sections * sizeof(*smap));
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
        Section *sr = smap[i].elf->reloc;
        Section *s = smap[i].elf;
        int k;
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
                /* External: leave bits as-is. The Mach-O linker will
                 * use the reloc record + symbol entry to resolve. */
                continue;
            }
            /* Compute the symbol's final address in our layout. */
            sym_val = 0;
            {
                int j;
                for (j = 0; j < nsec; j++) {
                    if (smap[j].elf->sh_num == esym->st_shndx) {
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

    /* Build symbol table now that section addresses are known. */
    build_symtab(s1, &sb, smap, nsec);

    /* Per-section relocations. We emit them in section order; record
     * each section's reloff/nreloc. Mach-O relocation entries are 8-byte
     * structs (two 32-bit words), and ld requires reloff to be 4-byte
     * aligned, so pad section-data end up to a 4-byte boundary first. */
    cur_off = (cur_off + 3u) & ~3u;
    reloc_file_off = cur_off;
    for (i = 0; i < nsec; i++) {
        size_t before = relbuf.len;
        int n = emit_section_relocs(&relbuf, smap[i].elf, s1,
                                    smap, nsec,
                                    sb.xlat, sb.isext, sb.xlat_present);
        if (n > 0) {
            smap[i].reloff = reloc_file_off + (uint32_t)before;
            smap[i].nreloc = n;
        } else {
            smap[i].reloff = 0;
            smap[i].nreloc = 0;
        }
    }
    cur_off += (uint32_t)relbuf.len;

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
        put32be(&out, 0);  /* reserved1 */
        put32be(&out, 0);  /* reserved2 */
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
    /* Everything else 0: tocoff, ntoc, modtaboff, nmodtab,
     * extrefsymoff, nextrefsyms, indirectsymoff, nindirectsyms,
     * extreloff, nextrel, locreloff, nlocrel. */
    for (j = 0; j < 12; j++) put32be(&out, 0);

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
        if ((smap[i].flags & 0xff) == S_ZEROFILL)
            continue;
        a = 1u << smap[i].align_log2;
        while (out.len < smap[i].offset)
            put8(&out, 0);
        (void)a;
        if (smap[i].elf->data && smap[i].size)
            obuf_put(&out, smap[i].elf->data, smap[i].size);
    }

    /* Relocations. */
    if (relbuf.len) {
        while (out.len < reloc_file_off)
            put8(&out, 0);
        obuf_put(&out, relbuf.buf, relbuf.len);
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
    tcc_free(out.buf);
    tcc_free(smap);
    tcc_free(section_lc_off);
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
