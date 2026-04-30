/*
 * ppc-gen.c — 32-bit PowerPC code generator for TCC.
 *
 * Targets Mac OS X 10.4 Tiger on PowerPC G3 and later. Apple PPC ABI
 * (similar to System V PPC ABI but with Apple-specific struct passing
 * and varargs rules).
 *
 * Session 003 — scaffold only. All codegen and load/store routines
 * call tcc_error() for now. Macros and register layout are real and
 * informed by the Apple PPC ABI (PowerPC ELF Application Binary
 * Interface Supplement, 1996, plus Apple's "Mac OS X ABI Function
 * Call Guide").
 *
 * Copyright (c) 2026 the tcc-darwin8-ppc contributors.
 * MIT licensed; see ../LICENSE in the project repo.
 */

#ifdef TARGET_DEFS_ONLY

/*
 * Register layout (TCC's view).
 *
 * PPC has 32 GPRs (r0-r31) and 32 FPRs (f0-f31). For Apple ABI:
 *   r0       - scratch / "0" in addi-from-zero idiom; never alloc'd
 *   r1       - stack pointer; never alloc'd
 *   r2       - reserved on Darwin (TOC on AIX, unused here); never alloc'd
 *   r3..r10  - volatile, also arg/return regs
 *   r11, r12 - volatile, scratch
 *   r13..r31 - non-volatile (callee-saved); not yet alloc'd in v1
 *   f0       - scratch
 *   f1..f13  - volatile, also arg/return regs
 *   f14..f31 - non-volatile; not yet alloc'd in v1
 *
 * For now we expose 8 int regs (r3-r10) plus 2 scratch (r11, r12),
 * and 8 float regs (f1-f8). Total = 18.
 *
 * Future work: expose non-volatile registers for spill avoidance.
 */
#define NB_REGS         18

/* Map TCC's TREG_* slot to a PPC register number. */
#define TREG_R(x)       (x)             /* x = 0..7  -> r3..r10  */
#define TREG_R11        8               /* scratch */
#define TREG_R12        9               /* scratch */
#define TREG_F(x)       ((x) + 10)      /* x = 0..7  -> f1..f8   */

/* Translate a TREG slot back to the actual PPC GPR number (3..12). */
#define TREG_TO_GPR(t)  ((t) < 8 ? (t) + 3 : (t) == 8 ? 11 : 12)
/* Translate a TREG slot back to the actual PPC FPR number (1..8). */
#define TREG_TO_FPR(t)  ((t) - 10 + 1)

/* Register-class bitmasks. Bit 0 = generic int, bit 1 = generic float;
 * bits 2..11 are individual int regs, bits 12..19 individual floats. */
#define RC_INT          (1 << 0)
#define RC_FLOAT        (1 << 1)
#define RC_R(x)         (1 << (2 + (x)))        /* x = 0..9 */
#define RC_F(x)         (1 << (12 + (x)))       /* x = 0..7 */

#define RC_R3           RC_R(0)
#define RC_R4           RC_R(1)
#define RC_F1           RC_F(0)

/* Function-call return register classes & numbers. */
#define RC_IRET         RC_R3           /* int return register class */
#define RC_IRE2         RC_R4           /* second word of long long return */
#define RC_FRET         RC_F1           /* float return register class */

#define REG_IRET        TREG_R(0)       /* r3 */
#define REG_IRE2        TREG_R(1)       /* r4, used for long long */
#define REG_FRET        TREG_F(0)       /* f1 */

#define PTR_SIZE        4

/* Apple PPC: long double = double (no 128-bit "double-double" yet).
 * Future: support double-double for full ABI compat. */
#define LDOUBLE_SIZE    8
#define LDOUBLE_ALIGN   8

/* Largest required alignment for any scalar; 8 covers double. */
#define MAX_ALIGN       8

/* Caller must sign/zero-extend small int return values. PowerPC ABI
 * has the callee return values widened to 32 bits, but we play it
 * safe and have the caller do it too (matches what gcc does on
 * Darwin). */
#define PROMOTE_RET

/******************************************************/
#else /* !TARGET_DEFS_ONLY */
/******************************************************/

#define USING_GLOBALS
#include "tcc.h"
#include <assert.h>

/* Predefined macros for `cpp -dM`-style queries. The classic PPC
 * predefined macros, plus Apple's. */
ST_DATA const char * const target_machine_defs =
    "__powerpc__\0"
    "__POWERPC__\0"
    "__ppc__\0"
    "__PPC__\0"
    "_ARCH_PPC\0"
    "__BIG_ENDIAN__\0"
#if defined(TCC_TARGET_MACHO)
    "__APPLE__\0"
    "__MACH__\0"
#endif
    ;

/* Per-register class. Index = TREG slot. */
ST_DATA const int reg_classes[NB_REGS] = {
    /* 0..7: r3..r10 */
    RC_INT | RC_R(0),
    RC_INT | RC_R(1),
    RC_INT | RC_R(2),
    RC_INT | RC_R(3),
    RC_INT | RC_R(4),
    RC_INT | RC_R(5),
    RC_INT | RC_R(6),
    RC_INT | RC_R(7),
    /* 8: r11 (scratch) */
    RC_INT | RC_R(8),
    /* 9: r12 (scratch) */
    RC_INT | RC_R(9),
    /* 10..17: f1..f8 */
    RC_FLOAT | RC_F(0),
    RC_FLOAT | RC_F(1),
    RC_FLOAT | RC_F(2),
    RC_FLOAT | RC_F(3),
    RC_FLOAT | RC_F(4),
    RC_FLOAT | RC_F(5),
    RC_FLOAT | RC_F(6),
    RC_FLOAT | RC_F(7),
};

