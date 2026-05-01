# tccmacho.c PowerPC 32-bit Porting: Complete Surgical Survey

**File:** `/Users/cell/claude/tcc-darwin8-ppc/tcc/tccmacho.c` (2,476 lines)  
**Target:** 32-bit PowerPC (Apple PPC) Mach-O on Mac OS X 10.4 Tiger  
**Current Support:** x86_64 and arm64 only  
**Survey Date:** 2026-04-30

---

## 1. PLATFORM CHECK (Line 34-36)

**Location:** Lines 34-36

```c
#if !defined TCC_TARGET_X86_64 && !defined TCC_TARGET_ARM64
#error Platform not supported
#endif
```

**Description:** Compile-time guard preventing this file from being used on unsupported architectures.

**PPC32 Requirement:**
```c
#if !defined TCC_TARGET_X86_64 && !defined TCC_TARGET_ARM64 && !defined TCC_TARGET_PPC
#error Platform not supported
#endif
```

**Difficulty:** Trivial

---

## 2. CPU TYPE & SUBTYPE CONSTANTS (Lines 49-58)

**Location:** Lines 49-58

```c
#define CPU_SUBTYPE_LIB64       (0x80000000)
#define CPU_SUBTYPE_X86_ALL     (3)
#define CPU_SUBTYPE_ARM64_ALL   (0)

#define CPU_ARCH_ABI64          (0x01000000)

#define CPU_TYPE_X86            (7)
#define CPU_TYPE_X86_64         (CPU_TYPE_X86 | CPU_ARCH_ABI64)
#define CPU_TYPE_ARM            (12)
#define CPU_TYPE_ARM64          (CPU_TYPE_ARM | CPU_ARCH_ABI64)
```

**Description:** Mach-O CPU type/subtype identifiers for FAT binary parsing and header generation.

**PPC32 Requirements:** Add after line 58:
```c
#define CPU_TYPE_POWERPC        (18)
#define CPU_SUBTYPE_POWERPC_ALL (0)
#define CPU_SUBTYPE_POWERPC_750 (9)  /* G3 variant */
```

**Difficulty:** Trivial

---

## 3. MAGIC NUMBER (Lines 94-97)

**Location:** Lines 94-97

```c
#define MH_MAGIC        0xfeedface      /* the mach magic number */
#define MH_CIGAM        0xcefaedfe      /* NXSwapInt(MH_MAGIC) */
#define MH_MAGIC_64     0xfeedfacf      /* the 64-bit mach magic number */
#define MH_CIGAM_64     0xcffaedfe      /* NXSwapInt(MH_MAGIC_64) */
```

**Description:** 32-bit and 64-bit Mach-O magic numbers already defined. Key point: **PPC32 uses `MH_MAGIC = 0xfeedface` (32-bit), NOT `MH_MAGIC_64`.**

**PPC32 Requirement:** These constants are already correct. The code will use `MH_MAGIC` (not `MH_MAGIC_64`) when targeting PPC, which is correct per the Mach-O spec.

**Difficulty:** Trivial (already in place)

---

## 4. MACH-O HEADER STRUCTURES (Lines 78-91)

**Location:** Lines 78-91

```c
struct mach_header {
    uint32_t        magic;          /* mach magic number identifier */
    int             cputype;        /* cpu specifier */
    int             cpusubtype;     /* machine specifier */
    uint32_t        filetype;       /* type of file */
    uint32_t        ncmds;          /* number of load commands */
    uint32_t        sizeofcmds;     /* the size of all the load commands */
    uint32_t        flags;          /* flags */
};

struct mach_header_64 {
    struct mach_header  mh;
    uint32_t            reserved;       /* reserved, pad to 64bit */
};
```

**Description:** The 32-bit `struct mach_header` is the correct format for PPC. The code uses `mach_header_64` throughout for 64-bit archs, so we need conditional logic.

**PPC32 Requirement:**  
For 64-bit targets (x86_64, arm64), code uses `mach_header_64` (valid).  
For 32-bit target (PPC), code must use `mach_header` (32-bit version).

**Decision Point (Critical):** This is a **structural difference**. The file must either:
- Use `#ifdef TCC_TARGET_PPC` blocks to swap struct types between `mach_header_64` and `mach_header`, OR
- Define per-arch `typedef` aliases at the top of the file to unify the naming.

