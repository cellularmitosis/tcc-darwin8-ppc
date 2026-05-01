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

#define IS_FREG(x) ((x) >= TREG_F(0))

/* Function-prolog backfill bookkeeping. The prolog is reserved at
 * gfunc_prolog time but emitted at gfunc_epilog time, when we know
 * the final frame size. Mirrors i386-gen.c's pattern. */
static unsigned long func_sub_sp_offset;

/* Apple PPC ABI frame layout. After our prologue:
 *
 *   high addr (caller's frame)
 *   +----+ <-- OLD_SP, our r31 (frame pointer / FP)
 *   | r31 save (4 bytes)        <-- at r31-4 (i.e., loc=-4 reserved)
 *   | user locals (loc=-8 down) <-- addressed via offset(r31)
 *   |    ...
 *   | 24-byte linkage area
 *   +----+ <-- NEW_SP, our r1
 *
 * r31 is a callee-saved register in the Apple PPC ABI; we save it
 * to the top of our own frame and use it as a stable frame pointer
 * for the duration of the function. Locals are addressed at
 * negative offsets from r31 (matching tcc's `loc` convention from
 * i386-gen.c).
 *
 * Prologue (5 instructions = 20 bytes):
 *   mflr r0
 *   stw  r0, 8(r1)              ; save LR in caller's linkage area
 *   stwu r1, -frame_size(r1)    ; allocate frame, set back-chain
 *   stw  r31, frame_size-4(r1)  ; save old r31 at our frame top
 *   addi r31, r1, frame_size    ; FP = OLD_SP
 *
 * Epilogue (5 instructions = 20 bytes):
 *   lwz  r31, frame_size-4(r1)  ; restore r31 (must precede addi)
 *   addi r1, r1, frame_size     ; restore SP
 *   lwz  r0, 8(r1)              ; reload LR from caller's linkage
 *   mtlr r0
 *   blr
 */
#define PPC_PROLOG_SIZE 20
#define PPC_LINKAGE_SIZE 24
#define PPC_STACK_ALIGN 16
#define PPC_FP_REG 31

static int ppc_frame_size(void)
{
    /* loc is non-positive (locals grow downward). loc=-4 is reserved
     * for the FP-save slot at the top of the frame. User locals
     * start at loc=-8. Frame holds linkage + (-loc) bytes of saved-
     * FP-plus-locals, rounded up to 16. Minimum 32. */
    int n = PPC_LINKAGE_SIZE + (-loc);
    n = (n + PPC_STACK_ALIGN - 1) & -PPC_STACK_ALIGN;
    if (n < 32) n = 32;
    return n;
}

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