/* Bounds-check support is deferred. tccgen.c references
 * func_bound_add_epilog unconditionally (not gated by
 * CONFIG_TCC_BCHECK), so its storage must always exist. The other
 * two are only used by future bcheck-aware codegen routines. */
ST_DATA int func_bound_add_epilog;
#if defined(CONFIG_TCC_BCHECK)
static addr_t func_bound_offset;
static unsigned long func_bound_ind;
#endif

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* PPC instructions are big-endian 32-bit words regardless of the host
 * byte order. Write directly without using write32le / write32be —
 * tcc.h doesn't expose a portable be writer. */
static void ppc_write32be(unsigned char *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8)  & 0xff;
    p[3] = v         & 0xff;
}

/* (ppc_read32be will be added when first needed by ppc-link.c.) */

/* ------------------------------------------------------------------ */
/* required ST_FUNC API — bodies are stubs in session 003             */
/* ------------------------------------------------------------------ */

/* Append a big-endian 32-bit instruction word to the current code
 * section. Real, used by every codegen routine. */
ST_FUNC void o(unsigned int c)
{
    int ind1 = ind + 4;
    if (nocode_wanted)
        return;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    ppc_write32be(cur_text_section->data + ind, c);
    ind = ind1;
}

/* Patch an unresolved jump-list. */
ST_FUNC void gsym_addr(int t, int a)
{
    tcc_error("ppc-gen: gsym_addr stub (t=0x%x a=0x%x)", t, a);
}

/* PPC nop = ori r0, r0, 0 = 0x60000000. */
ST_FUNC void gen_fill_nops(int bytes)
{
    if (bytes & 3)
        tcc_error("ppc-gen: gen_fill_nops byte-count not multiple of 4");
    while (bytes) {
        o(0x60000000);
        bytes -= 4;
    }
}

ST_FUNC void load(int r, SValue *sv)
{
    tcc_error("ppc-gen: load stub (r=%d type=0x%x)", r, sv ? sv->type.t : 0);
}

ST_FUNC void store(int r, SValue *sv)
{
    tcc_error("ppc-gen: store stub (r=%d)", r);
}

ST_FUNC void gfunc_call(int nb_args)
{
    tcc_error("ppc-gen: gfunc_call stub (nb_args=%d)", nb_args);
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    tcc_error("ppc-gen: gfunc_prolog stub");
}

ST_FUNC void gfunc_epilog(void)
{
    tcc_error("ppc-gen: gfunc_epilog stub");
}

/* Apple PPC ABI: structs <= 8 bytes returned in r3:r4; larger via
 * caller-allocated buffer pointed to by an implicit hidden first arg
 * passed in r3. Stub for now. */
ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    *ret_align = 4;
    *regsize = 4;
    /* 0 = struct returned via memory; 1+ = number of regs used. */
    return 0;
}

ST_FUNC int gjmp(int t)
{
    tcc_error("ppc-gen: gjmp stub (t=%d)", t);
    return 0;
}

ST_FUNC void gjmp_addr(int a)
{
    tcc_error("ppc-gen: gjmp_addr stub (a=0x%x)", a);
}

ST_FUNC int gjmp_append(int n, int t)
{
    tcc_error("ppc-gen: gjmp_append stub");
    return 0;
}

ST_FUNC int gjmp_cond(int op, int t)
{
    tcc_error("ppc-gen: gjmp_cond stub (op=0x%x)", op);
    return 0;
}

ST_FUNC void gen_opi(int op)
{
    tcc_error("ppc-gen: gen_opi stub (op=0x%x)", op);
}

ST_FUNC void gen_opf(int op)
{
    tcc_error("ppc-gen: gen_opf stub (op=0x%x)", op);
}

ST_FUNC void gen_cvt_itof(int t)
{
    tcc_error("ppc-gen: gen_cvt_itof stub");
}

ST_FUNC void gen_cvt_ftoi(int t)
{
    tcc_error("ppc-gen: gen_cvt_ftoi stub");
}

ST_FUNC void gen_cvt_ftof(int t)
{
    tcc_error("ppc-gen: gen_cvt_ftof stub");
}

ST_FUNC void gen_cvt_csti(int t)
{
    tcc_error("ppc-gen: gen_cvt_csti stub");
}

ST_FUNC void gen_increment_tcov(SValue *sv)
{
    tcc_error("ppc-gen: gen_increment_tcov stub");
}

ST_FUNC void ggoto(void)
{
    tcc_error("ppc-gen: ggoto stub");
}

/* VLA support — stubs. Tcc compiles fine without VLAs in user code;
 * these only fire if the source uses one. */
ST_FUNC void gen_vla_sp_save(int addr)
{
    tcc_error("ppc-gen: VLAs not supported");
}

ST_FUNC void gen_vla_sp_restore(int addr)
{
    tcc_error("ppc-gen: VLAs not supported");
}

ST_FUNC void gen_vla_alloc(CType *type, int align)
{
    tcc_error("ppc-gen: VLAs not supported");
}

#endif /* !TARGET_DEFS_ONLY */