Recommendation: **Per-arch typedef aliases** (cleaner, fewer #ifdefs).

**Difficulty:** Medium (impacts many locations)

---

## 5. SEGMENT AND SECTION STRUCTURES (Lines 124-151)

**Location:** Lines 124-151

```c
struct segment_command_64 { /* for 64-bit architectures */
    uint32_t        cmd;            /* LC_SEGMENT_64 */
    uint32_t        cmdsize;        /* includes sizeof section_64 structs */
    char            segname[16];    /* segment name */
    uint64_t        vmaddr;         /* memory address of this segment */
    uint64_t        vmsize;         /* memory size of this segment */
    uint64_t        fileoff;        /* file offset of this segment */
    uint64_t        filesize;       /* amount to map from the file */
    vm_prot_t       maxprot;        /* maximum VM protection */
    vm_prot_t       initprot;       /* initial VM protection */
    uint32_t        nsects;         /* number of sections in segment */
    uint32_t        flags;          /* flags */
};

struct section_64 { /* for 64-bit architectures */
    char            sectname[16];   /* name of this section */
    char            segname[16];    /* segment this section goes in */
    uint64_t        addr;           /* memory address of this section */
    uint64_t        size;           /* size in bytes of this section */
    uint32_t        offset;         /* file offset of this section */
    uint32_t        align;          /* section alignment (power of 2) */
    uint32_t        reloff;         /* file offset of relocation entries */
    uint32_t        nreloc;         /* number of relocation entries */
    uint32_t        flags;          /* flags (section type and attributes)*/
    uint32_t        reserved1;      /* reserved (for offset or index) */
    uint32_t        reserved2;      /* reserved (for count or sizeof) */
    uint32_t        reserved3;      /* reserved */
};
```

**Description:** 64-bit segment and section structures. PPC32 needs 32-bit variants where `uint64_t` becomes `uint32_t`.

**PPC32 Requirement:** Add 32-bit struct definitions:

```c
struct segment_command { /* for 32-bit architectures */
    uint32_t        cmd;            /* LC_SEGMENT */
    uint32_t        cmdsize;        /* includes sizeof section structs */
    char            segname[16];    /* segment name */
    uint32_t        vmaddr;         /* memory address of this segment */
    uint32_t        vmsize;         /* memory size of this segment */
    uint32_t        fileoff;        /* file offset of this segment */
    uint32_t        filesize;       /* amount to map from the file */
    vm_prot_t       maxprot;        /* maximum VM protection */
    vm_prot_t       initprot;       /* initial VM protection */
    uint32_t        nsects;         /* number of sections in segment */
    uint32_t        flags;          /* flags */
};

struct section { /* for 32-bit architectures */
    char            sectname[16];   /* name of this section */
    char            segname[16];    /* segment this section goes in */
    uint32_t        addr;           /* memory address of this section */
    uint32_t        size;           /* size in bytes of this section */
    uint32_t        offset;         /* file offset of this section */
    uint32_t        align;          /* section alignment (power of 2) */
    uint32_t        reloff;         /* file offset of relocation entries */
    uint32_t        nreloc;         /* number of relocation entries */
    uint32_t        flags;          /* flags */
    uint32_t        reserved1;      /* reserved (for offset or index) */
    uint32_t        reserved2;      /* reserved (for count or sizeof) */
};
```

Also define load command constant:
```c
#define LC_SEGMENT      0x1   /* for 32-bit */
```

**Difficulty:** Small (struct definitions, but used pervasively)

---

## 6. SYMBOL TABLE STRUCTURES (Lines 407-413)

**Location:** Lines 407-413

```c
struct nlist_64 {
    uint32_t  n_strx;      /* index into the string table */
    uint8_t n_type;        /* type flag, see below */
    uint8_t n_sect;        /* section number or NO_SECT */
    uint16_t n_desc;       /* see <mach-o/stab.h> */
    uint64_t n_value;      /* value of this symbol (or stab offset) */
};
```

**Description:** 64-bit symbol table entry. Size: 16 bytes. PPC32 needs 32-bit variant.

**PPC32 Requirement:** Add 32-bit struct:

```c
struct nlist {
    uint32_t  n_strx;      /* index into the string table */
    uint8_t   n_type;      /* type flag, see below */
    uint8_t   n_sect;      /* section number or NO_SECT */
    int16_t   n_desc;      /* see <mach-o/stab.h> */
    uint32_t  n_value;     /* value of this symbol (or stab offset) */
};
```

Note: `n_desc` is `int16_t` in 32-bit, `uint16_t` in 64-bit (minor).

**Difficulty:** Small

---

## 7. ENTRY POINT COMMAND (Lines 374-378)

**Location:** Lines 374-378

```c
struct entry_point_command {
    uint32_t  cmd;      /* LC_MAIN only used in MH_EXECUTE filetypes */
    uint32_t  cmdsize;  /* 24 */
    uint64_t  entryoff; /* file (__TEXT) offset of main() */
    uint64_t  stacksize;  /* initial stack size if stacksize != 0 */
};
```

**Description:** Used for modern executables. **LC_MAIN is 10.7+, not available on 10.4 Tiger.**

**PPC32 Requirement:**  
- **Do NOT use LC_MAIN for PPC32.**
- Use `LC_UNIXTHREAD` instead (0x5, available on all versions).
- Define alternative struct:

```c
#define LC_UNIXTHREAD   0x5

/* For PPC, thread state is 40 uint32_t values */
struct ppc_thread_state {
    uint32_t srr0;      /* Instruction address register (PC) */
    uint32_t srr1;      /* Machine state register */
    uint32_t gpr[32];   /* General purpose registers */
    uint32_t cr;        /* Condition register */
    uint32_t xer;       /* XER */
    uint32_t lr;        /* Link register */
    uint32_t ctr;       /* Count register */
    uint32_t mq;        /* MQ register (optional) */
};

struct thread_command {
    uint32_t cmd;       /* LC_UNIXTHREAD */
    uint32_t cmdsize;   /* includes thread_state */
    uint32_t flavor;    /* PPC_THREAD_STATE = 1 */
    uint32_t count;     /* number of uint32_t's in thread state (40 for PPC) */
    /* followed by ppc_thread_state */
};
```

For PPC: Set PC (gpr[0] analog actually is in srr0) to entry point address.

**Locations Requiring Change:**
- Line 1766: `mo->ep = add_lc(mo, LC_MAIN, ...)`  → Conditional on target arch
- Line 2210: `mo.ep->entryoff = get_sym_addr(...)` → Conditional / redefine for PPC

**Difficulty:** Medium (architecture-specific, but straightforward)

---

## 8. ARCHITECTURE-CONDITIONAL CODE BLOCKS (Relocation Generation)

### 8.1 __GLOBAL_init Function (Lines 575-645)

**Location:** Lines 575-645

```c
#ifdef TCC_TARGET_X86_64
    ptr = section_ptr_add(text_section, 4);
    ptr[0] = 0x55;  // pushq   %rbp
    /* ... x86_64 code ... */
#elif defined TCC_TARGET_ARM64
    ptr = section_ptr_add(text_section, 8);
    write32le(ptr, 0xa9bf7bfd);     // stp x29, x30, [sp, #-16]!
    /* ... arm64 code ... */
#endif
```

**Description:** Generates initialization code that calls `__cxa_atexit`. Each architecture needs its own instruction sequence.

**PPC32 Requirement:** Add PPC branch:

```c
#elif defined TCC_TARGET_PPC
    /* PPC calling convention: pass args in r3, r4, r5, r6
       Stack grows downward; return address saved in LR
    */
    ptr = section_ptr_add(text_section, 4);
    write32le(ptr, 0x9421ffd0);     // stwu r1, -48(r1)   save LR, local frame
    write32le(ptr + 4, 0x7c0802a6); // mflr r0
    write32le(ptr + 8, 0x90010030); // stw r0, 48(r1)     save return address
    
    for_each_elem(s->reloc, 0, rel, ElfW_Rel) {
        int sym_index = ELFW(R_SYM)(rel->r_info);
        
        /* Load destructor address into r3 */
        ptr = section_ptr_add(text_section, 8);
        write32le(ptr, 0x3c600000);     // lis r3, destructorHi@ha
        put_elf_reloc(s1->symtab, text_section,
                      text_section->data_offset - 8,
                      R_PPC_HA16, sym_index);
        write32le(ptr + 4, 0x60630000); // ori r3, r3, destructorLo@l
        put_elf_reloc(s1->symtab, text_section,
                      text_section->data_offset - 4,
                      R_PPC_LO16, sym_index);
        
        /* r4 = NULL (reserved = NULL, second param) */
        ptr = section_ptr_add(text_section, 4);
        write32le(ptr, 0x38800000);     // li r4, 0
        
        /* r5 = &mh_execute_header (third param) */
        ptr = section_ptr_add(text_section, 8);
        write32le(ptr, 0x3ca00000);     // lis r5, mh_execute_headerHi@ha
        put_elf_reloc(s1->symtab, text_section,
                      text_section->data_offset - 8,
                      R_PPC_HA16, mh_execute_header);
        write32le(ptr + 4, 0x60a50000); // ori r5, r5, mh_execute_headerLo@l
        put_elf_reloc(s1->symtab, text_section,
                      text_section->data_offset - 4,
                      R_PPC_LO16, mh_execute_header);
        
        /* bl __cxa_atexit (call with link) */
        ptr = section_ptr_add(text_section, 4);
        write32le(ptr, 0x48000001);     // bl __cxa_atexit (offset from current PC)
        put_elf_reloc(s1->symtab, text_section,
                      text_section->data_offset - 4,
                      R_PPC_REL24, at_exit_sym);
    }
    
    /* Epilogue */
    ptr = section_ptr_add(text_section, 8);
    write32le(ptr, 0x80010030);     // lwz r0, 48(r1)     restore return address
    write32le(ptr + 4, 0x7c0803a6); // mtlr r0
    write32le(ptr + 8, 0x38210030); // addi r1, r1, 48    return
    write32le(ptr + 12, 0x4e800020); // blr
#endif
```

**Relocation types for PPC:** `R_PPC_HA16`, `R_PPC_LO16`, `R_PPC_REL24` (see section 10 below).

**Difficulty:** Large (instruction sequences, relocation handling)

---

### 8.2 PLT/Stub Generation (Lines 711-760 and 889-944)

**Location:** Lines 711-760

```c
#ifdef TCC_TARGET_X86_64
    if (type != R_X86_64_PLT32)
         continue;
    jmp = section_ptr_add(mo->stubs, 6);
    jmp[0] = 0xff;  /* jmpq *ofs(%rip) */
    jmp[1] = 0x25;
    put_elf_reloc(..., R_X86_64_GOTPCREL, ...);
#elif defined TCC_TARGET_ARM64
    if (type != R_AARCH64_CALL26)
         continue;
    jmp = section_ptr_add(mo->stubs, 12);
    write32le(jmp, 0x90000010);      // adrp x16, #sym
    put_elf_reloc(..., R_AARCH64_ADR_GOT_PAGE, ...);
    write32le(jmp + 4, 0xf9400210);  // ldr x16,[x16, #sym]
    write32le(jmp + 8, 0xd61f0200);  // br x16
#endif
```

**Description:** Generates procedure linkage table (PLT) stubs for calling external functions. Used twice: lines 735-760 and 916-932.

**PPC32 Requirement:** Add PPC stub:

```c
#elif defined TCC_TARGET_PPC
    if (type != R_PPC_REL24)
         continue;
    
    jmp = section_ptr_add(mo->stubs, 12);
    /* PPC uses absolute indirect jumps via GOT:
       lis r11, sym@ha
       lwz r11, sym@l(r11)
       mtctr r11
       bctr
    */
    write32le(jmp, 0x3d600000);      // lis r11, sym@ha
    put_elf_reloc(s1->symtab, mo->stubs,
                  attr->plt_offset,
                  R_PPC_HA16, sym_index);
    
    write32le(jmp + 4, 0x816b0000);  // lwz r11, sym@l(r11)
    put_elf_reloc(s1->symtab, mo->stubs,
                  attr->plt_offset + 4,
                  R_PPC_LO16, sym_index);
    
    write32le(jmp + 8, 0x7d6903a6);  // mtctr r11
    write32le(jmp + 12, 0x4e800420); // bctr (branch to count register)
```

Note: stubs for PPC are 16 bytes (4 instructions), not 6 or 12.

**Locations:** Lines 735-760, 916-932, and also line 1830-1832 where `sec->reserved2` is set (need PPC variant: 16 instead of 6 or 12).

**Difficulty:** Large

---

### 8.3 Stub Helper (Lines 800-830)

**Location:** Lines 800-830

```c
#ifdef TCC_TARGET_X86_64
    jmp = section_ptr_add(mo->stub_helper, 16);
    jmp[0] = 0x4c;  /* leaq _dyld_private(%rip), %r11 */
    /* ... */
#elif defined TCC_TARGET_ARM64
    jmp = section_ptr_add(mo->stub_helper, 24);
    put_elf_reloc(s1->symtab, mo->stub_helper, 0,
          R_AARCH64_ADR_PREL_PG_HI21, mo->dyld_private);
    /* ... */
#endif
```

**Description:** Generates the common stub helper entry point that all PLT stubs jump to, which then calls `dyld_stub_binder` to resolve the binding at runtime.

**PPC32 Requirement:** Add PPC version:

```c
#elif defined TCC_TARGET_PPC
    jmp = section_ptr_add(mo->stub_helper, 24);
    /* Get _dyld_private into r11 */
    write32le(jmp, 0x3d600000);      // lis r11, _dyld_private@ha
    put_elf_reloc(s1->symtab, mo->stub_helper, 0,
                  R_PPC_HA16, mo->dyld_private);
    
    write32le(jmp + 4, 0x816b0000);  // lwz r11, _dyld_private@l(r11)
    put_elf_reloc(s1->symtab, mo->stub_helper, 4,
                  R_PPC_LO16, mo->dyld_private);
    
    /* Save r11 and r12 on stack, prepare for dyld_stub_binder call */
    write32le(jmp + 8, 0x9421ffd0);   // stwu r1, -48(r1)
    write32le(jmp + 12, 0x7d8b61a8); // stw r11, 44(r1)
    
    /* Get dyld_stub_binder address */
    write32le(jmp + 16, 0x3d600000);  // lis r11, dyld_stub_binder@ha
    put_elf_reloc(s1->symtab, mo->stub_helper, 16,
                  R_PPC_HA16, mo->dyld_stub_binder);
    
    write32le(jmp + 20, 0x816b0000);  // lwz r11, dyld_stub_binder@l(r11)
    put_elf_reloc(s1->symtab, mo->stub_helper, 20,
                  R_PPC_LO16, mo->dyld_stub_binder);
    
    write32le(jmp + 24, 0x7d6903a6);  // mtctr r11
    write32le(jmp + 28, 0x4e800420);  // bctr
```

Size: 32 bytes instead of 16 or 24.

**Difficulty:** Large

---

### 8.4 Lazy Binding Stub (Lines 889-944)

**Location:** Lines 889-944 (second code block, in `#else` branch)

```c
#ifdef TCC_TARGET_X86_64
    if (type != R_X86_64_PLT32)
         continue;
    /* ... 6-byte stub ... */
#elif defined TCC_TARGET_ARM64
    if (type != R_AARCH64_CALL26)
         continue;
    /* ... 12-byte stub ... */
#endif
```

**Description:** Another location for lazy-binding stub generation.

**PPC32 Requirement:** Same as section 8.2 above (16-byte stubs for PPC).

**Difficulty:** Large (duplicate logic)

---

## 9. MACH-O HEADER INITIALIZATION (Lines 1964-1971)

**Location:** Lines 1964-1971

```c
mo->mh.mh.magic = MH_MAGIC_64;
#ifdef TCC_TARGET_X86_64
    mo->mh.mh.cputype = CPU_TYPE_X86_64;
    mo->mh.mh.cpusubtype = CPU_SUBTYPE_LIB64 | CPU_SUBTYPE_X86_ALL;
#elif defined TCC_TARGET_ARM64
    mo->mh.mh.cputype = CPU_TYPE_ARM64;
    mo->mh.mh.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
#endif
```

**Description:** Sets CPU type and subtype in Mach-O header.

**PPC32 Requirement:**
```c
#ifdef TCC_TARGET_X86_64
    mo->mh.mh.magic = MH_MAGIC_64;
    mo->mh.mh.cputype = CPU_TYPE_X86_64;
    mo->mh.mh.cpusubtype = CPU_SUBTYPE_LIB64 | CPU_SUBTYPE_X86_ALL;
#elif defined TCC_TARGET_ARM64
    mo->mh.mh.magic = MH_MAGIC_64;
    mo->mh.mh.cputype = CPU_TYPE_ARM64;
    mo->mh.mh.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
#elif defined TCC_TARGET_PPC
    mo->mh.mh.magic = MH_MAGIC;     /* 32-bit magic */
    mo->mh.mh.cputype = CPU_TYPE_POWERPC;
    mo->mh.mh.cpusubtype = CPU_SUBTYPE_POWERPC_ALL;
#endif
```

**Difficulty:** Small

---

## 10. LOAD COMMAND ARCHITECTURE HANDLING (Lines 1750-1768)

**Location:** Lines 1750-1768

```c
dyldlc = add_lc(mo, LC_LOAD_DYLINKER, i);
dyldlc->name = sizeof(*dyldlc);
str = (char*)dyldlc + dyldlc->name;
strcpy(str, "/usr/lib/dyld");

dyldbv = add_lc(mo, LC_BUILD_VERSION, sizeof(*dyldbv));
dyldbv->platform = PLATFORM_MACOS;
dyldbv->minos = (10 << 16) + (6 << 8);   /* 10.6 */
dyldbv->sdk = (10 << 16) + (6 << 8);
dyldbv->ntools = 0;

dyldsv = add_lc(mo, LC_SOURCE_VERSION, sizeof(*dyldsv));
dyldsv->version = 0;

if (s1->output_type == TCC_OUTPUT_EXE) {
    mo->ep = add_lc(mo, LC_MAIN, sizeof(*mo->ep));
    mo->ep->entryoff = 4096;
}
```

**Description:** Adds several load commands. **Problems for Tiger 10.4:**
1. `LC_BUILD_VERSION` is 10.6+ only
2. `LC_MAIN` is 10.7+ only
3. Version should be 10.4, not 10.6

**PPC32 Requirement:**

```c
#ifdef TCC_TARGET_PPC
    /* For Tiger 10.4, omit LC_BUILD_VERSION (10.6+) and use LC_UNIXTHREAD instead of LC_MAIN (10.7+) */
    dyldlc = add_lc(mo, LC_LOAD_DYLINKER, i);
    dyldlc->name = sizeof(*dyldlc);
    str = (char*)dyldlc + dyldlc->name;
    strcpy(str, "/usr/lib/dyld");
    
    /* Skip LC_BUILD_VERSION for PPC (not available in 10.4) */
    
    if (s1->output_type == TCC_OUTPUT_EXE) {
        struct thread_command *tc = add_lc(mo, LC_UNIXTHREAD, 
                                          sizeof(*tc) + sizeof(struct ppc_thread_state));
        tc->flavor = 1;  /* PPC_THREAD_STATE */
        tc->count = 40;  /* 40 uint32_t's in ppc_thread_state */
        
        struct ppc_thread_state *state = (struct ppc_thread_state *) (tc + 1);
        memset(state, 0, sizeof(*state));
        state->srr0 = 0;  /* Will be filled in later with main address */
    }
#else
    dyldlc = add_lc(mo, LC_LOAD_DYLINKER, i);
    dyldlc->name = sizeof(*dyldlc);
    str = (char*)dyldlc + dyldlc->name;
    strcpy(str, "/usr/lib/dyld");

    dyldbv = add_lc(mo, LC_BUILD_VERSION, sizeof(*dyldbv));
    dyldbv->platform = PLATFORM_MACOS;
    dyldbv->minos = (10 << 16) + (6 << 8);
    dyldbv->sdk = (10 << 16) + (6 << 8);
    dyldbv->ntools = 0;

    dyldsv = add_lc(mo, LC_SOURCE_VERSION, sizeof(*dyldsv));
    dyldsv->version = 0;

    if (s1->output_type == TCC_OUTPUT_EXE) {
        mo->ep = add_lc(mo, LC_MAIN, sizeof(*mo->ep));
        mo->ep->entryoff = 4096;
    }
#endif
```

Also need to update line 2210 where entry point is set:

```c
#ifdef TCC_TARGET_PPC
    struct thread_command *tc = (struct thread_command *) mo->ep;
    struct ppc_thread_state *state = (struct ppc_thread_state *) (tc + 1);
    state->srr0 = get_sym_addr(s1, "main", 1, 1);
#else
    mo.ep->entryoff = get_sym_addr(s1, "main", 1, 1);
#endif
```

**Difficulty:** Medium

---

## 11. FAT BINARY PARSING (Lines 2376-2382)

**Location:** Lines 2376-2382

```c
for (i = 0; i < SWAP(fh.nfat_arch); i++)
#ifdef TCC_TARGET_X86_64
    if (SWAP(fa[i].cputype) == CPU_TYPE_X86_64
        && SWAP(fa[i].cpusubtype) == CPU_SUBTYPE_X86_ALL)
#elif defined TCC_TARGET_ARM64
    if (SWAP(fa[i].cputype) == CPU_TYPE_ARM64
        && SWAP(fa[i].cpusubtype) == CPU_SUBTYPE_ARM64_ALL)
#endif
        break;
```

**Description:** When loading DLLs that are FAT binaries (containing multiple architectures), this code finds the right slice for the current target.

**PPC32 Requirement:**
```c
for (i = 0; i < SWAP(fh.nfat_arch); i++)
#ifdef TCC_TARGET_X86_64
    if (SWAP(fa[i].cputype) == CPU_TYPE_X86_64
        && SWAP(fa[i].cpusubtype) == CPU_SUBTYPE_X86_ALL)
#elif defined TCC_TARGET_ARM64
    if (SWAP(fa[i].cputype) == CPU_TYPE_ARM64
        && SWAP(fa[i].cpusubtype) == CPU_SUBTYPE_ARM64_ALL)
#elif defined TCC_TARGET_PPC
    if (SWAP(fa[i].cputype) == CPU_TYPE_POWERPC
        && SWAP(fa[i].cpusubtype) == CPU_SUBTYPE_POWERPC_ALL)
#endif
        break;
```

**Difficulty:** Trivial

---

## 12. MACH-O MAGIC CHECK (Line 2399)

**Location:** Line 2399

```c
if (mh.magic != MH_MAGIC_64)
    return -1;
```

**Description:** Verifies loaded Mach-O is 64-bit format. For PPC, we need to accept 32-bit magic.

**PPC32 Requirement:**
```c
#ifdef TCC_TARGET_PPC
    if (mh.magic != MH_MAGIC)
        return -1;
#else
    if (mh.magic != MH_MAGIC_64)
        return -1;
#endif
```

**Difficulty:** Trivial

---

## 13. MACH-O HEADER SIZE IN DLL LOADING (Line 2402)

**Location:** Line 2402

```c
buf2 = load_data(fd, machofs + sizeof(struct mach_header_64), mh.sizeofcmds);
```

**Description:** Reads load commands from a loaded DLL. Header size differs between 32-bit and 64-bit.

**PPC32 Requirement:**
```c
#ifdef TCC_TARGET_PPC
    buf2 = load_data(fd, machofs + sizeof(struct mach_header), mh.sizeofcmds);
#else
    buf2 = load_data(fd, machofs + sizeof(struct mach_header_64), mh.sizeofcmds);
#endif
```

**Difficulty:** Trivial

---

## 14. ENDIANNESS (write32le, SWAP)

**Location:** Throughout (lines 614-644, 751-758, 818-829, 906, 924-944, 1327, etc.)

**Description:** `write32le` writes 32-bit values in little-endian. `SWAP` macro reverses byte order for FAT header parsing. PPC is **big-endian**.

**PPC32 Requirement:**  
The code uses `write32le()` which is defined in tcc.h. We need an equivalent `write32be()` for PPC. Options:

1. Define conditional alias at top of tccmacho.c:
```c
#ifdef TCC_TARGET_PPC
    #define write32le(p, v) write32be(p, v)
    static inline void write32be(uint8_t *p, uint32_t v) {
        p[0] = (v >> 24) & 0xff;
        p[1] = (v >> 16) & 0xff;
        p[2] = (v >> 8) & 0xff;
        p[3] = v & 0xff;
    }
#endif
```

2. Or use conditional helper function everywhere (more explicit).

Recommendation: **Define `#define write32le` as `write32be` for PPC at the top of the file**, minimizing changes throughout.

**SWAP macro (line 2254):**
```c
#define SWAP(x) (swap ? macho_swap32(x) : (x))
```

This is only used in FAT header parsing, which is correct regardless of host endianness.

**Difficulty:** Small (define helpers, minimal changes throughout)

---

## 15. RELOCATION TYPE MAPPING

**Location:** Lines 581-607, 620-640, 735-760, 750-760, 800-830, 868-915, etc.

**Description:** The code uses ELF relocation types (e.g., `R_X86_64_PLT32`, `R_AARCH64_CALL26`) and must translate them to Mach-O Mach-O relocation types internally during stub generation. PPC has its own set of ELF relocation types.

**PPC32 ELF Relocations needed:**
- `R_PPC_ADDR32`: Absolute 32-bit address relocation
- `R_PPC_HA16`: High-order 16-bits of symbol address (adjusted for sign extension)
- `R_PPC_LO16`: Low-order 16 bits of symbol address
- `R_PPC_REL24`: PC-relative 24-bit branch offset
- `R_PPC_ADDR24`: Absolute 24-bit relocation (for branches)
- `R_PPC_GLOB_DAT`: Global data pointer (GOT)
- `R_PPC_JMP_SLOT`: Procedure linkage table entry

These are typically defined in `tcc/elf.c` or similar, not in tccmacho.c. **Survey scope: tccmacho.c itself doesn't define these, but assumes they exist.** The relocation handling in tccmacho.c is:
- Lines 581-607: Creates relocations using `put_elf_reloca()` and ELF reloc types
- Lines 735-760: Checks relocation type and creates stubs accordingly

**For tccmacho.c changes:**
No direct changes needed to relocation mappings (those are internal ELF format), but the conditional code blocks (8.1-8.4) must emit correct ELF relocation type markers so the later conversion functions (if any) know what to do.

**Difficulty:** Medium (relocation types must be defined elsewhere, but conditional code in tccmacho.c must emit them correctly)

---

## 16. STRUCT MACH_HEADER_64 USAGE IN MACHO STRUCT (Line 424)

**Location:** Line 424

```c
struct macho {
    struct mach_header_64 mh;
    /* ... */
};
```

**Description:** The `macho` struct embeds a 64-bit header.

**PPC32 Requirement:**  
Must become conditional. Either:

Option A: Conditional struct members:
```c
struct macho {
    #ifdef TCC_TARGET_PPC
        struct mach_header mh;
    #else
        struct mach_header_64 mh;
    #endif
    /* ... */
};
```

Option B: Use a union (not recommended, confusing).

Option C: **Recommended**: Define per-arch typedef at the top:
```c
#ifdef TCC_TARGET_PPC
    typedef struct mach_header Mach_Header;
    typedef struct segment_command Segment_Command;
    typedef struct section Section_Mach;
#else
    typedef struct mach_header_64 Mach_Header;
    typedef struct segment_command_64 Segment_Command;
    typedef struct section_64 Section_Mach;
#endif

struct macho {
    Mach_Header mh;
    /* ... */
};
```

Then use `Segment_Command` and `Section_Mach` throughout (less disruptive than changing many `segment_command_64` to conditional).

**Difficulty:** Medium (but central to the refactoring strategy)

---

## 17. NLIST SYMBOL TABLE SIZE (Line 1913)

**Location:** Line 1913

```c
symlc->nsyms = mo->symtab->data_offset / sizeof(struct nlist_64);
```

**Description:** Calculates symbol table entry count. Must use correct struct size.

**PPC32 Requirement:**
```c
#ifdef TCC_TARGET_PPC
    symlc->nsyms = mo->symtab->data_offset / sizeof(struct nlist);
#else
    symlc->nsyms = mo->symtab->data_offset / sizeof(struct nlist_64);
#endif
```

**Difficulty:** Trivial

---

## 18. DLL LOADING: SYMBOL TABLE SIZE (Line 2410, 2462)

**Location:** Lines 2410, 2462

```c
symtab = load_data(fd, machofs + sc->symoff, nsyms * sizeof(*symtab));
/* ... */
struct nlist_64 *sym = symtab + i;
```

**Description:** Loads symbol tables from linked DLLs. Must use correct struct size.

**PPC32 Requirement:**
```c
#ifdef TCC_TARGET_PPC
    symtab = load_data(fd, machofs + sc->symoff, nsyms * sizeof(struct nlist));
    /* ... */
    struct nlist *sym = symtab + i;
#else
    symtab = load_data(fd, machofs + sc->symoff, nsyms * sizeof(struct nlist_64));
    /* ... */
    struct nlist_64 *sym = symtab + i;
#endif
```

**Difficulty:** Trivial

---

## 19. ADD_SEGMENT AND GET_SEGMENT (Lines 486-498)

**Location:** Lines 486-498

```c
static struct segment_command_64 * add_segment(struct macho *mo, const char *name)
{
    struct segment_command_64 *sc = add_lc(mo, LC_SEGMENT_64, sizeof(*sc));
    /* ... */
}

static struct segment_command_64 * get_segment(struct macho *mo, int i)
{
    return (struct segment_command_64 *) (mo->lc[mo->seg2lc[i]]);
}
```

**Description:** Helper functions for segment management.

**PPC32 Requirement:**  
Conditional logic:

```c
static inline void * add_segment(struct macho *mo, const char *name)
{
    #ifdef TCC_TARGET_PPC
        struct segment_command *sc = add_lc(mo, LC_SEGMENT, sizeof(*sc));
        strncpy(sc->segname, name, 16);
        mo->seg2lc = tcc_realloc(mo->seg2lc, sizeof(*mo->seg2lc) * (mo->nseg + 1));
        mo->seg2lc[mo->nseg++] = mo->nlc - 1;
        return sc;
    #else
        struct segment_command_64 *sc = add_lc(mo, LC_SEGMENT_64, sizeof(*sc));
        strncpy(sc->segname, name, 16);
        mo->seg2lc = tcc_realloc(mo->seg2lc, sizeof(*mo->seg2lc) * (mo->nseg + 1));
        mo->seg2lc[mo->nseg++] = mo->nlc - 1;
        return sc;
    #endif
}

static inline void * get_segment(struct macho *mo, int i)
{
    return (void *) (mo->lc[mo->seg2lc[i]]);
}
```

Or use the typedef approach to avoid repeating logic.

**Difficulty:** Small

---

## 20. ADD_SECTION (Lines 500-518)

**Location:** Lines 500-518

```c
static int add_section(struct macho *mo, struct segment_command_64 **_seg, const char *name)
{
    struct segment_command_64 *seg = *_seg;
    int ret = seg->nsects;
    struct section_64 *sec;

    sec = (struct section_64*)((char*)seg + sizeof(*seg)) + ret;
    /* ... */
}

static struct section_64 *get_section(struct segment_command_64 *seg, int i)
{
    return (struct section_64*)((char*)seg + sizeof(*seg)) + i;
}
```

**Description:** Add and retrieve sections within segments.

**PPC32 Requirement:**  
Conditional per struct size:

```c
static int add_section(struct macho *mo, void *_seg, const char *name)
{
    #ifdef TCC_TARGET_PPC
        struct segment_command *seg = (struct segment_command *) _seg;
        int ret = seg->nsects;
        struct section *sec;
        
        sec = (struct section *)((char *)seg + sizeof(*seg)) + ret;
        strncpy(sec->sectname, name, 16);
        strncpy(sec->segname, seg->segname, 16);
        return ret;
    #else
        struct segment_command_64 *seg = (struct segment_command_64 *) _seg;
        int ret = seg->nsects;
        struct section_64 *sec;
        
        sec = (struct section_64 *)((char *)seg + sizeof(*seg)) + ret;
        strncpy(sec->sectname, name, 16);
        strncpy(sec->segname, seg->segname, 16);
        return ret;
    #endif
}
```

**Difficulty:** Small

---

## 21. RESERVED2 FOR STUB SIZE (Lines 1828-1833)

**Location:** Lines 1828-1833

```c
if (sk == sk_stubs)
#ifdef TCC_TARGET_X86_64
    sec->reserved2 = 6;
#elif defined TCC_TARGET_ARM64
    sec->reserved2 = 12;
#endif
```

**Description:** Mach-O section header field `reserved2` encodes stub size for `S_SYMBOL_STUBS` sections.

**PPC32 Requirement:**
```c
if (sk == sk_stubs)
#ifdef TCC_TARGET_X86_64
    sec->reserved2 = 6;   /* x86_64: 6 bytes per stub */
#elif defined TCC_TARGET_ARM64
    sec->reserved2 = 12;  /* arm64: 12 bytes per stub */
#elif defined TCC_TARGET_PPC
    sec->reserved2 = 16;  /* PPC: 16 bytes per stub (4 instructions) */
#endif
```

**Difficulty:** Trivial

---

## 22. BUFFER SIZE IN MACHO_LOAD_DLL (Line 2352)

**Location:** Line 2352

```c
unsigned char buf[sizeof(struct mach_header_64)];
```

**Description:** Stack buffer for reading Mach-O header when loading DLLs.

**PPC32 Requirement:**  
Must accommodate largest header (64-bit is larger):

```c
unsigned char buf[sizeof(struct mach_header_64)];  /* sufficient for both 32 and 64-bit */
```

Actually, `mach_header_64` is still the larger one, so **no change needed** (32-bit is smaller). Can leave as-is.

**Difficulty:** Trivial (no change)

---

## SUMMARY TABLE: All Changes at a Glance

| Line(s) | Category | Type | Difficulty | Change |
|---------|----------|------|------------|--------|
| 34–36 | Platform check | #ifdef | Trivial | Add `TCC_TARGET_PPC` to guard |
| 49–58 | CPU constants | #define | Trivial | Add PPC CPU_TYPE/SUBTYPE defs |
| 78–91 | Struct defs | struct | Small | Add `mach_header` (32-bit) variant |
| 110 | Load cmd constant | #define | Trivial | Add `LC_SEGMENT 0x1` |
| 124–151 | Struct defs | struct | Small | Add `segment_command`, `section` (32-bit) |
| 374–378 | Entry point | struct | Medium | Add `thread_command`, `ppc_thread_state` |
| 407–413 | Struct defs | struct | Small | Add `nlist` (32-bit) |
| 575–645 | __GLOBAL_init | #ifdef block | Large | Add PPC instruction sequence |
| 735–760 | PLT stubs | #ifdef block | Large | Add PPC stub code |
| 800–830 | Stub helper | #ifdef block | Large | Add PPC stub helper |
| 889–944 | Lazy stubs | #ifdef block | Large | Add PPC stub code (duplicate) |
| 1050–1100 | convert_symbol | Inspection | Trivial | Verify `nlist_64` → `nlist` size handling |
| 1160–1220 | create_symtab | Inspection | Trivial | Check symbol table ops |
| 1304, 1620–1621 | Segment var types | func param | Medium | Use void* or typedef for segment pointers |
| 1750–1768 | Load commands | #ifdef block | Medium | Omit LC_BUILD_VERSION, use LC_UNIXTHREAD for PPC |
| 1799, 1826 | Section types | var decl | Medium | Use void* or typedef for section pointers |
| 1828–1833 | Stub size | #ifdef | Trivial | Add PPC case: 16 bytes |
| 1913 | Symbol count | #ifdef | Trivial | Use `sizeof(struct nlist)` for PPC |
| 2024–2174 | bind_rebase_import | Inspection | Small | Verify 64-bit ptrs vs 32-bit on PPC |
| 2210 | Entry point set | #ifdef | Medium | Set srr0 in LC_UNIXTHREAD for PPC |
| 2352 | Buffer size | No change | Trivial | (mach_header_64 is sufficient) |
| 2376–2382 | FAT parsing | #ifdef | Trivial | Add PPC CPU_TYPE check |
| 2399 | Magic check | #ifdef | Trivial | Use `MH_MAGIC` for PPC |
| 2402 | Header size | #ifdef | Trivial | Use `sizeof(struct mach_header)` for PPC |
| 2410, 2462 | Symbol load | #ifdef | Trivial | Use `struct nlist` for PPC |
| Throughout | Endianness | write32le | Small | Define as write32be for PPC |
| 486–498 | add/get_segment | func | Medium | Conditional or typedef-based |
| 500–518 | add/get_section | func | Medium | Conditional or typedef-based |

---

## PORTING STRATEGY RECOMMENDATION

### Option A: Monolithic File with Per-Arch #ifdefs (RECOMMENDED)

**Keep tccmacho.c as a single file with extensive `#ifdef TCC_TARGET_X86_64 / #elif TCC_TARGET_ARM64 / #elif TCC_TARGET_PPC` blocks.**

**Advantages:**
- Shared infrastructure (add_lc, add_segment, add_section, symbol table building, FAT parsing)
- No duplication of dll loading, export trie, etc.
- Easier to maintain common code
- Single file to reason about

**Disadvantages:**
- Many #ifdefs (but unavoidable given structural differences)
- Harder to read instruction-heavy sections (8.1–8.4)

**Recommendation:** This is the right choice given the code structure. The conditional code is concentrated in specific sections (init generation, stub creation, stub helper, load commands), not scattered everywhere.

### Option B: Split Per-Architecture (NOT RECOMMENDED)

**Create tccmacho-ppc.c, tccmacho-x86_64.c, tccmacho-arm64.c.**

**Advantages:**
- Clean separation, no #ifdefs
- Clearer instruction sequences for each arch

**Disadvantages:**
- Duplicate ~70% of the code (common DLL loading, FAT parsing, symbol table, export trie, binding tables)
- Hard to maintain when fixing bugs that apply to all archs
- Larger codebase

**Not recommended** unless code divergence becomes extreme.

---

## IMPLEMENTATION PHASES

### Phase 1: Structural Foundation (1–2 sessions)

**Goal:** Define types, constants, structs; allow code to compile for PPC.

**Tasks:**
1. Add `TCC_TARGET_PPC` to platform guard (line 34)
2. Define CPU_TYPE/SUBTYPE constants (lines 49–58)
3. Define `struct mach_header`, `struct segment_command`, `struct section`, `struct nlist` (32-bit variants)
4. Define `LC_SEGMENT`, `LC_UNIXTHREAD` constants
5. Define `struct thread_command`, `struct ppc_thread_state`
6. At top of file, define per-arch typedefs:
   ```c
   #ifdef TCC_TARGET_PPC
       typedef struct mach_header Mach_Header;
       typedef struct segment_command Segment_Command;
       typedef struct section Section_Mach;
       typedef struct nlist Nlist;
       #define write32le(p, v) write32be(p, v)
       static inline void write32be(...) { ... }
   #else
       typedef struct mach_header_64 Mach_Header;
       typedef struct segment_command_64 Segment_Command;
       typedef struct section_64 Section_Mach;
       typedef struct nlist_64 Nlist;
   #endif
   ```
7. Update `struct macho` to use typedefs (line 424)
8. Update function signatures: `add_segment()`, `get_segment()`, `add_section()`, `get_section()` to use void* or typedefs

**Smoke Test:** Code should compile for PPC with `tcc -c test.c -o test.o` (but not link or run yet).

---

### Phase 2: Load Command & Entry Point Handling (1 session)

**Goal:** Allow PPC executables to initialize with LC_UNIXTHREAD instead of LC_MAIN.

**Tasks:**
1. Update mach_header initialization (line 1964) to use correct magic number for PPC
2. Set CPU_TYPE/SUBTYPE (lines 1966–1970) to PPC values
3. Conditionally add LC_UNIXTHREAD vs LC_MAIN (lines 1750–1768)
4. Omit LC_BUILD_VERSION for PPC (10.6+ feature)
5. Update entry point setting (line 2210) to set srr0 in thread state for PPC

**Smoke Test:** `tcc -c test.c -o test.o && otool -h test.o` should show PPC cputype and MH_MAGIC.

---

### Phase 3: Basic Relocation & Symbol Handling (1 session)

**Goal:** Allow simple programs (variables, no dynamic calls) to compile and link.

**Tasks:**
1. Update `convert_symbol()` and related to handle `struct nlist` vs `nlist_64`
2. Update symbol table size calculations (lines 1913, 2410)
3. Update FAT binary parsing to recognize PPC slices (line 2376)
4. Update magic number check (line 2399) and header size in DLL loading (line 2402)
5. Define PPC relocation types in tcc/elf.c (R_PPC_ADDR32, R_PPC_HA16, R_PPC_LO16, R_PPC_REL24, etc.) if not already present
6. Add stub size field (reserved2) for PPC stubs (line 1828) — 16 bytes

**Smoke Test:** Compile and link simple program without function calls:
```c
int x = 5;
int main(void) { return x; }
```

---

### Phase 4: Stub Generation & Dynamic Linking (1–2 sessions)

**Goal:** Allow calling external functions (printf, etc.) via stubs and GOT.

**Tasks:**
1. Add PPC stub generation in two places (lines 735–760 and 916–932):
   - Load symbol address via lis/ori/lwz (HA16/LO16 relocs)
   - Jump via mtctr/bctr
2. Add PPC stub helper (line 800–830):
   - Set up _dyld_private
   - Call dyld_stub_binder
3. Add PPC __GLOBAL_init generation (lines 575–645):
   - Build function prologue/epilogue
   - Load destructor address
   - Call __cxa_atexit with proper argument setup (r3, r4, r5 per ABI)

**Relocation Types to Ensure Are Defined (in tcc/elf.c):**
- R_PPC_HA16, R_PPC_LO16 (address loads)
- R_PPC_REL24 (branch offsets)
- R_PPC_GLOB_DAT, R_PPC_JMP_SLOT (for GOT/PLT)

**Smoke Test:** Compile and link program with function calls:
```c
#include <stdio.h>
int main(void) { printf("Hello, PPC\n"); return 0; }
```

---

### Phase 5: Verification & Cleanup (1 session)

**Goal:** Ensure basic programs run on actual G3 hardware (Tiger).

**Tasks:**
1. Test on imacg3: Build simple executable, run it via SSH
2. Verify otool output matches expectations (sections, load commands, stubs)
3. Test with gcc-compiled counterpart to compare Mach-O structure
4. Debug any relocation/symbol issues
5. Clean up any debug #define statements

**Smoke Test:** On imacg3:
```bash
ssh imacg3 'cd /tmp; tcc -c /tmp/hello.c -o /tmp/hello.o; ld /tmp/hello.o -o /tmp/hello; ./hello'
```

Expected output: "Hello, PPC" (or appropriate test program output).

---

## OPEN QUESTIONS & RESEARCH NEEDED

Before proceeding with implementation, the following items need clarification or validation:

### 1. PPC Stub & Stub Helper Instruction Sequences

**Question:** Are the 16-byte stub sequences (lis/ori/lwz/mtctr/bctr) correct for PPC32, or should they use other addressing modes (e.g., pic-relative)?

**Action:** Cross-check with:
- `/Users/cell/datasheets/books/prog/_powerpc/` ABI & instruction guides
- gcc-4.0 output on imacg3 (disassemble .o files with otool -tV)
- tinycc mob/ branch for any PPC references

### 2. Tiger Load Command Minimum Set

**Question:** Exactly which load commands are required vs optional on 10.4? Is LC_LOAD_DYLIB enough, or do we need additional commands?

**Action:**
- Check imacg3 Tiger system Mach-O files in /usr/lib/ with otool -lv
- Verify with Leopard ADC docs on load command availability timeline

### 3. PPC Thread State for LC_UNIXTHREAD

**Question:** Is `ppc_thread_state` with 40 uint32_t elements correct, or does it vary?

**Action:**
- Check `/usr/include/mach/ppc/thread_status.h` equivalent on Tiger
- Verify against gcc-compiled executables

### 4. __dyld_private on PPC

**Question:** What is the correct address/symbol for `_dyld_private` on PPC 10.4? Does it exist, or is there a PPC-specific equivalent?

**Action:**
- nm output from Tiger /usr/lib/dyld
- Compare with x86_64/arm64 handling in current code

### 5. FAT Binary Endianness

**Question:** Tiger FAT binaries for PPC — are they big-endian FAT headers (0xfeedbabe) or little-endian? Does imacg3 have any mixed FAT libraries?

**Action:**
- Check system libraries: file /opt/local/lib/*.dylib on imacg3
- Verify endianness handling in FAT_MAGIC constants (already correct in code)

### 6. Relocation Type Mapping: ELF → Mach-O Internal

**Question:** Does tccmacho.c ever convert ELF relocation types to Mach-O relocation types (R_X86_64_* → X86_64_RELOC_*), or are relocations kept in ELF format until final writing?

**Answer from code review:** Relocations are kept in ELF format during processing and written to a separate "BINDING" or "CHAINED_FIXUPS" section, not as Mach-O relocations. **No conversion function exists.** This means the ELF relocation types used in conditional code (sections 8.1–8.4) are just markers; the actual Mach-O binding/rebase tables don't use traditional Mach-O relocation records.

**Implication:** As long as PPC ELF relocation types are defined, the binding/rebase code should work automatically (it iterates over ELF relocations and builds abstract bind/rebase structures).

### 7. CONFIG_NEW_MACHO vs Old MACHO Mode

**Question:** The code has two modes: `CONFIG_NEW_MACHO` (uses LC_DYLD_CHAINED_FIXUPS, modern) and classic mode (uses LC_DYLD_INFO_ONLY + REBASE/BINDING sections). Which should PPC use, and is 10.4 compatible?

**Answer from code:** LC_DYLD_CHAINED_FIXUPS is 10.15+, not available on Tiger. The code has `#ifndef CONFIG_NEW_MACHO` blocks for the classic approach. **PPC must use the classic mode (REBASE/BINDING sections).** The code already has this path; need to verify Tiger compatibility of LC_DYLD_INFO_ONLY.

**Action:**
- Confirm LC_DYLD_INFO_ONLY availability on 10.4 (likely yes, it's been around since 10.6+)
- Ensure `#else` branches (classic mode) are used for PPC

---

## RISK ASSESSMENT

**Highest Risk Areas:**

1. **Instruction sequences (Sections 8.1–8.4):** PPC instruction encoding must be exact. Any error will cause invalid opcodes or relocation mismatches.
   - *Mitigation:* Cross-validate against gcc-4.0 output on imacg3 with otool -tV.

2. **Endianness (Section 14):** write32le vs write32be must be consistent. Byte-swapping errors will corrupt all binary values.
   - *Mitigation:* Early smoke test with otool -h to verify magic number and field values are correct.

3. **Thread state in LC_UNIXTHREAD (Section 7):** Incorrect thread state structure will cause kernel to reject the executable.
   - *Mitigation:* Test on imacg3 with proper error handling; compare against gcc-compiled binary.

4. **Struct size assumptions:** Pointer and field size differences between 32-bit and 64-bit can cause buffer overflows or incorrect reads.
   - *Mitigation:* Use typedefs/sizeof throughout; compile with warnings enabled (-Wpadded, etc.).

**Medium Risk:**

5. **Load command constants and availability:** Using 10.6+ features (LC_BUILD_VERSION) on 10.4 could be silently ignored or cause linking errors.
   - *Mitigation:* Test on imacg3 Tiger; check otool output; consult ADC docs.

6. **PPC ABI details:** Argument passing, stack alignment, return values must match convention.
   - *Mitigation:* Refer to Apple PPC ABI Function Call Guide; test simple programs first.

---

## DETAILED SITE MAP (For Easy Reference During Implementation)

```
tccmacho.c
├─ PART 1: Definitions & Constants (lines 1–500)
│  ├─ Includes & guards (lines 1–40)
│  │  └─ Line 34: Platform guard ← CHANGE: Add TCC_TARGET_PPC
│  │
│  ├─ Constants (lines 41–119)
│  │  ├─ Lines 49–58: CPU types ← CHANGE: Add PPC types
│  │  ├─ Lines 94–97: Magic numbers (OK as-is)
│  │  ├─ Line 110: LC_SEGMENT_64 (OK), need to add LC_SEGMENT
│  │  ├─ Lines 104–118: Load command constants ← CHANGE: Add LC_UNIXTHREAD
│  │  └─ Line 114: LC_MAIN ← CHANGE: Conditional on target
│  │
│  ├─ Struct definitions (lines 60–380)
│  │  ├─ Lines 78–91: mach_header, mach_header_64 ← ADD: 32-bit variant
│  │  ├─ Lines 124–151: segment_command_64, section_64 ← ADD: 32-bit variants
│  │  ├─ Lines 374–378: entry_point_command ← ADD: ppc_thread_state, thread_command
│  │  ├─ Lines 407–413: nlist_64 ← ADD: 32-bit nlist
│  │  └─ Lines 424: struct macho with mach_header_64 ← CHANGE: Use typedef
│  │
│  └─ Helper functions & typedefs (lines 420–500)
│     ├─ Lines 476–483: add_lc()
│     ├─ Lines 486–493: add_segment() ← CHANGE: Conditional types
│     ├─ Lines 495–498: get_segment() ← CHANGE: Return void*
│     ├─ Lines 500–518: add_section(), get_section() ← CHANGE: Conditional types
│     └─ ← PLACE HERE: Per-arch typedefs and write32be() helper
│
├─ PART 2: Relocation & Binding Logic (lines 520–1220)
│  ├─ Lines 575–645: __GLOBAL_init function ← CHANGE: Add PPC #elif block
│  ├─ Lines 666–785: check_relocs() [CONFIG_NEW_MACHO] ← Inspect for 64-bit assumptions
│  ├─ Lines 789–975: check_relocs() [classic mode] ← CHANGE: Add PPC stubs (lines 800–830, 889–944)
│  ├─ Lines 1052–1093: convert_symbol() ← CHANGE: Handle nlist vs nlist_64
│  ├─ Lines 1160–1218: create_symtab() ← CHANGE: Symbol size (line 1913)
│  └─ Lines 1273–1295 / 1315–1399: calc_fixup_size() / bind_rebase()
│
├─ PART 3: Segment & Layout (lines 1220–1910)
│  ├─ Lines 1250–1269: all_segment[] array ← Inspect: VMA start values
│  ├─ Lines 1304–1313: set_segment_and_offset()
│  ├─ Lines 1620–1700: create_macho() top half
│  ├─ Lines 1750–1768: Load command setup ← CHANGE: LC_MAIN vs LC_UNIXTHREAD, omit LC_BUILD_VERSION
│  ├─ Lines 1799–1880: Segment/section iteration ← Check sizeof() calls
│  ├─ Lines 1828–1833: Stub size field ← CHANGE: Add PPC case (16 bytes)
│  └─ Lines 1913–1950: Symbol/relocation tables fill in ← CHANGE: Line 1913 nlist size
│
├─ PART 4: Final Output & Binding (lines 1950–2220)
│  ├─ Lines 1960–1990: write_macho_header() ← CHANGE: Magic number, CPU type (line 1964–1970)
│  ├─ Lines 2013–2174: bind_rebase_import() ← Inspect: 64-bit pointer assumptions
│  ├─ Lines 2210: Entry point assignment ← CHANGE: Conditional on LC_MAIN vs LC_UNIXTHREAD
│  └─ Lines 2219–2230: macho_output()
│
└─ PART 5: DLL Loading (lines 2350–2476)
   ├─ Lines 2350–2400: macho_load_dll() setup
   ├─ Lines 2352, 2399, 2402: Buffer/magic checks ← CHANGE: Conditional on 32 vs 64-bit
   ├─ Lines 2376–2382: FAT binary parsing ← CHANGE: Add PPC CPU_TYPE check
   ├─ Lines 2406–2449: Load command parsing (FAT OK as-is)
   ├─ Line 2410: Symbol load ← CHANGE: nlist size
   └─ Lines 2462–2469: Symbol processing ← CHANGE: nlist field access
```

---

## FILES TO CREATE/MODIFY

**In tccmacho.c:**
- Add per-arch struct definitions (32-bit variants): ~50 lines
- Add per-arch typedefs and helpers: ~30 lines
- Add #ifdef blocks in 8 locations for instruction sequences: ~300 lines
- Update 15+ locations with conditional logic: ~100 lines
- Total new code: ~480 lines of new #ifdefs and definitions

**Dependencies (verify/update if needed):**
- `tcc/elf.c`: Ensure R_PPC_* relocation types are defined
- `tcc/tcc.h`: Ensure write32le() is available, or add write32be()
- Cross-compilation setup: Ensure tcc/Config-xxx files support TCC_TARGET_PPC flag

**Testing files (to create):**
- `/Users/cell/claude/tcc-darwin8-ppc/tests/test-ppc-simple.c`: Hello world
- `/Users/cell/claude/tcc-darwin8-ppc/tests/test-ppc-relocations.c`: Various relocation types
- `/Users/cell/claude/tcc-darwin8-ppc/tests/test-ppc-dylib.c`: External function calls

---

## CONCLUSION

**Surgical Change Count:** ~25 discrete locations in tccmacho.c (plus 8 instruction-heavy #ifdef blocks).

**Complexity:** Medium overall, with concentrated difficulty in:
- Sections 8.1–8.4 (PPC instruction sequences & relocation handling) — requires ABI knowledge
- Section 7 (LC_UNIXTHREAD thread state setup) — requires kernel/ABI knowledge
- Endianness handling (Section 14) — relatively straightforward with write32be helper

**Estimated Implementation Time:** 4–6 sessions (one per phase), assuming PPC ABI documentation is accessible and testing on imacg3 is available.

**Success Criteria:**
1. Phase 1: Code compiles for TCC_TARGET_PPC
2. Phase 2: otool -h shows MH_MAGIC and correct cputype
3. Phase 3: Simple variable-only programs link and run
4. Phase 4: Programs with printf() and external function calls work
5. Phase 5: Binaries match gcc-4.0 structure (loosely) and run on Tiger G3

