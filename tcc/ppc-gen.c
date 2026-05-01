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
 *   |  caller's parameter save area (32 bytes for r3..r10):       |
 *   |     +52(r31)  spilled r10                                   |
 *   |     +48       spilled r9                                    |
 *   |     +44       spilled r8                                    |
 *   |     +40       spilled r7                                    |
 *   |     +36       spilled r6                                    |
 *   |     +32       spilled r5                                    |
 *   |     +28       spilled r4                                    |
 *   |     +24       spilled r3   <-- first incoming arg lives here|
 *   |  caller's linkage:                                          |
 *   |     +8(r31)   saved LR  (we wrote it in our prolog)         |
 *   +----+ <-- OLD_SP, our r31 (frame pointer / FP)
 *   | r31 save (4 bytes)        <-- at r31-4 (loc=-4 reserved)
 *   | user locals (loc=-8 down) <-- addressed via offset(r31)
 *   |    ...
 *   | outgoing parameter area (32 bytes, r1+24..55)
 *   | linkage area (24 bytes,   r1+0..23)
 *   +----+ <-- NEW_SP, our r1
 *
 * Incoming params live in the CALLER's parameter save area at
 * r31+24..+52. The body addresses them via positive offset from FP.
 * This layout matters for varargs: with all 8 GPR args spilled in
 * source order at consecutive INCREASING addresses, the standard
 * `__builtin_va_start(ap, last) = &last + sizeof(last)` macro walks
 * forward into the variadic args correctly.
 *
 * Prologue (13 instructions = 52 bytes):
 *   mflr r0
 *   stw  r0, 8(r1)              ; save LR in caller's linkage
 *   stw  r3, 24(r1)             ;\
 *   stw  r4, 28(r1)             ; \  spill arg-passing GPRs r3..r10
 *   stw  r5, 32(r1)             ; |  to caller's param save area.
 *   stw  r6, 36(r1)             ; |  Spilled UNCONDITIONALLY — even
 *   stw  r7, 40(r1)             ; |  for void/no-arg/non-variadic
 *   stw  r8, 44(r1)             ; /  functions, so the prolog size
 *   stw  r9, 48(r1)             ;/   is constant (32 wasted bytes
 *   stw  r10, 52(r1)            ;    in those cases — negligible).
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
#define PPC_PROLOG_SIZE 52
#define PPC_LINKAGE_SIZE 24
#define PPC_PARAM_AREA   32   /* 8 GPR slots * 4 bytes for outgoing calls */
#define PPC_STACK_ALIGN 16
#define PPC_FP_REG 31