static uint32_t ppc_read32be(unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* Read the chain-link offset stored in an unconditional `b`
 * instruction at byte position `pos` and return the absolute
 * position the link points to (or 0 for end-of-chain). */
static int ppc_b_chain_decode(int pos)
{
    uint32_t w = ppc_read32be(cur_text_section->data + pos);
    int32_t disp = w & 0x03ffffff;
    if (disp & 0x02000000) disp |= 0xfc000000;
    disp &= ~3;
    return pos + disp;
}

/* Encode an unconditional `b` whose displacement encodes a chain
 * link to `target` (target=0 ⇒ end-of-chain). */
static uint32_t ppc_b_chain(int pos, int target)
{
    int32_t disp = target - pos;
    return 0x48000000u | ((uint32_t)disp & 0x03fffffc);
}

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

/* Walk the unresolved-jump chain rooted at `t`, patching each
 * branch to point at absolute byte position `a`. */
ST_FUNC void gsym_addr(int t, int a)
{
    while (t) {
        int next = ppc_b_chain_decode(t);
        uint8_t *p = cur_text_section->data + t;
        uint32_t w = ppc_read32be(p);
        int32_t new_disp = (a - t) & 0x03fffffc;
        ppc_write32be(p, (w & ~0x03fffffc) | new_disp);
        t = next;
    }
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

/* Emit a load-immediate of a 32-bit value into GPR `gpr`. Uses
 *   li  rD, simm                  (one instruction, signed 16-bit)
 *   lis rD, hi                    (one instruction, low 16 zero)
 *   lis rD, hi  / ori rD, rD, lo  (two instructions, full 32-bit)
 */
static void ppc_emit_li(int gpr, int32_t v)
{
    if (v >= -0x8000 && v < 0x8000) {
        /* li rD, v  ->  addi rD, 0, v  (sign-extends) */
        o(0x38000000 | (gpr << 21) | (v & 0xffff));
    } else if ((v & 0xffff) == 0) {
        /* lis rD, hi  ->  addis rD, 0, hi */
        o(0x3c000000 | (gpr << 21) | ((v >> 16) & 0xffff));
    } else {
        /* lis rD, hi ; ori rD, rD, lo */
        o(0x3c000000 | (gpr << 21) | ((v >> 16) & 0xffff));
        o(0x60000000 | (gpr << 21) | (gpr << 16) | (v & 0xffff));
    }
}

ST_FUNC void load(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int gpr;
    int offset;

    /* Value already in a register slot (no VT_LVAL): move-register.
     * tcc requests this when forcing a value into a specific register
     * class (e.g. RC_IRET) where it currently lives in some other
     * register. */
    if (v < VT_CONST && !(sv->r & VT_LVAL)) {
        int src = v;
        if (src == r) return;  /* nothing to do */
        if (IS_FREG(r) != IS_FREG(src))
            tcc_error("ppc-gen: cross-bank int<->FP register move not supported");
        if (IS_FREG(r)) {
            int dst_fpr = TREG_TO_FPR(r);
            int src_fpr = TREG_TO_FPR(src);
            /* fmr fD, fB  --  opcode 63, ext 72 (X-form). */
            o(0xfc000090 | (dst_fpr << 21) | (src_fpr << 11));
        } else {
            int dst_gpr = TREG_TO_GPR(r);
            int src_gpr = TREG_TO_GPR(src);
            /* mr rA, rS  =  or rA, rS, rS  --  opcode 31, ext 444. */
            o(0x7c000378 | (src_gpr << 21) | (dst_gpr << 16) | (src_gpr << 11));
        }
        return;
    }

    /* Integer constant immediate. */
    if (v == VT_CONST && !(sv->r & VT_LVAL) && !(sv->r & VT_SYM)) {
        if (IS_FREG(r)) {
            tcc_error("ppc-gen: load FP constant not yet implemented");
        }
        gpr = TREG_TO_GPR(r);
        ppc_emit_li(gpr, (int32_t)sv->c.i);
        return;
    }

    /* Local variable, address taken (rvalue = its address). */
    if (v == VT_LOCAL && !(sv->r & VT_LVAL)) {
        if (IS_FREG(r)) tcc_error("ppc-gen: address-of into FP register?");
        gpr = TREG_TO_GPR(r);
        offset = (int)sv->c.i;
        /* addi rD, r31, offset */
        o(0x38000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
        return;
    }

    /* Local variable, lvalue read (load from memory at FP+offset). */
    if (v == VT_LOCAL && (sv->r & VT_LVAL)) {
        int bt = sv->type.t & VT_BTYPE;
        offset = (int)sv->c.i;
        if (IS_FREG(r)) {
            tcc_error("ppc-gen: FP local load not yet implemented");
        }
        gpr = TREG_TO_GPR(r);
        switch (bt) {
        case VT_BYTE:
            /* lbz rD, d(rA) — opcode 34. Sign-extend with extsb if signed. */
            o(0x88000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            if (!(sv->type.t & VT_UNSIGNED))
                o(0x7c000774 | (gpr << 21) | (gpr << 16));  /* extsb rA, rS */
            return;
        case VT_SHORT:
            if (sv->type.t & VT_UNSIGNED) {
                /* lhz — opcode 40 */
                o(0xa0000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            } else {
                /* lha — opcode 42 (sign-extending) */
                o(0xa8000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            }
            return;
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:
            /* lwz rD, d(rA) — opcode 32 = 0x80000000 */
            o(0x80000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            return;
        default:
            tcc_error("ppc-gen: load VT_LOCAL of basic type 0x%x not yet supported", bt);
        }
    }

    tcc_error("ppc-gen: load stub (r=%d sv->r=0x%x type=0x%x val=0x%llx)",
              r, sv->r, sv->type.t, (long long)sv->c.i);
}

ST_FUNC void store(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int gpr;
    int offset;
    int bt = sv->type.t & VT_BTYPE;

    if (v == VT_LOCAL) {
        offset = (int)sv->c.i;
        if (IS_FREG(r)) {
            tcc_error("ppc-gen: store FP local not yet implemented");
        }
        gpr = TREG_TO_GPR(r);
        switch (bt) {
        case VT_BYTE:
            /* stb rS, d(rA) — opcode 38 */
            o(0x98000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            return;
        case VT_SHORT:
            /* sth rS, d(rA) — opcode 44 */
            o(0xb0000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            return;
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:
            /* stw rS, d(rA) — opcode 36 = 0x90000000 */
            o(0x90000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            return;
        default:
            tcc_error("ppc-gen: store VT_LOCAL of bt 0x%x not yet supported", bt);
        }
    }

    tcc_error("ppc-gen: store stub (r=%d sv->r=0x%x bt=0x%x)", r, sv->r, bt);
}

ST_FUNC void gfunc_call(int nb_args)
{
    tcc_error("ppc-gen: gfunc_call stub (nb_args=%d)", nb_args);
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    /* Reserve PROLOG_SIZE bytes for backfill in gfunc_epilog. */
    ind += PPC_PROLOG_SIZE;
    func_sub_sp_offset = ind;
    /* loc=-4 reserves the FP-save slot at r31-4. User locals begin
     * at loc=-8 after tcc's first allocation. */
    loc = -4;
    func_vc = 0;
    /* No parameter unpacking in v1 (no params supported yet). */
    (void)func_sym;
}

ST_FUNC void gfunc_epilog(void)
{
    addr_t saved_ind;
    int frame_size = ppc_frame_size();
    int fp_save_off = frame_size - 4;  /* offset of saved-r31 slot from new SP */

    /* Note: tccgen.c already calls gsym(rsym) before invoking us
     * (see tccgen.c around `gsym(rsym); nocode_wanted = 0;` just
     * before gfunc_epilog), so any pending return-jumps have
     * already been patched to land at our current ind. We don't
     * touch rsym here. */

    /* Epilogue:
     *   lwz  r31, fp_save_off(r1)
     *   addi r1, r1, frame_size
     *   lwz  r0, 8(r1)
     *   mtlr r0
     *   blr
     */
    o(0x80000000 | (PPC_FP_REG << 21) | (1 << 16) | (fp_save_off & 0xffff));
    o(0x38210000 | (frame_size & 0xffff));
    o(0x80010008);
    o(0x7c0803a6);
    o(0x4e800020);

    /* Backfill the reserved prologue. */
    saved_ind = ind;
    ind = func_sub_sp_offset - PPC_PROLOG_SIZE;
    /* mflr r0 */
    o(0x7c0802a6);
    /* stw r0, 8(r1) */
    o(0x90010008);
    /* stwu r1, -frame_size(r1) */
    o(0x94210000 | ((-frame_size) & 0xffff));
    /* stw r31, fp_save_off(r1) */
    o(0x90000000 | (PPC_FP_REG << 21) | (1 << 16) | (fp_save_off & 0xffff));
    /* addi r31, r1, frame_size */
    o(0x38000000 | (PPC_FP_REG << 21) | (1 << 16) | (frame_size & 0xffff));
    ind = saved_ind;
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

/* Unconditional branch with chain link to `t`. Returns the position
 * of the emitted branch (the new chain head). */
ST_FUNC int gjmp(int t)
{
    int r;
    if (nocode_wanted)
        return t;
    r = ind;
    o(ppc_b_chain(r, t));
    return r;
}

/* Unconditional branch to a fixed (already-known) byte address. */
ST_FUNC void gjmp_addr(int a)
{
    int32_t disp = a - ind;
    o(0x48000000u | ((uint32_t)disp & 0x03fffffc));
}

/* Append chain `n` to chain `t` and return the merged head. */
ST_FUNC int gjmp_append(int n, int t)
{
    if (n) {
        int lp = n;
        int p = ppc_b_chain_decode(lp);
        while (p) {
            lp = p;
            p = ppc_b_chain_decode(lp);
        }
        /* lp is the chain terminator; patch it to point at t. */
        uint8_t *q = cur_text_section->data + lp;
        uint32_t w = ppc_read32be(q);
        int32_t new_disp = (t - lp) & 0x03fffffc;
        ppc_write32be(q, (w & ~0x03fffffc) | new_disp);
        t = n;
    }
    return t;
}

/* Conditional branch. Pattern: `bc <skip>, .+8 ; b <chain>` so that
 * the chained instruction is always a 24-bit `b` (one chain encoding
 * to maintain). Cost: 1 extra instruction per conditional vs. using
 * `bc`'s 14-bit BD field directly. Worth it for simplicity until we
 * grow large enough functions to need BD-direct.
 *
 * `op` is a tcc TOK_EQ..TOK_GT comparison; we emit the bc that
 * SKIPS the chained b when the condition is FALSE (i.e., when we
 * shouldn't take the branch). */
ST_FUNC int gjmp_cond(int op, int t)
{
    int r;
    int take_bo, bi;

    /* Map op -> (BO, BI) for "branch if op holds". BI selects the
     * cr0 bit: 0=LT, 1=GT, 2=EQ. BO=12 means take when bit is set,
     * BO=4 means take when bit is clear. */
    switch (op) {
    case TOK_EQ:                take_bo = 12; bi = 2; break;
    case TOK_NE:                take_bo = 4;  bi = 2; break;
    case TOK_LT: case TOK_ULT:  take_bo = 12; bi = 0; break;
    case TOK_GE: case TOK_UGE:  take_bo = 4;  bi = 0; break;
    case TOK_GT: case TOK_UGT:  take_bo = 12; bi = 1; break;
    case TOK_LE: case TOK_ULE:  take_bo = 4;  bi = 1; break;
    default:
        tcc_error("ppc-gen: gjmp_cond op 0x%x not supported", op);
        return t;
    }

    if (nocode_wanted)
        return t;

    /* bc <inverted-BO>, BI, .+8  -- skip the next 4-byte b if
     * we should NOT take the branch. */
    o(0x40000000u | ((take_bo ^ 8) << 21) | (bi << 16) | 8);

    /* b <chain-to-t>  -- taken if condition holds. */
    r = ind;
    o(ppc_b_chain(r, t));
    return r;
}

/* Two-register integer ops. Result goes into vtop[-1]'s slot;
 * vtop[0] is consumed.
 *
 * PPC has two main encoding shapes for these:
 *   XO-form arithmetic (add/sub/mul):  rD at 21..25, rA at 16..20, rB at 11..15
 *   X-form logical / shift:            rS at 21..25, rA(=dst) at 16..20, rB at 11..15
 */
ST_FUNC void gen_opi(int op)
{
    int ra_slot, rb_slot, ra_gpr, rb_gpr;

    /* Comparisons: emit cmpw / cmplw into cr0, then mark vtop as
     * VT_CMP. The actual conditional branch is emitted later by
     * gjmp_cond(). */
    if (op >= TOK_ULT && op <= TOK_GT) {
        int unsigned_cmp = (op == TOK_ULT || op == TOK_UGE ||
                            op == TOK_ULE || op == TOK_UGT);
        gv2(RC_INT, RC_INT);
        ra_slot = vtop[-1].r;
        rb_slot = vtop[0].r;
        ra_gpr = TREG_TO_GPR(ra_slot);
        rb_gpr = TREG_TO_GPR(rb_slot);
        if (unsigned_cmp) {
            /* cmplw cr0, rA, rB (X-form, opcode 31, ext 32) */
            o(0x7c000040 | (ra_gpr << 16) | (rb_gpr << 11));
        } else {
            /* cmpw cr0, rA, rB (X-form, opcode 31, ext 0) */
            o(0x7c000000 | (ra_gpr << 16) | (rb_gpr << 11));
        }
        vtop--;
        vset_VT_CMP(op);
        return;
    }

    gv2(RC_INT, RC_INT);
    ra_slot = vtop[-1].r;
    rb_slot = vtop[0].r;
    ra_gpr = TREG_TO_GPR(ra_slot);
    rb_gpr = TREG_TO_GPR(rb_slot);

    switch (op) {
    case '+':
        /* add rD, rA, rB */
        o(0x7c000214 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case '-':
        /* subf rD, rA, rB  computes rD = rB - rA, so swap. */
        o(0x7c000050 | (ra_gpr << 21) | (rb_gpr << 16) | (ra_gpr << 11));
        break;
    case '*':
        /* mullw rD, rA, rB */
        o(0x7c0001d6 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case '&':
        /* and rA, rS, rB */
        o(0x7c000038 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case '|':
        /* or rA, rS, rB */
        o(0x7c000378 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case '^':
        /* xor rA, rS, rB */
        o(0x7c000278 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case TOK_SHL:
        /* slw rA, rS, rB */
        o(0x7c000030 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case TOK_SHR:
        /* srw rA, rS, rB (logical) */
        o(0x7c000430 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case TOK_SAR:
        /* sraw rA, rS, rB (arithmetic) */
        o(0x7c000630 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case '/':
        /* divw rD, rA, rB (signed) */
        o(0x7c0003d6 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case TOK_UDIV:
        /* divwu rD, rA, rB (unsigned) */
        o(0x7c000396 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case '%':
    case TOK_UMOD:
        /* PPC has no modulo instruction; compute as a - (a/b)*b.
         * Use a temp register? For now, error so we notice. */
        tcc_error("ppc-gen: modulo not yet implemented");
        break;
    default:
        tcc_error("ppc-gen: gen_opi op 0x%x not yet supported", op);
    }
    vtop--;
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