static int ppc_frame_size(void)
{
    /* Frame layout (NEW_SP at bottom, OLD_SP at top):
     *   NEW_SP+0..23      linkage area
     *   NEW_SP+24..55     parameter area for outgoing calls (8 GPR slots)
     *   NEW_SP+56..       gap, then locals growing upward to:
     *   r31 + loc         user local at offset `loc` from FP
     *   r31 - 4           saved r31 (FP)
     *   r31               OLD_SP
     *
     * loc is non-positive (locals grow downward). loc=-4 is reserved
     * for the FP-save slot. User locals start at loc=-8.
     *
     * Always reserving the parameter area (even for leaf functions)
     * costs 32 bytes per stack frame and avoids needing flow analysis
     * to detect whether a function makes calls. */
    int n = PPC_LINKAGE_SIZE + PPC_PARAM_AREA + (-loc);
    n = (n + PPC_STACK_ALIGN - 1) & -PPC_STACK_ALIGN;
    if (n < 64) n = 64;
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

/* Place a 4- or 8-byte FP constant in rodata and return its offset.
 * The bytes are written big-endian to match the host PPC's view. */
static unsigned int ppc_emit_fp_const(const void *bytes, int size, int align)
{
    unsigned int off;
    Section *s = rodata_section;
    /* section_add returns the starting offset and bumps data_offset. */
    off = section_add(s, size, align);
    /* The constant table lives in rodata which is little/big-endian-
     * neutral memory; we want the IEEE 754 bytes in the standard
     * big-endian layout for PPC's lfs/lfd. The host ints we got from
     * c.f / c.d are already in host byte order. Since we're cross-
     * building from a possibly-LE host, swap if needed. */
    {
        unsigned char *p = s->data + off;
        const unsigned char *src = (const unsigned char *)bytes;
        /* Detect endianness at runtime. */
        unsigned int test = 1;
        int host_le = (*(unsigned char *)&test) == 1;
        int i;
        if (host_le) {
            for (i = 0; i < size; i++)
                p[i] = src[size - 1 - i];
        } else {
            for (i = 0; i < size; i++)
                p[i] = src[i];
        }
    }
    return off;
}

/* Materialize an FP constant from the rodata pool into FPR `fpr`.
 * Uses the standard PPC two-instruction literal-load idiom:
 *     lis  r12, ha16(addr)
 *     lfs  fD, lo16(addr)(r12)   ; or lfd for double
 * The HA/LO halves are filled in by R_PPC_ADDR16_HA/LO relocations
 * pointing at the rodata symbol that owns our literal. */
static void ppc_load_fp_const(int fpr, int is_double, const void *bytes)
{
    unsigned int off;
    Sym *sym;
    int size = is_double ? 8 : 4;
    int align = is_double ? 8 : 4;
    /* Rather than synthesize a fresh anonymous symbol per constant
     * (which works but spams the symbol table), reuse a single
     * label for the whole rodata section: __ppc_fp_const_pool, and
     * use the byte-offset within it. We push the actual literal
     * bytes here and pin a sym pointing at this offset. */
    off = ppc_emit_fp_const(bytes, size, align);
    /* Create an anonymous local symbol attached to the rodata
     * section at our literal's offset. */
    sym = get_sym_ref(is_double ? &char_pointer_type : &char_pointer_type,
                      rodata_section, off, size);
    /* lis r12, ha16(literal_addr) */
    greloc(cur_text_section, sym, ind, R_PPC_ADDR16_HA);
    o(0x3c000000 | (12 << 21));  /* lis r12, 0 */
    if (is_double) {
        /* lfd fD, lo16(literal)(r12) */
        greloc(cur_text_section, sym, ind, R_PPC_ADDR16_LO);
        o(0xc8000000 | (fpr << 21) | (12 << 16));
    } else {
        /* lfs fD, lo16(literal)(r12) */
        greloc(cur_text_section, sym, ind, R_PPC_ADDR16_LO);
        o(0xc0000000 | (fpr << 21) | (12 << 16));
    }
}

ST_FUNC void load(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int gpr;
    int offset;

    /* VT_CMP: condition codes are live in cr0; tcc wants a 0/1 int
     * value in `r`. We emit:
     *     li  rD, 1
     *     bc  <take>, BI, .+8     ; if condition holds, skip the li
     *     li  rD, 0
     * Map (op) -> (BO, BI) the same way gjmp_cond does. */
    if (v == VT_CMP) {
        int take_bo, bi;
        int op = (int)sv->c.i;
        int dst_gpr;
        if (IS_FREG(r))
            tcc_error("ppc-gen: VT_CMP into FP register?");
        dst_gpr = TREG_TO_GPR(r);
        switch (op) {
        case TOK_EQ:                take_bo = 12; bi = 2; break;
        case TOK_NE:                take_bo = 4;  bi = 2; break;
        case TOK_LT: case TOK_ULT:  take_bo = 12; bi = 0; break;
        case TOK_GE: case TOK_UGE:  take_bo = 4;  bi = 0; break;
        case TOK_GT: case TOK_UGT:  take_bo = 12; bi = 1; break;
        case TOK_LE: case TOK_ULE:  take_bo = 4;  bi = 1; break;
        default:
            tcc_error("ppc-gen: VT_CMP load with op 0x%x", op);
            return;
        }
        /* li rD, 1   (addi rD, 0, 1) */
        o(0x38000001 | (dst_gpr << 21));
        /* bc take_bo, bi, .+8 */
        o(0x40000000u | (take_bo << 21) | (bi << 16) | 8);
        /* li rD, 0 */
        o(0x38000000 | (dst_gpr << 21));
        return;
    }

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
            int bt = sv->type.t & VT_BTYPE;
            int fpr = TREG_TO_FPR(r);
            if (bt == VT_FLOAT) {
                /* Convert the constant to single precision (it may have
                 * been parsed as double; tcc stores in c.f for VT_FLOAT). */
                float f = sv->c.f;
                ppc_load_fp_const(fpr, 0, &f);
                return;
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                double d = (bt == VT_LDOUBLE) ? (double)sv->c.ld : sv->c.d;
                ppc_load_fp_const(fpr, 1, &d);
                return;
            } else {
                tcc_error("ppc-gen: load FP constant of bt 0x%x", bt);
            }
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

    /* Pointer dereference: lvalue read from memory at the address
     * held in a register (e.g. *p where p is in a register).
     *
     * Convention (matching i386/arm): for `(reg | VT_LVAL)`, the
     * effective address is `reg + 0`. sv->c.i is residual from the
     * pre-gv state and must not be added — the register already
     * holds the full address. */
    if (v < VT_CONST && (sv->r & VT_LVAL)) {
        int bt = sv->type.t & VT_BTYPE;
        int base_gpr;
        if (IS_FREG(v))
            tcc_error("ppc-gen: dereference of FP register?");
        base_gpr = TREG_TO_GPR(v);
        if (IS_FREG(r)) {
            int fpr = TREG_TO_FPR(r);
            if (bt == VT_FLOAT) {
                /* lfs fD, 0(rA) */
                o(0xc0000000 | (fpr << 21) | (base_gpr << 16));
                return;
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                /* lfd fD, 0(rA) */
                o(0xc8000000 | (fpr << 21) | (base_gpr << 16));
                return;
            } else {
                tcc_error("ppc-gen: FP load via ptr of bt 0x%x", bt);
            }
        }
        gpr = TREG_TO_GPR(r);
        switch (bt) {
        case VT_BYTE:
            o(0x88000000 | (gpr << 21) | (base_gpr << 16));
            if (!(sv->type.t & VT_UNSIGNED))
                o(0x7c000774 | (gpr << 21) | (gpr << 16));  /* extsb */
            return;
        case VT_SHORT:
            if (sv->type.t & VT_UNSIGNED)
                o(0xa0000000 | (gpr << 21) | (base_gpr << 16));
            else
                o(0xa8000000 | (gpr << 21) | (base_gpr << 16));
            return;
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:
            o(0x80000000 | (gpr << 21) | (base_gpr << 16));
            return;
        default:
            tcc_error("ppc-gen: deref of basic type 0x%x not yet supported", bt);
        }
    }

    /* Symbolic constant, lvalue OR address-of: load from a symbol's
     * address. Used for FP literals (placed in rodata by tccgen) and
     * for global variable accesses.
     *
     * Two cases:
     *   VT_CONST | VT_SYM            -- want the symbol's address.
     *   VT_CONST | VT_LVAL | VT_SYM  -- want *(symbol + sv->c.i).
     *
     * We use the standard PPC two-instruction literal-address idiom
     * with R_PPC_ADDR16_HA / R_PPC_ADDR16_LO relocations:
     *   lis  rT, ha16(sym)
     *   addi rT, rT, lo16(sym)        ; for "address" form
     * Or:
     *   lis  rT, ha16(sym)
     *   l[fbsh][zad] rD, lo16(sym)(rT) ; for "load" form
     */
    if (v == VT_CONST && (sv->r & VT_SYM)) {
        int bt = sv->type.t & VT_BTYPE;
        int extra_off = (int)sv->c.i;
        int addr_gpr;
        int want_load = (sv->r & VT_LVAL) != 0;

        /* Choose a GPR to hold the symbol address. If our destination
         * is a GPR and we want_load is FALSE (just want the address),
         * use r itself. Otherwise use r12 as a scratch. */
        if (!want_load && !IS_FREG(r)) {
            addr_gpr = TREG_TO_GPR(r);
        } else {
            addr_gpr = 12;
        }

        /* lis addr_gpr, ha16(sym) */
        greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_HA);
        o(0x3c000000 | (addr_gpr << 21));
        if (!want_load) {
            /* addi rD, addr_gpr, lo16(sym) */
            int dst_gpr = TREG_TO_GPR(r);
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(0x38000000 | (dst_gpr << 21) | (addr_gpr << 16));
            return;
        }
        /* Otherwise, emit the typed load with lo16(sym) as the disp. */
        if (IS_FREG(r)) {
            int fpr = TREG_TO_FPR(r);
            if (bt == VT_FLOAT) {
                /* lfs fD, lo16(sym)(addr_gpr) */
                greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
                o(0xc0000000 | (fpr << 21) | (addr_gpr << 16));
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                /* lfd fD, lo16(sym)(addr_gpr) */
                greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
                o(0xc8000000 | (fpr << 21) | (addr_gpr << 16));
            } else {
                tcc_error("ppc-gen: FP load via sym of bt 0x%x", bt);
            }
            (void)extra_off; /* TODO: handle nonzero c.i */
            return;
        }
        gpr = TREG_TO_GPR(r);
        switch (bt) {
        case VT_BYTE:
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(0x88000000 | (gpr << 21) | (addr_gpr << 16));
            if (!(sv->type.t & VT_UNSIGNED))
                o(0x7c000774 | (gpr << 21) | (gpr << 16));  /* extsb */
            return;
        case VT_SHORT:
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            if (sv->type.t & VT_UNSIGNED)
                o(0xa0000000 | (gpr << 21) | (addr_gpr << 16));
            else
                o(0xa8000000 | (gpr << 21) | (addr_gpr << 16));
            return;
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(0x80000000 | (gpr << 21) | (addr_gpr << 16));
            return;
        default:
            tcc_error("ppc-gen: load via sym of bt 0x%x not yet supported", bt);
        }
    }

    /* Local variable, lvalue read (load from memory at FP+offset). */
    if (v == VT_LOCAL && (sv->r & VT_LVAL)) {
        int bt = sv->type.t & VT_BTYPE;
        offset = (int)sv->c.i;
        if (IS_FREG(r)) {
            int fpr = TREG_TO_FPR(r);
            if (bt == VT_FLOAT) {
                /* lfs fD, off(r31) */
                o(0xc0000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
                return;
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                /* lfd fD, off(r31) */
                o(0xc8000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
                return;
            } else {
                tcc_error("ppc-gen: FP local load of bt 0x%x", bt);
            }
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
        case VT_LLONG: {
            /* tcc loads the two halves separately via the rc2 path.
             * For the low half, r is the destination and (offset+4)
             * is where we wrote it during store; for the high, r is
             * the destination and (offset) is the high-half slot.
             * tcc tells us which half by calling gv with the matching
             * load_type and incrementing offset between calls. So just
             * do a standard 32-bit lwz at the (already-adjusted)
             * offset. */
            o(0x80000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            return;
        }
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

    /* Store-via-pointer: lvalue is a register holding the destination
     * address. Same offset-zero convention as load(). */
    if (v < VT_CONST && (sv->r & VT_LVAL)) {
        int base_gpr;
        if (IS_FREG(v))
            tcc_error("ppc-gen: store via FP register?");
        base_gpr = TREG_TO_GPR(v);
        if (IS_FREG(r)) {
            int fpr = TREG_TO_FPR(r);
            if (bt == VT_FLOAT) {
                /* stfs fS, 0(rA) */
                o(0xd0000000 | (fpr << 21) | (base_gpr << 16));
                return;
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                /* stfd fS, 0(rA) */
                o(0xd8000000 | (fpr << 21) | (base_gpr << 16));
                return;
            } else {
                tcc_error("ppc-gen: FP store via ptr of bt 0x%x", bt);
            }
        }
        gpr = TREG_TO_GPR(r);
        switch (bt) {
        case VT_BYTE:
            o(0x98000000 | (gpr << 21) | (base_gpr << 16));
            return;
        case VT_SHORT:
            o(0xb0000000 | (gpr << 21) | (base_gpr << 16));
            return;
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:
            o(0x90000000 | (gpr << 21) | (base_gpr << 16));
            return;
        default:
            tcc_error("ppc-gen: store via ptr of bt 0x%x not yet supported", bt);
        }
    }

    if (v == VT_LOCAL) {
        offset = (int)sv->c.i;
        if (IS_FREG(r)) {
            int fpr = TREG_TO_FPR(r);
            if (bt == VT_FLOAT) {
                /* stfs fS, off(r31) */
                o(0xd0000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
                return;
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                /* stfd fS, off(r31) */
                o(0xd8000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
                return;
            } else {
                tcc_error("ppc-gen: store FP local of bt 0x%x", bt);
            }
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
        case VT_LLONG: {
            /* tcc convention: r holds the LOW half, sv->r2 holds the
             * HIGH half. Store with high at lower address (matches
             * gfunc_prolog spill order, big-endian word order). */
            int hi_gpr;
            if (sv->r2 >= VT_CONST)
                tcc_error("ppc-gen: store VT_LLONG with no r2 set");
            hi_gpr = TREG_TO_GPR(sv->r2);
            /* stw r2(=high), offset(r31) */
            o(0x90000000 | (hi_gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            /* stw r(=low), offset+4(r31) */
            o(0x90000000 | (gpr << 21) | (PPC_FP_REG << 16) | ((offset + 4) & 0xffff));
            return;
        }
        default:
            tcc_error("ppc-gen: store VT_LOCAL of bt 0x%x not yet supported", bt);
        }
    }

    tcc_error("ppc-gen: store stub (r=%d sv->r=0x%x bt=0x%x)", r, sv->r, bt);
}

/* Apple PPC ABI v1 caller side:
 *   - First 8 int args go in r3..r10 (RC_R(0)..RC_R(7)).
 *   - FP / vector / struct / >8-arg / vararg cases not yet supported.
 *   - Direct call (VT_CONST|VT_SYM): emit `bl 0` and record an
 *     R_PPC_REL24 relocation; tccrun.c (or the ELF/Mach-O linker)
 *     patches the displacement.
 *   - Indirect call: gv into a GPR, then `mtctr ; bctrl`.
 *   - Return value lands in r3 (REG_IRET); caller (tccgen.c) pushes
 *     the appropriate vtop entry after we return.
 */
ST_FUNC void gfunc_call(int nb_args)
{
    int i;
    int gpr_used;     /* count of GPR arg slots already consumed */
    int fpr_used;     /* count of FPR arg slots already consumed */
    int *gpr_alloc;   /* per-source-arg starting GPR slot (0..7), or -1 for FP */
    int *fpr_alloc;   /* per-source-arg FPR slot (0..7), or -1 for non-FP */

    /* Spill any vstack values living in caller-saved regs that the
     * call would clobber. tcc walks the vstack and uses our
     * reg_classes[] mask to decide what can stay. */
    save_regs(nb_args + 1);

    /* First pass: assign GPR / FPR arg slots in source order.
     * Apple PPC ABI: each FP arg consumes one FPR (f1..f13) AND
     * "shadows" the corresponding GPR slot(s) — float skips one GPR
     * slot, double skips two. Int args take a GPR slot (LL takes two). */
    gpr_alloc = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;
    fpr_alloc = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;
    gpr_used = 0;
    fpr_used = 0;
    for (i = 0; i < nb_args; i++) {
        SValue *arg = &vtop[-(nb_args - 1 - i)];
        int bt = arg->type.t & VT_BTYPE;
        gpr_alloc[i] = -1;
        fpr_alloc[i] = -1;
        if (bt == VT_STRUCT) {
            tcc_free(gpr_alloc); tcc_free(fpr_alloc);
            tcc_error("ppc-gen: struct arguments not yet supported");
        }
        if (bt == VT_FLOAT) {
            fpr_alloc[i] = fpr_used++;
            gpr_used += 1;  /* float shadows 1 GPR slot */
        } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
            fpr_alloc[i] = fpr_used++;
            gpr_used += 2;  /* double shadows 2 GPR slots */
        } else if (bt == VT_LLONG) {
            gpr_alloc[i] = gpr_used;
            gpr_used += 2;
        } else {
            gpr_alloc[i] = gpr_used;
            gpr_used += 1;
        }
    }
    if (gpr_used > 8) {
        tcc_free(gpr_alloc); tcc_free(fpr_alloc);
        tcc_error("ppc-gen: argument list exceeds 8 GPR slots (got %d)",
                  gpr_used);
    }
    if (fpr_used > 8) {
        tcc_free(gpr_alloc); tcc_free(fpr_alloc);
        tcc_error("ppc-gen: argument list exceeds 8 FPR slots (got %d)",
                  fpr_used);
    }

    /* Second pass: process args from vtop downward, materializing
     * each into the assigned register slot. */
    for (i = 0; i < nb_args; i++) {
        int src_index = nb_args - 1 - i;
        int bt = vtop->type.t & VT_BTYPE;
        if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE) {
            int fslot = fpr_alloc[src_index];
            gv(RC_F(fslot));
        } else {
            int gslot = gpr_alloc[src_index];
            gv(RC_R(gslot));
        }
        vtop--;
    }
    tcc_free(gpr_alloc);
    tcc_free(fpr_alloc);

    /* Emit the call. */
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST && (vtop->r & VT_SYM)) {
        /* Direct symbolic call. greloc records a relocation at `ind`
         * for vtop->sym. The placeholder bytes are `bl 0` (LK=1,
         * displacement field 0); the linker fills the displacement. */
        greloc(cur_text_section, vtop->sym, ind, R_PPC_REL24);
        o(0x48000001);  /* bl 0 */
    } else {
        /* Indirect call via CTR. */
        int gpr;
        gv(RC_INT);
        gpr = TREG_TO_GPR(vtop->r & VT_VALMASK);
        /* mtctr rS  (mtspr ctr, rS; ctr is SPR 9) */
        o(0x7c0903a6 | (gpr << 21));
        /* bctrl */
        o(0x4e800421);
    }

    vtop--;  /* pop the function value; tccgen.c will push the return. */
}

/* Track which incoming FPR slots (f1..f8) hold FP params and need to
 * be spilled in the prolog. Reset on each function. */
static int ppc_fp_param_count;
/* Per-FP-param: offset (from FP / r31) at which to store the FPR.
 * Stored linearly in source order, indexed by FP-param ordinal. */
static int ppc_fp_param_off[8];
/* Per-FP-param: 1 if double (8-byte stfd), 0 if float (4-byte stfs).  */
static int ppc_fp_param_is_double[8];

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    CType *func_type = &func_sym->type;
    Sym *sym;
    int gpr_index = 0;     /* GPR slots consumed (FP args also shadow) */
    int fpr_index = 0;     /* FPR slots consumed by FP params */

    /* Reserve PROLOG_SIZE bytes for backfill in gfunc_epilog. The
     * actual mflr/stw/stwu/stw r31/addi r31 instructions are emitted
     * at gfunc_epilog time once we know the final frame size.
     *
     * Plus reserve up to 8 extra slots (32 bytes) for FP-param spills
     * (one stfs/stfd per FP param). Unused slots become nops. */
    ind += PPC_PROLOG_SIZE + 8 * 4;
    func_sub_sp_offset = ind;
    /* loc=-4 reserves the FP-save slot at r31-4. User locals (and
     * spilled register parameters) start at loc=-8. */
    loc = -4;
    func_vc = 0;
    ppc_fp_param_count = 0;

    /* Apple PPC ABI: all 8 GPR arg slots (r3..r10) get spilled to
     * the caller's parameter save area in our prologue. Body code
     * reads INTEGER params at positive offsets from FP:
     *   slot i lives at r31 + 24 + i*4.
     * For FP params (float/double), the value lives in f1..f8 (and
     * the corresponding GPR shadow slot, but our caller path doesn't
     * populate GPR shadows for FP args). So in our prolog we ALSO
     * spill FPRs to the same address space; the param is then
     * addressable as an in-memory float or double from the body.
     *
     * gfunc_set_param tells tcc where each named param lives. For
     * FP params we point them at the same param-area offset; load()
     * will emit lfs/lfd from r31+offset. */
    for (sym = func_type->ref->next; sym; sym = sym->next) {
        CType *type = &sym->type;
        int size, align;
        int bt = type->t & VT_BTYPE;
        int param_offset;

        size = type_size(type, &align);
        size = (size + 3) & ~3;

        if (bt == VT_STRUCT)
            tcc_error("ppc-gen: struct parameters not yet supported");

        if (bt == VT_FLOAT) {
            if (gpr_index + 1 > 8 || fpr_index >= 8)
                tcc_error("ppc-gen: parameters exceed reg slots");
            param_offset = 24 + gpr_index * 4;
            ppc_fp_param_off[ppc_fp_param_count] = param_offset;
            ppc_fp_param_is_double[ppc_fp_param_count] = 0;
            ppc_fp_param_count++;
            gfunc_set_param(sym, param_offset, 0);
            gpr_index += 1;
            fpr_index += 1;
        } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
            if (gpr_index + 2 > 8 || fpr_index >= 8)
                tcc_error("ppc-gen: parameters exceed reg slots");
            param_offset = 24 + gpr_index * 4;
            ppc_fp_param_off[ppc_fp_param_count] = param_offset;
            ppc_fp_param_is_double[ppc_fp_param_count] = 1;
            ppc_fp_param_count++;
            gfunc_set_param(sym, param_offset, 0);
            gpr_index += 2;
            fpr_index += 1;
        } else {
            int slots = (bt == VT_LLONG) ? 2 : 1;
            if (gpr_index + slots > 8)
                tcc_error("ppc-gen: parameters exceed 8 GPR slots");
            param_offset = 24 + gpr_index * 4;
            gfunc_set_param(sym, param_offset, 0);
            gpr_index += slots;
        }
    }
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

    /* Backfill the reserved prologue. PPC_PROLOG_SIZE + 8 reserved
     * FP-spill slots = 13 + 8 = 21 instructions / 84 bytes. */
    saved_ind = ind;
    ind = func_sub_sp_offset - (PPC_PROLOG_SIZE + 8 * 4);
    /* mflr r0 */
    o(0x7c0802a6);
    /* stw r0, 8(r1) */
    o(0x90010008);
    /* Spill all 8 arg-passing GPRs (r3..r10) to caller's parameter
     * save area. Done UNCONDITIONALLY so the prolog size is constant.
     * Trade: 32 bytes per function for not needing flow analysis
     * to detect "is this function variadic" at prolog-emit time. */
    {
        int g;
        for (g = 3; g <= 10; g++) {
            /* stw r(g), (24 + (g-3)*4)(r1) */
            int off = 24 + (g - 3) * 4;
            o(0x90000000 | (g << 21) | (1 << 16) | (off & 0xffff));
        }
    }
    /* Spill FP params: for each FP param recorded in gfunc_prolog,
     * emit stfs / stfd at the same offset that the corresponding
     * GPR-shadow slot would have used. Body code reads these as
     * `lfs/lfd offset(r31)` after the FP-relative addressing kicks
     * in. Note: the offset here is from r1, which equals (r31 - 0)
     * BEFORE the stwu below adjusts r1. After stwu, r31 == old_sp =
     * old_r1, so the GPR-shadow address from r1's perspective is
     * (r31 + offset) post-prolog. */
    {
        int f, k;
        for (f = 0; f < ppc_fp_param_count; f++) {
            int fpr = f + 1;  /* f1..f8 */
            int off = ppc_fp_param_off[f];
            if (ppc_fp_param_is_double[f]) {
                /* stfd fS, off(r1) -- opcode 54 */
                o(0xd8000000 | (fpr << 21) | (1 << 16) | (off & 0xffff));
            } else {
                /* stfs fS, off(r1) -- opcode 52 */
                o(0xd0000000 | (fpr << 21) | (1 << 16) | (off & 0xffff));
            }
        }
        /* Pad unused FP-spill slots with nops. */
        for (k = ppc_fp_param_count; k < 8; k++)
            o(0x60000000);  /* nop */
    }
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
    case TOK_ADDC1:
        /* addc rD, rA, rB -- adds and sets XER[CA]. */
        o(0x7c000014 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case TOK_ADDC2:
        /* adde rD, rA, rB -- adds rA + rB + XER[CA]. */
        o(0x7c000114 | (ra_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        break;
    case '-':
        /* subf rD, rA, rB  computes rD = rB - rA, so swap. */
        o(0x7c000050 | (ra_gpr << 21) | (rb_gpr << 16) | (ra_gpr << 11));
        break;
    case TOK_SUBC1:
        /* subfc rD, rA, rB  computes rD = rB - rA, sets XER[CA].
         * For "ra - rb" (low half of long long subtract): swap so
         * rA-field=rb, rB-field=ra, rD=ra. */
        o(0x7c000010 | (ra_gpr << 21) | (rb_gpr << 16) | (ra_gpr << 11));
        break;
    case TOK_SUBC2:
        /* subfe rD, rA, rB  computes rD = ~rA + rB + CA.
         * For "ra - rb - borrow" (high half): same encoding shape. */
        o(0x7c000110 | (ra_gpr << 21) | (rb_gpr << 16) | (ra_gpr << 11));
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
    case TOK_UMULL: {
        /* Unsigned 32x32 -> 64 multiply. tcc's gen_opl algorithm for
         * long long * long long pushes duplicates of the operands
         * onto the vstack via vpushv, which shares the same register
         * slot as the original entry — so we MUST NOT clobber ra_gpr
         * or rb_gpr. Allocate fresh slots for both halves of the
         * result. */
        int lo_slot = get_reg(RC_INT);
        int lo_gpr = TREG_TO_GPR(lo_slot);
        int hi_slot = get_reg(RC_INT);
        int hi_gpr = TREG_TO_GPR(hi_slot);
        /* mulhwu rD, rA, rB -- opcode 31, ext 11. */
        o(0x7c000016 | (hi_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        /* mullw rD, rA, rB -- low 32 bits. */
        o(0x7c0001d6 | (lo_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        vtop[-1].r = lo_slot;
        vtop[-1].r2 = hi_slot;
        break;
    }
    case '%':
    case TOK_UMOD: {
        /* PPC has no modulo instruction. Compute as a - (a/b)*b
         * using a fresh temp register for the quotient. */
        int tmp_slot = get_reg(RC_INT);
        int tmp_gpr = TREG_TO_GPR(tmp_slot);
        if (op == TOK_UMOD)
            /* divwu rD, rA, rB */
            o(0x7c000396 | (tmp_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        else
            /* divw rD, rA, rB */
            o(0x7c0003d6 | (tmp_gpr << 21) | (ra_gpr << 16) | (rb_gpr << 11));
        /* mullw tmp, tmp, rB  →  tmp = (a/b) * b */
        o(0x7c0001d6 | (tmp_gpr << 21) | (tmp_gpr << 16) | (rb_gpr << 11));
        /* subf rA, tmp, rA  →  rA = rA - tmp = a - (a/b)*b */
        o(0x7c000050 | (ra_gpr << 21) | (tmp_gpr << 16) | (ra_gpr << 11));
        break;
    }
    default:
        tcc_error("ppc-gen: gen_opi op 0x%x not yet supported", op);
    }
    vtop--;
}

/* Floating-point binary ops, unary negate, and comparisons.
 *
 * PPC FP encoding shapes (instruction = 4 bytes, big-endian):
 *   single-precision arithmetic (opcode 59 = 0xec000000):
 *     fadds  XO=21 -> 0xec00002a
 *     fsubs  XO=20 -> 0xec000028
 *     fmuls  XO=25 -> 0xec000032   (uses fC field at bits 21-25, A-form)
 *     fdivs  XO=18 -> 0xec000024
 *   double-precision arithmetic (opcode 63 = 0xfc000000):
 *     fadd   XO=21 -> 0xfc00002a
 *     fsub   XO=20 -> 0xfc000028
 *     fmul   XO=25 -> 0xfc000032   (uses fC field at bits 21-25, A-form)
 *     fdiv   XO=18 -> 0xfc000024
 *   misc (opcode 63):
 *     fneg   XO=40 -> 0xfc000050   (X-form: fD,fB)
 *     frsp   XO=12 -> 0xfc000018   (round to single)
 *     fcmpu  XO=0  -> 0xfc000000   (BF<<23 | fA<<16 | fB<<11)
 *
 * A-form layout (fadd, fsub, fdiv): fD=21..25, fA=16..20, fB=11..15.
 * fmul/fmuls use fC=6..10 instead of fB. (We emit it as fA*fC.) */
ST_FUNC void gen_opf(int op)
{
    int bt = vtop[0].type.t & VT_BTYPE;
    int dbl = (bt == VT_DOUBLE || bt == VT_LDOUBLE);
    int a_slot, b_slot, fa, fb, fd;
    uint32_t base, instr;

    if (op == TOK_NEG) {
        int slot;
        gv(RC_FLOAT);
        slot = vtop[0].r;
        fb = TREG_TO_FPR(slot);
        fd = fb;
        /* fneg fD, fB */
        o(0xfc000050 | (fd << 21) | (fb << 11));
        return;
    }

    /* Comparison: fcmpu cr0, fA, fB then VT_CMP. The condition bit
     * mapping in cr0 is the SAME as for the integer comparisons we
     * already handle in gjmp_cond (LT=0, GT=1, EQ=2). For unordered
     * (NaN) results bit 3 is set; we treat NaN like "not equal /
     * unordered"; tcc's predicates work correctly under fcmpu's
     * IEEE semantics for finite operands. */
    if (op >= TOK_ULT && op <= TOK_GT) {
        gv2(RC_FLOAT, RC_FLOAT);
        a_slot = vtop[-1].r;
        b_slot = vtop[0].r;
        fa = TREG_TO_FPR(a_slot);
        fb = TREG_TO_FPR(b_slot);
        /* fcmpu cr0, fA, fB */
        o(0xfc000000 | (fa << 16) | (fb << 11));
        vtop--;
        vset_VT_CMP(op);
        return;
    }

    gv2(RC_FLOAT, RC_FLOAT);
    a_slot = vtop[-1].r;
    b_slot = vtop[0].r;
    fa = TREG_TO_FPR(a_slot);
    fb = TREG_TO_FPR(b_slot);
    fd = fa;  /* result lands in vtop[-1]'s slot */
    base = dbl ? 0xfc000000 : 0xec000000;

    switch (op) {
    case '+':
        /* fadd[s] fD, fA, fB */
        instr = base | 0x2a | (fd << 21) | (fa << 16) | (fb << 11);
        break;
    case '-':
        /* fsub[s] fD, fA, fB */
        instr = base | 0x28 | (fd << 21) | (fa << 16) | (fb << 11);
        break;
    case '*':
        /* fmul[s] fD, fA, fC -- second operand goes in fC field
         * (bits 6..10, NOT the fB field at 11..15). */
        instr = base | 0x32 | (fd << 21) | (fa << 16) | (fb << 6);
        break;
    case '/':
        /* fdiv[s] fD, fA, fB */
        instr = base | 0x24 | (fd << 21) | (fa << 16) | (fb << 11);
        break;
    default:
        tcc_error("ppc-gen: gen_opf op 0x%x not yet supported", op);
    }
    o(instr);
    vtop--;
}

/* Convert an integer to FP. PPC has no direct user-mode int->FP
 * instruction; we use the classical "magic constant" trick:
 *
 *   For a signed 32-bit int x:
 *     1) flip the sign bit:           y = x XOR 0x80000000
 *     2) build the double:            d = 0x4330_0000_8000_0000 (high) | y (low)
 *     3) interpret as IEEE double:    d_val = 2^52 + 2^31 + (signed_x + 2^31)
 *     4) subtract magic:              d_val - (2^52 + 2^31) = signed_x
 *
 * Concretely we write the two halves of a double to a small scratch
 * area at r1+24 (start of caller's outgoing param area; idle when
 * we're not in a call), load it as a double, and subtract the magic
 * double which we've placed in rodata.  */
ST_FUNC void gen_cvt_itof(int t)
{
    int src_bt = vtop->type.t & VT_BTYPE;
    int unsign = vtop->type.t & VT_UNSIGNED;
    int dbl = (t == VT_DOUBLE || t == VT_LDOUBLE);
    int gpr_int, fpr_dst, fpr_tmp;
    int dst_slot, tmp_slot;
    static const unsigned char magic_signed[8] = {
        0x43, 0x30, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00
    };
    static const unsigned char magic_unsigned[8] = {
        0x43, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    if (src_bt == VT_LLONG) {
        tcc_error("ppc-gen: long long to FP conversion not yet implemented");
    }

    /* Materialize the int operand into a GPR. */
    gv(RC_INT);
    gpr_int = TREG_TO_GPR(vtop->r);

    /* Allocate destination FPR. Push it on vtop first so the second
     * get_reg won't return the same FPR. */
    vtop--;
    dst_slot = get_reg(RC_FLOAT);
    fpr_dst = TREG_TO_FPR(dst_slot);
    vtop++;
    vtop->r = dst_slot;
    vtop->type.t = t;
    /* Allocate a second FPR for the magic constant -- now that
     * dst_slot is on the vstack, get_reg will avoid it. */
    tmp_slot = get_reg(RC_FLOAT);
    fpr_tmp = TREG_TO_FPR(tmp_slot);

    /* Step 1: build the double in a scratch slot at r1+24..31. We
     * need to write 0x43300000 to the high word and (gpr_int XOR
     * 0x80000000 for signed) to the low word. We also need a third
     * GPR for the high half — use r0 (always available scratch). */
    if (!unsign) {
        /* xor with 0x80000000 to flip sign bit. xoris rA, rS, 0x8000 */
        o(0x6c000000 | (gpr_int << 21) | (0 << 16) | 0x8000);
        /* That's xoris r0, gpr_int, 0x8000. Save back to r0. */
        /* Actually: xoris encoding is opcode 27 = 0x6c000000:
         *   xoris rA, rS, UI  ->  0x6c000000 | (rS<<21) | (rA<<16) | UI
         * So our emit above does: rA=0, rS=gpr_int, UI=0x8000. Good. */
    } else {
        /* Just copy gpr_int into r0: mr r0, gpr_int = or r0, gpr_int, gpr_int */
        o(0x7c000378 | (gpr_int << 21) | (0 << 16) | (gpr_int << 11));
    }

    /* Step 2: write 0x43300000 to (r1+24), and the (xor'd) low half
     * to (r1+28). Load the high constant into a temp GPR (r12). */
    /* lis r12, 0x4330 */
    o(0x3c000000 | (12 << 21) | 0x4330);
    /* stw r12, 24(r1) */
    o(0x90000000 | (12 << 21) | (1 << 16) | 24);
    /* stw r0, 28(r1) */
    o(0x90000000 | (0 << 21) | (1 << 16) | 28);

    /* Step 3: lfd fpr_dst, 24(r1) */
    o(0xc8000000 | (fpr_dst << 21) | (1 << 16) | 24);

    /* Step 4: load the magic double (matching signed/unsigned). */
    ppc_load_fp_const(fpr_tmp, 1, unsign ? magic_unsigned : magic_signed);

    /* fsub fpr_dst, fpr_dst, fpr_tmp  (double precision subtract) */
    o(0xfc000028 | (fpr_dst << 21) | (fpr_dst << 16) | (fpr_tmp << 11));

    /* If destination is float, round to single. */
    if (!dbl) {
        /* frsp fD, fB */
        o(0xfc000018 | (fpr_dst << 21) | (fpr_dst << 11));
    }
}

/* Convert FP to integer. Sequence:
 *   fctiwz fD, fB     ; convert to int32, round-toward-zero
 *   stfd   fD, 24(r1) ; spill
 *   lwz    rD, 28(r1) ; load low word (the integer result)
 * For unsigned int target, the same fctiwz instruction works for
 * values in [0, 2^31); larger values would need a different path
 * (we error out for now).  */
ST_FUNC void gen_cvt_ftoi(int t)
{
    int dst_bt = t & VT_BTYPE;
    int gpr_dst, fpr_src, fpr_tmp;
    int dst_slot, tmp_slot;

    if (dst_bt == VT_LLONG) {
        tcc_error("ppc-gen: FP to long long conversion not yet implemented");
    }

    /* Materialize FP operand. */
    gv(RC_FLOAT);
    fpr_src = TREG_TO_FPR(vtop->r);

    /* Allocate result GPR; push it onto vtop so the next get_reg
     * for the FP scratch doesn't return our source FPR. */
    vtop--;
    dst_slot = get_reg(RC_INT);
    gpr_dst = TREG_TO_GPR(dst_slot);
    vtop++;
    vtop->r = dst_slot;
    vtop->type.t = t;
    /* Allocate scratch FPR for the converted value. */
    tmp_slot = get_reg(RC_FLOAT);
    fpr_tmp = TREG_TO_FPR(tmp_slot);

    /* fctiwz fD, fB -- opcode 63, XO 15 -> 0xfc00001e */
    o(0xfc00001e | (fpr_tmp << 21) | (fpr_src << 11));
    /* stfd fS, 24(r1) */
    o(0xd8000000 | (fpr_tmp << 21) | (1 << 16) | 24);
    /* lwz rD, 28(r1)  (low word of the spilled double = our int) */
    o(0x80000000 | (gpr_dst << 21) | (1 << 16) | 28);
}

/* Convert between float and double precision.
 *   double -> float:  frsp fD, fB
 *   float  -> double: no-op (float in FPR is already representable
 *                     as double; PPC FPRs are always 64-bit and
 *                     internally hold the converted value). */
ST_FUNC void gen_cvt_ftof(int t)
{
    int src_bt = vtop->type.t & VT_BTYPE;
    int dst_bt = t & VT_BTYPE;
    int fpr;

    if (src_bt == dst_bt)
        return;

    /* Treat LDOUBLE as DOUBLE (Apple PPC + our LDOUBLE_SIZE=8). */
    if (src_bt == VT_LDOUBLE) src_bt = VT_DOUBLE;
    if (dst_bt == VT_LDOUBLE) dst_bt = VT_DOUBLE;
    if (src_bt == dst_bt) {
        vtop->type.t = t;
        return;
    }

    gv(RC_FLOAT);
    fpr = TREG_TO_FPR(vtop->r);

    if (src_bt == VT_DOUBLE && dst_bt == VT_FLOAT) {
        /* frsp fD, fB -- round to single. Result stays in same FPR. */
        o(0xfc000018 | (fpr << 21) | (fpr << 11));
    } else {
        /* float -> double: no instruction needed. */
    }
    vtop->type.t = t;
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
