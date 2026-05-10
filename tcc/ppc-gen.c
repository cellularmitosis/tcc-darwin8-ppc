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
 * and 13 float regs (f1-f13 -- the full Apple PPC32 ABI variadic
 * FP arg slot range). Total = 23.
 *
 * Future work: expose non-volatile registers for spill avoidance.
 */
#define NB_REGS         23

/* Map TCC's TREG_* slot to a PPC register number. */
#define TREG_R(x)       (x)             /* x = 0..7  -> r3..r10  */
#define TREG_R11        8               /* scratch */
#define TREG_R12        9               /* scratch */
#define TREG_F(x)       ((x) + 10)      /* x = 0..12 -> f1..f13  */

/* Translate a TREG slot back to the actual PPC GPR number (3..12). */
#define TREG_TO_GPR(t)  ((t) < 8 ? (t) + 3 : (t) == 8 ? 11 : 12)
/* Translate a TREG slot back to the actual PPC FPR number (1..13). */
#define TREG_TO_FPR(t)  ((t) - 10 + 1)

/* Register-class bitmasks. Bit 0 = generic int, bit 1 = generic float;
 * bits 2..11 are individual int regs, bits 12..24 individual floats. */
#define RC_INT          (1 << 0)
#define RC_FLOAT        (1 << 1)
#define RC_R(x)         (1 << (2 + (x)))        /* x = 0..9 */
#define RC_F(x)         (1 << (12 + (x)))       /* x = 0..12 */

#define RC_R3           RC_R(0)
#define RC_R4           RC_R(1)
#define RC_F1           RC_F(0)
#define RC_F2           RC_F(1)

/* Function-call return register classes & numbers. */
#define RC_IRET         RC_R3           /* int return register class */
#define RC_IRE2         RC_R4           /* second word of long long return */
#define RC_FRET         RC_F1           /* float return register class */
#define RC_FRE2         RC_F2           /* second FPR of long-double return */

#define REG_IRET        TREG_R(0)       /* r3 */
#define REG_IRE2        TREG_R(1)       /* r4, used for long long */
#define REG_FRET        TREG_F(0)       /* f1 */
#define REG_FRE2        TREG_F(1)       /* f2, used for long-double low */

#define PTR_SIZE        4

/* Apple PPC32 ABI: `long double` is 128-bit IBM double-double — a
 * pair of doubles representing (high, low) where the value is
 * high+low. Natural alignment is 4 bytes per Apple's ABI doc.
 * Passed in two consecutive FPRs (f{n}, f{n+1}); returned in
 * (f1, f2). Arithmetic is delegated to libgcc helpers
 * (__gcc_qadd/qsub/qmul/qdiv); see tcctok.h and lib-ppc.c. */
#define LDOUBLE_SIZE    16
#define LDOUBLE_ALIGN   4

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
 * predefined macros, plus Apple's.
 *
 * The __*_MANT_DIG__ pair is critical on Apple PPC: Tiger's
 * <sys/cdefs.h> uses `__LDBL_MANT_DIG__ > __DBL_MANT_DIG__` to
 * decide whether to redirect printf/scanf/etc. to the
 * `*$LDBLStub` symbols (the LDBL-128 ABI). Without these
 * defines the cdefs check evaluates `0 > 0` and emits no
 * redirect, so printf("%Lf", ld) routes to the legacy 8-byte
 * LDBL printf and reads only half of our 16-byte LD value. */
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
    "__DBL_MANT_DIG__ 53\0"
    "__LDBL_MANT_DIG__ 106\0"
    /* Set minimum-OSX-version high enough that <sys/cdefs.h>'s
     * __DARWIN_LDBL_COMPAT macro emits direct $LDBL128 references
     * (in libSystem on Tiger 10.4+) rather than $LDBLStub (which
     * gcc resolves via libgcc-provided runtime-lookup stubs that
     * we don't ship). */
    "__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ 1040\0"
    /* And the math.h analog: <architecture/ppc/math.h>'s
     * __LIBMLDBL_COMPAT macro emits $LDBL128 only when
     * __APPLE_CC__ && __LONG_DOUBLE_128__ are defined. Without
     * the redirect, math functions (ldexpl, expl, sinl, etc.) go
     * to the legacy 8-byte-LD libm entry which silently treats
     * the LD as double. Tiger libm has the LDBL128 entries
     * bundled in libSystem. (__APPLE_CC__ is already defined to
     * 1 in tccdefs.h for TargetConditionals.h compatibility.) */
    "__LONG_DOUBLE_128__ 1\0"
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
    /* 10..22: f1..f13 */
    RC_FLOAT | RC_F(0),
    RC_FLOAT | RC_F(1),
    RC_FLOAT | RC_F(2),
    RC_FLOAT | RC_F(3),
    RC_FLOAT | RC_F(4),
    RC_FLOAT | RC_F(5),
    RC_FLOAT | RC_F(6),
    RC_FLOAT | RC_F(7),
    RC_FLOAT | RC_F(8),
    RC_FLOAT | RC_F(9),
    RC_FLOAT | RC_F(10),
    RC_FLOAT | RC_F(11),
    RC_FLOAT | RC_F(12),
};

/* Bounds-check support is deferred. tccgen.c references
 * func_bound_add_epilog unconditionally (not gated by
 * CONFIG_TCC_BCHECK), so its storage must always exist. The other
 * two are only used by future bcheck-aware codegen routines. */
ST_DATA int func_bound_add_epilog;

/* Most-recent function's frame size, set by gfunc_epilog. tccdbg.c
 * reads this in tcc_debug_frame_end's PPC block to emit per-FDE
 * DW_CFA_def_cfa_offset describing the post-prolog CFA position
 * (CFA = r1 + frame_size). Declared in tcc.h with ST_DATA so the
 * include order works under both ONE_SOURCE (file-scope static)
 * and split-build (extern + this definition). */
ST_DATA int ppc_last_frame_size;
/* Tcc's PPC prolog has a constant layout. The stwu instruction
 * (where the CFA changes from r1+0 to r1+frame_size) sits at:
 *   mflr r0           4
 *   stw r0, 8(r1)     4
 *   8x stw rN spill  32
 *   13x FP/nop spill 52
 *   ------------------
 *   stwu offset      92
 * 4-byte stwu itself, so CFA advances kick in at +96 from func
 * start. Lives as a literal inside tccdbg.c's PPC FDE block. */
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
/* Prologue size in bytes. Worst case: 14 instructions for the
 * short-frame path (mflr, stw LR, 8 arg spills, stwu, stw r31,
 * stw r30, addi r31) PLUS up to 8 extra instructions of nop padding
 * when we use the long-frame path for any of the four
 * frame-relative ops (stwu/stw r31/stw r30/addi r31, each blowing
 * up from 1 to 3 instructions when frame_size or the saved-reg
 * offsets won't fit in a 16-bit signed immediate). 14 + 8 = 22
 * instructions = 88 bytes. */
#define PPC_PROLOG_SIZE 88
#define PPC_LINKAGE_SIZE 24
#define PPC_PARAM_AREA_MIN 96   /* default outgoing param area: 24 GPR
                                  slots, big enough for variadic
                                  printf and most plain calls */
#define PPC_STACK_ALIGN 16
#define PPC_FP_REG 31

/* Per-function high-water mark for the outgoing parameter area.
 * Reset to PPC_PARAM_AREA_MIN in gfunc_prolog; bumped by gfunc_call
 * when an arg list (especially one carrying a large struct value)
 * needs more bytes; consulted by ppc_frame_size() at gfunc_epilog
 * time. The struct-by-value support (and >8-arg calls in general)
 * means 96 bytes isn't always enough. */
static int ppc_param_area;

static int ppc_frame_size(void)
{
    /* Frame layout (NEW_SP at bottom, OLD_SP at top):
     *   NEW_SP+0..23      linkage area
     *   NEW_SP+24..119    outgoing parameter area (24 GPR slots,
     *                      enough for 24 int args or 12 LL args
     *                      via direct stack passing for slot >= 8;
     *                      slots 0..7 still go via r3..r10).
     *   NEW_SP+120..      gap, then locals growing upward to:
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
    int n = PPC_LINKAGE_SIZE + ppc_param_area + (-loc);
    n = (n + PPC_STACK_ALIGN - 1) & -PPC_STACK_ALIGN;
    if (n < 64) n = 64;
    return n;
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* Forward declarations for the PIC-indirection helpers (defined later
 * in this file, after the gfunc_prolog/epilog block). */
static int  ppc_need_pic_for_sym(Sym *sym);
static void ppc_emit_pic_addr_load(int addr_gpr, Sym *sym);

/* True if `offset` fits in the signed 16-bit immediate of a D-form
 * load/store. False means we need the lis/ori + X-form indexed
 * sequence. */
static inline int ppc_off_fits_d(int offset)
{
    return (int16_t)offset == offset;
}

/* Adjust `addr_gpr` so that the remaining displacement fits in a
 * signed 16-bit immediate. If `extra_off` already fits, returns it
 * unchanged. Otherwise emits `addis addr_gpr, addr_gpr, ha16(extra_off)`
 * and returns the lo16 portion (signed 16-bit, may be negative).
 *
 * Used by the PIC load/store paths: when computing `head[n]` via the
 * #define `head (prev+WSIZE)` pattern, the byte offset from prev is
 * WSIZE * sizeof(Pos) = 0x10000, which doesn't fit in the D-form
 * immediate. Without this adjustment, `extra_off & 0xffff` truncates
 * to 0 and writes/reads land at prev[n] instead of head[n].
 *
 * Surfaced by gzip 1.11's fill_window: the slide path's
 * `head[n] = (m >= WSIZE ? m-WSIZE : NIL)` updated prev[] (twice!)
 * and never touched head[], corrupting the hash chains and producing
 * gzip output with CRC errors at >= 64KB inputs. */
static inline int ppc_adjust_extra_off(int addr_gpr, int extra_off)
{
    int ha;
    if (ppc_off_fits_d(extra_off))
        return extra_off;
    ha = (((unsigned)extra_off + 0x8000u) >> 16) & 0xffff;
    /* addis addr_gpr, addr_gpr, ha16 */
    o(0x3c000000 | (addr_gpr << 21) | (addr_gpr << 16) | ha);
    return (int16_t)(extra_off & 0xffff);
}

/* Materialize a 32-bit value into r0 via lis + ori. The pair
 * faithfully reconstructs the value (ori does not sign-extend), so
 * no +0x8000 fix-up is needed. */
static void ppc_li32_r0(int value)
{
    int hi = ((unsigned)value >> 16) & 0xffff;
    int lo = (unsigned)value & 0xffff;
    o(0x3c000000 | (0 << 21) | hi);              /* lis r0, hi */
    o(0x60000000 | (0 << 21) | (0 << 16) | lo);  /* ori r0, r0, lo */
}

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
    /* In DLL mode, the literal's absolute VA isn't slide-invariant.
     * Use the PIC sectdiff path: addis/lwz against r30 (anchor),
     * which the macho writer rewrites into addis/addi giving the
     * literal's runtime VA in r12. Then lfs/lfd from there.
     *
     * For OBJ/EXE/MEMORY, keep the direct lis/lfs idiom — simpler
     * and avoids the PIC base setup overhead. */
    if (ppc_need_pic_for_sym(sym)) {
        ppc_emit_pic_addr_load(12, sym);
        if (is_double)
            o(0xc8000000 | (fpr << 21) | (12 << 16));   /* lfd fD, 0(r12) */
        else
            o(0xc0000000 | (fpr << 21) | (12 << 16));   /* lfs fD, 0(r12) */
        return;
    }
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

    /* VT_JMP / VT_JMPI: value is the consequence of a forward jump
     * chain. Materialize as 0 or 1 in `r`. Pattern (matches i386):
     *
     *     li   rD, t
     *     b    $+8                  ; skip past the chain target
     *  patch_chain_to_here:
     *     li   rD, t ^ 1
     *
     * For VT_JMP  (v=0x34, t=0): if no jump fires, rD=0; if a jump
     *                            in the chain fires, rD=1.
     * For VT_JMPI (v=0x35, t=1): inverted.
     */
    if (v == VT_JMP || v == VT_JMPI) {
        int t = v & 1;
        int dst_gpr;
        if (IS_FREG(r))
            tcc_error("ppc-gen: VT_JMP into FP register?");
        dst_gpr = TREG_TO_GPR(r);
        /* li rD, t */
        o(0x38000000 | (dst_gpr << 21) | (t & 0xffff));
        /* b $+8 */
        o(0x48000008);
        /* Patch the chain to land here. */
        gsym((int)sv->c.i);
        /* li rD, t ^ 1 */
        o(0x38000000 | (dst_gpr << 21) | ((t ^ 1) & 0xffff));
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

    /* VT_LLOCAL: an indirect local. The local at FP+c.i holds a
     * POINTER to the actual value; we need to load that pointer
     * first, then deref through it. Mirrors i386-gen.c:316 pattern.
     *
     * Used by tcc for VLAs and for some struct-by-pointer paths. */
    if (v == VT_LLOCAL && (sv->r & VT_LVAL)) {
        SValue v1;
        int tmp = r;
        v1.type.t = VT_PTR;
        v1.r = VT_LOCAL | VT_LVAL;
        v1.c.i = sv->c.i;
        v1.sym = NULL;
        v1.r2 = VT_CONST;
        /* Load the pointer into a GPR (use r itself if it's int and
         * not float; otherwise allocate a temp). */
        if (IS_FREG(tmp))
            tmp = get_reg(RC_INT);
        load(tmp, &v1);
        /* Now (tmp) holds the address. Synthesize a "deref via reg"
         * SValue and load again. */
        v1.type = sv->type;
        v1.r = tmp | VT_LVAL;
        v1.c.i = 0;
        v1.sym = NULL;
        v1.r2 = VT_CONST;
        load(r, &v1);
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
        if (ppc_off_fits_d(offset)) {
            /* addi rD, r31, offset */
            o(0x38000000 | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
        } else {
            /* lis r0, hi(offset); ori r0, r0, lo(offset); add rD, r31, r0 */
            ppc_li32_r0(offset);
            o(0x7c000214 | (gpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
        }
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
        case VT_VOID:
            /* `*p` of `void *` is a constraint violation but tcc
             * accepts it, used like `i ? *p : *p;` for side-effect
             * sequencing tests (dr106). The value is undefined and
             * never consumed; emit nothing. */
            return;
        case VT_BOOL:
            o(0x88000000 | (gpr << 21) | (base_gpr << 16));
            return;
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
        case VT_STRUCT:
            /* "Loading" a struct lvalue whose address sits in a
             * register means producing that address — struct values
             * flow through tcc as pointers (gfunc_call/vstore etc.
             * read the bytes from the address). Just `mr r, base`,
             * skipping the no-op when r already is base. Mirrors the
             * VT_LOCAL+VT_LVAL+VT_STRUCT case above. */
            if (gpr != base_gpr)
                o(0x7c000378 | (base_gpr << 21) | (gpr << 16) | (base_gpr << 11));
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
        int need_pic = ppc_need_pic_for_sym(sv->sym);

        /* PIC path: external data symbol. Two-step indirection:
         *   addr_gpr = *(__nl_symbol_ptr_for_sym)
         *   (then load/use as below).
         * The address-load uses addis+lwz against r2 (the per-function
         * PIC base anchor). We stash the result in a scratch GPR or
         * directly in the destination if want_load is false. */
        if (need_pic) {
            int dst_gpr;

            /* Pick the scratch GPR that will hold the symbol's address.
             * If we're computing an address and the destination is a
             * GPR (not FP) with no extra offset, use r itself. Otherwise
             * use r12 as a scratch -- it's not in tcc's reg allocator
             * and is caller-saved on Darwin. */
            if (!want_load && !IS_FREG(r) && extra_off == 0) {
                addr_gpr = TREG_TO_GPR(r);
            } else {
                addr_gpr = 12;
            }
            ppc_emit_pic_addr_load(addr_gpr, sv->sym);
            /* Now addr_gpr holds the *address* of the symbol. Fold
             * any high bits of extra_off into addr_gpr via addis so
             * the remaining displacement fits in the D-form
             * immediate (signed 16-bit). */
            extra_off = ppc_adjust_extra_off(addr_gpr, extra_off);

            if (!want_load) {
                /* Address-of (with optional addend). */
                dst_gpr = TREG_TO_GPR(r);
                if (extra_off != 0) {
                    /* addi dst, addr_gpr, extra_off */
                    o(0x38000000 | (dst_gpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                } else if (dst_gpr != addr_gpr) {
                    /* mr dst, addr_gpr -- shouldn't normally happen
                     * because we picked addr_gpr = dst_gpr above. */
                    o(0x7c000378 | (addr_gpr << 21) | (dst_gpr << 16)
                                 | (addr_gpr << 11));
                }
                return;
            }

            /* Typed load through addr_gpr + extra_off. */
            if (IS_FREG(r)) {
                int fpr = TREG_TO_FPR(r);
                if (bt == VT_FLOAT) {
                    o(0xc0000000 | (fpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                    o(0xc8000000 | (fpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                } else {
                    tcc_error("ppc-gen: PIC FP load of bt 0x%x", bt);
                }
                return;
            }
            gpr = TREG_TO_GPR(r);
            switch (bt) {
            case VT_BOOL:
                o(0x88000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                return;
            case VT_BYTE:
                o(0x88000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                if (!(sv->type.t & VT_UNSIGNED))
                    o(0x7c000774 | (gpr << 21) | (gpr << 16));  /* extsb */
                return;
            case VT_SHORT:
                if (sv->type.t & VT_UNSIGNED)
                    o(0xa0000000 | (gpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                else
                    o(0xa8000000 | (gpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                return;
            case VT_INT:
            case VT_PTR:
            case VT_FUNC:
                o(0x80000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                return;
            case VT_LLONG:
                /* High then low (big-endian word order). */
                o(0x80000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                return;
            default:
                tcc_error("ppc-gen: PIC load via sym of bt 0x%x not yet supported", bt);
            }
            return;
        }

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
            /* addi rD, addr_gpr, lo16(sym) -- this gives &sym. If
             * extra_off != 0 (e.g. &g.b for struct g), add it with a
             * second addi; we can't fold into the lo16 immediate
             * because the relocation overwrites it. */
            int dst_gpr = TREG_TO_GPR(r);
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(0x38000000 | (dst_gpr << 21) | (addr_gpr << 16));
            if (extra_off != 0) {
                /* Fold high bits via addis if extra_off > 15-bit signed. */
                extra_off = ppc_adjust_extra_off(dst_gpr, extra_off);
                /* addi dst_gpr, dst_gpr, extra_off */
                o(0x38000000 | (dst_gpr << 21) | (dst_gpr << 16)
                             | (extra_off & 0xffff));
            }
            return;
        }
        /* Otherwise, emit the typed load.
         *
         * If extra_off != 0 (struct member, array element, etc.), we
         * must NOT bake it into the lo16(sym) immediate -- the linker
         * overwrites that 16-bit field with lo16(sym) at link time and
         * any addend stored there is lost. Instead, materialize the
         * full symbol address into addr_gpr with `addi addr_gpr,
         * addr_gpr, lo16(sym)`, then do the typed load with extra_off
         * as the displacement. This costs one extra instruction per
         * non-zero-offset access, which we accept for correctness. */
        if (extra_off != 0) {
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            /* addi addr_gpr, addr_gpr, lo16(sym) */
            o(0x38000000 | (addr_gpr << 21) | (addr_gpr << 16));
            /* Fold any high bits of extra_off into addr_gpr via addis. */
            extra_off = ppc_adjust_extra_off(addr_gpr, extra_off);
            if (IS_FREG(r)) {
                int fpr = TREG_TO_FPR(r);
                if (bt == VT_FLOAT) {
                    o(0xc0000000 | (fpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                    o(0xc8000000 | (fpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                } else {
                    tcc_error("ppc-gen: FP load via sym+off of bt 0x%x", bt);
                }
                return;
            }
            gpr = TREG_TO_GPR(r);
            switch (bt) {
            case VT_BOOL:
                o(0x88000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                return;
            case VT_BYTE:
                o(0x88000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                if (!(sv->type.t & VT_UNSIGNED))
                    o(0x7c000774 | (gpr << 21) | (gpr << 16));
                return;
            case VT_SHORT:
                if (sv->type.t & VT_UNSIGNED)
                    o(0xa0000000 | (gpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                else
                    o(0xa8000000 | (gpr << 21) | (addr_gpr << 16)
                                 | (extra_off & 0xffff));
                return;
            case VT_INT:
            case VT_PTR:
            case VT_FUNC:
                o(0x80000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                return;
            case VT_STRUCT:
                /* "Load" of a struct value = compute its address.
                 * addr_gpr already holds &sym after the addi at
                 * line 826-829; add extra_off to reach the array
                 * element / member. Mirrors the VT_STRUCT case in
                 * the extra_off==0 path below. */
                o(0x38000000 | (gpr << 21) | (addr_gpr << 16)
                             | (extra_off & 0xffff));
                return;
            default:
                tcc_error("ppc-gen: load via sym+off of bt 0x%x not yet supported", bt);
            }
        }

        /* extra_off == 0: original two-instruction lis+lwz path. */
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
            return;
        }
        gpr = TREG_TO_GPR(r);
        switch (bt) {
        case VT_BOOL:
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(0x88000000 | (gpr << 21) | (addr_gpr << 16));
            return;
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
        case VT_STRUCT:
            /* "Loading" a struct value via a symbol means computing
             * its address. addr_gpr already holds the symbol's HA-side
             * address; just turn `lwz r, lo(sym)(addr_gpr)` into
             * `addi r, addr_gpr, lo(sym)`. */
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(0x38000000 | (gpr << 21) | (addr_gpr << 16));
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
            uint32_t d_op = 0, x_op = 0;
            if (bt == VT_FLOAT) {
                d_op = 0xc0000000;             /* lfs */
                x_op = 0x7c00042e;             /* lfsx */
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                d_op = 0xc8000000;             /* lfd */
                x_op = 0x7c0004ae;             /* lfdx */
            } else {
                tcc_error("ppc-gen: FP local load of bt 0x%x", bt);
            }
            if (ppc_off_fits_d(offset)) {
                o(d_op | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            } else {
                ppc_li32_r0(offset);
                o(x_op | (fpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
            }
            return;
        }
        gpr = TREG_TO_GPR(r);
        {
            uint32_t d_op = 0, x_op = 0;
            int sign_ext = 0;
            switch (bt) {
            case VT_BOOL:
                d_op = 0x88000000;             /* lbz */
                x_op = 0x7c0000ae;             /* lbzx */
                break;
            case VT_BYTE:
                d_op = 0x88000000;
                x_op = 0x7c0000ae;
                if (!(sv->type.t & VT_UNSIGNED)) sign_ext = 1;
                break;
            case VT_SHORT:
                if (sv->type.t & VT_UNSIGNED) {
                    d_op = 0xa0000000;         /* lhz */
                    x_op = 0x7c00022e;         /* lhzx */
                } else {
                    d_op = 0xa8000000;         /* lha */
                    x_op = 0x7c0002ae;         /* lhax */
                }
                break;
            case VT_INT:
            case VT_PTR:
            case VT_FUNC:
            case VT_LLONG:
                d_op = 0x80000000;             /* lwz */
                x_op = 0x7c00002e;             /* lwzx */
                break;
            case VT_STRUCT:
                /* "Loading" a struct lvalue means producing its
                 * address. Struct values flow through tcc as pointers
                 * — gfunc_call etc. read the bytes from that address.
                 * addi for short, lis/ori/add for long. */
                if (ppc_off_fits_d(offset)) {
                    o(0x38000000 | (gpr << 21) | (PPC_FP_REG << 16)
                                 | (offset & 0xffff));
                } else {
                    ppc_li32_r0(offset);
                    /* add r, r31, r0 */
                    o(0x7c000214 | (gpr << 21) | (PPC_FP_REG << 16)
                                 | (0 << 11));
                }
                return;
            default:
                tcc_error("ppc-gen: load VT_LOCAL of basic type 0x%x not yet supported", bt);
            }
            if (ppc_off_fits_d(offset)) {
                o(d_op | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            } else {
                ppc_li32_r0(offset);
                o(x_op | (gpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
            }
            if (sign_ext)
                o(0x7c000774 | (gpr << 21) | (gpr << 16));  /* extsb */
            return;
        }
    }

    /* Load from an absolute address (e.g. `*(int *)0x20000000`):
     * VT_CONST | VT_LVAL with no VT_SYM. Materialize the HA half of
     * the address into r12 (NOT r0 — D-form load/store treats RA=0
     * as the literal value 0, not the contents of r0), then do a
     * D-form load via r12+lo. The +0x8000 trick on `hi` pairs with
     * addi-style sign-extending `lo` in the load instruction. */
    if (v == VT_CONST && (sv->r & VT_LVAL) && !(sv->r & VT_SYM)) {
        int bt = sv->type.t & VT_BTYPE;
        uint32_t addr = (uint32_t)sv->c.i;
        int hi = (((int)addr + 0x8000) >> 16) & 0xffff;
        int lo = addr & 0xffff;
        gpr = TREG_TO_GPR(r);
        /* lis r12, ha(addr) */
        o(0x3c000000 | (12 << 21) | hi);
        switch (bt) {
        case VT_BOOL:
        case VT_BYTE:
            o(0x88000000 | (gpr << 21) | (12 << 16) | (lo & 0xffff));
            if (bt == VT_BYTE && !(sv->type.t & VT_UNSIGNED))
                o(0x7c000774 | (gpr << 21) | (gpr << 16));
            return;
        case VT_SHORT:
            if (sv->type.t & VT_UNSIGNED)
                o(0xa0000000 | (gpr << 21) | (12 << 16) | (lo & 0xffff));
            else
                o(0xa8000000 | (gpr << 21) | (12 << 16) | (lo & 0xffff));
            return;
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:
            o(0x80000000 | (gpr << 21) | (12 << 16) | (lo & 0xffff));
            return;
        default:
            tcc_error("ppc-gen: load from absolute addr of bt 0x%x", bt);
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
        case VT_BOOL:
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
            uint32_t d_op = 0, x_op = 0;
            if (bt == VT_FLOAT) {
                d_op = 0xd0000000;             /* stfs */
                x_op = 0x7c00052e;             /* stfsx */
            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                d_op = 0xd8000000;             /* stfd */
                x_op = 0x7c0005ae;             /* stfdx */
            } else {
                tcc_error("ppc-gen: store FP local of bt 0x%x", bt);
            }
            if (ppc_off_fits_d(offset)) {
                o(d_op | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            } else {
                ppc_li32_r0(offset);
                o(x_op | (fpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
            }
            return;
        }
        gpr = TREG_TO_GPR(r);
        {
            uint32_t d_op = 0, x_op = 0;
            switch (bt) {
            case VT_BOOL:
            case VT_BYTE:
                d_op = 0x98000000;             /* stb */
                x_op = 0x7c0001ae;             /* stbx */
                break;
            case VT_SHORT:
                d_op = 0xb0000000;             /* sth */
                x_op = 0x7c00032e;             /* sthx */
                break;
            case VT_INT:
            case VT_PTR:
            case VT_FUNC:
                d_op = 0x90000000;             /* stw */
                x_op = 0x7c00012e;             /* stwx */
                break;
            case VT_LLONG: {
                int hi_gpr;
                d_op = 0x90000000;
                x_op = 0x7c00012e;
                if (sv->r2 < VT_CONST) {
                    hi_gpr = TREG_TO_GPR(sv->r2);
                    /* stw high, offset(r31) */
                    if (ppc_off_fits_d(offset)) {
                        o(d_op | (hi_gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
                    } else {
                        ppc_li32_r0(offset);
                        o(x_op | (hi_gpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
                    }
                    /* stw low, offset+4(r31) */
                    if (ppc_off_fits_d(offset + 4)) {
                        o(d_op | (gpr << 21) | (PPC_FP_REG << 16) | ((offset + 4) & 0xffff));
                    } else {
                        ppc_li32_r0(offset + 4);
                        o(x_op | (gpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
                    }
                } else {
                    if (ppc_off_fits_d(offset)) {
                        o(d_op | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
                    } else {
                        ppc_li32_r0(offset);
                        o(x_op | (gpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
                    }
                }
                return;
            }
            default:
                tcc_error("ppc-gen: store VT_LOCAL of bt 0x%x not yet supported", bt);
            }
            if (ppc_off_fits_d(offset)) {
                o(d_op | (gpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));
            } else {
                ppc_li32_r0(offset);
                o(x_op | (gpr << 21) | (PPC_FP_REG << 16) | (0 << 11));
            }
            return;
        }
    }

    /* Store to a global variable (or a relocatable symbol):
     * VT_CONST + VT_SYM, with or without VT_LVAL (tcc treats the
     * value as an lvalue when storing). Emit:
     *   lis  rTMP, sym@ha          (R_PPC_ADDR16_HA on the lis)
     *   stw  rS,   sym@lo(rTMP)    (R_PPC_ADDR16_LO on the stw)
     */
    if (v == VT_CONST && (sv->r & VT_SYM)) {
        int tmp_slot;
        int tmp_gpr;
        int store_op;
        int64_t addend = sv->c.i;
        int need_pic = ppc_need_pic_for_sym(sv->sym);

        /* PIC path: external data symbol. Get the address into a
         * scratch GPR via __nl_symbol_ptr, then store through it. */
        if (need_pic) {
            tmp_gpr = 12;     /* scratch (not in tcc allocator) */
            ppc_emit_pic_addr_load(tmp_gpr, sv->sym);
            /* Fold any high bits of addend into tmp_gpr via addis so
             * the remaining displacement fits the D-form immediate. */
            addend = ppc_adjust_extra_off(tmp_gpr, (int)addend);
            /* Now tmp_gpr holds the address of the symbol. Store
             * through it with addend as the byte offset. */
            if (IS_FREG(r)) {
                int fpr = TREG_TO_FPR(r);
                if (bt == VT_FLOAT)
                    store_op = 0xd0000000;       /* stfs */
                else if (bt == VT_DOUBLE || bt == VT_LDOUBLE)
                    store_op = 0xd8000000;       /* stfd */
                else {
                    tcc_error("ppc-gen: PIC store global FP of bt 0x%x", bt);
                    return;
                }
                o(store_op | (fpr << 21) | (tmp_gpr << 16) | (((int32_t)addend) & 0xffff));
                return;
            }
            gpr = TREG_TO_GPR(r);
            switch (bt) {
            case VT_BOOL:
            case VT_BYTE:  store_op = 0x98000000; break;
            case VT_SHORT: store_op = 0xb0000000; break;
            case VT_INT:
            case VT_PTR:
            case VT_FUNC:  store_op = 0x90000000; break;
            default:
                /* VT_LLONG/VT_LDOUBLE never reach here: vstore() rewrites
                 * the type to VT_INT/VT_DOUBLE and emits two single-word
                 * stores before calling store(). */
                tcc_error("ppc-gen: PIC store global of bt 0x%x not yet supported", bt);
                return;
            }
            o(store_op | (gpr << 21) | (tmp_gpr << 16) | (((int32_t)addend) & 0xffff));
            return;
        }

        tmp_slot = get_reg(RC_INT);
        tmp_gpr = TREG_TO_GPR(tmp_slot);
        /* lis rTMP, sym@ha  -- placeholder; relocator fills the half.
         * The lo16 immediate of `lis` is irrelevant (lis ignores low
         * 16 of the result anyway); leave it 0. */
        greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_HA);
        o(0x3c000000 | (tmp_gpr << 21));
        /* If addend != 0 (struct member, array index, etc.), we cannot
         * fold it into the lo16(sym) immediate of the store -- the
         * relocator overwrites that 16-bit field with lo16(sym). So
         * materialize the full sym address into tmp_gpr first via
         * `addi tmp_gpr, tmp_gpr, lo16(sym)`, then do the store with
         * addend as the displacement. */
        if (addend != 0) {
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(0x38000000 | (tmp_gpr << 21) | (tmp_gpr << 16));
            /* Fold any high bits of addend into tmp_gpr via addis. */
            addend = ppc_adjust_extra_off(tmp_gpr, (int)addend);
            if (IS_FREG(r)) {
                int fpr = TREG_TO_FPR(r);
                if (bt == VT_FLOAT)
                    store_op = 0xd0000000;
                else if (bt == VT_DOUBLE || bt == VT_LDOUBLE)
                    store_op = 0xd8000000;
                else
                    tcc_error("ppc-gen: store global+off FP of bt 0x%x", bt);
                o(store_op | (fpr << 21) | (tmp_gpr << 16)
                           | (((int32_t)addend) & 0xffff));
                return;
            }
            gpr = TREG_TO_GPR(r);
            switch (bt) {
            case VT_BOOL:
            case VT_BYTE:  store_op = 0x98000000; break;
            case VT_SHORT: store_op = 0xb0000000; break;
            case VT_INT:
            case VT_PTR:
            case VT_FUNC:  store_op = 0x90000000; break;
            default:
                /* VT_LLONG/VT_LDOUBLE never reach here — see comment in
                 * the PIC branch above. */
                tcc_error("ppc-gen: store global+off of bt 0x%x not yet supported", bt);
            }
            o(store_op | (gpr << 21) | (tmp_gpr << 16)
                       | (((int32_t)addend) & 0xffff));
            return;
        }

        /* addend == 0: original lis+store path with lo16(sym) as disp. */
        if (IS_FREG(r)) {
            int fpr = TREG_TO_FPR(r);
            if (bt == VT_FLOAT)
                store_op = 0xd0000000;       /* stfs */
            else if (bt == VT_DOUBLE || bt == VT_LDOUBLE)
                store_op = 0xd8000000;       /* stfd */
            else
                tcc_error("ppc-gen: store global FP of bt 0x%x", bt);
            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
            o(store_op | (fpr << 21) | (tmp_gpr << 16));
            return;
        }
        gpr = TREG_TO_GPR(r);
        switch (bt) {
        case VT_BOOL:
        case VT_BYTE:  store_op = 0x98000000; break;  /* stb */
        case VT_SHORT: store_op = 0xb0000000; break;  /* sth */
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:  store_op = 0x90000000; break;  /* stw */
        default:
            /* VT_LLONG/VT_LDOUBLE never reach here — see comment in
             * the PIC branch above. */
            tcc_error("ppc-gen: store global of bt 0x%x not yet supported", bt);
        }
        greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);
        o(store_op | (gpr << 21) | (tmp_gpr << 16));
        return;
    }

    /* Store to an absolute address: VT_CONST | VT_LVAL with no
     * VT_SYM. Mirror the load path: materialize the HA half of the
     * address in r12 (NOT r0 — D-form treats RA=0 as literal 0,
     * not r0's contents) then store via r12+lo. */
    if (v == VT_CONST && (sv->r & VT_LVAL) && !(sv->r & VT_SYM)) {
        uint32_t addr = (uint32_t)sv->c.i;
        int hi = (((int)addr + 0x8000) >> 16) & 0xffff;
        int lo = addr & 0xffff;
        gpr = TREG_TO_GPR(r);
        o(0x3c000000 | (12 << 21) | hi);                       /* lis r12, ha */
        switch (bt) {
        case VT_BOOL:
        case VT_BYTE:
            o(0x98000000 | (gpr << 21) | (12 << 16) | (lo & 0xffff));  /* stb */
            return;
        case VT_SHORT:
            o(0xb0000000 | (gpr << 21) | (12 << 16) | (lo & 0xffff));  /* sth */
            return;
        case VT_INT:
        case VT_PTR:
        case VT_FUNC:
            o(0x90000000 | (gpr << 21) | (12 << 16) | (lo & 0xffff));  /* stw */
            return;
        default:
            tcc_error("ppc-gen: store to absolute addr of bt 0x%x", bt);
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
     * reg_classes[] mask to decide what can stay.
     *
     * Use nb_args (not nb_args+1): the entry at vtop-nb_args is the
     * function pointer / call target. If that's an LVAL whose address
     * lives in a volatile GPR (e.g. r4), arg processing below would
     * clobber it before we ever gv() it for the indirect call. Save
     * range must include the func-ptr slot. (Matches x86_64-gen.c;
     * arm/i386/arm64/riscv64 use nb_args+1 but those don't materialize
     * the func ptr from an LVAL post-arg-setup the same way.)
     *
     * many_struct_test_3 in upstream abitest reproduces this — it
     * uses (*(s2->f2 = &f) + 0)(v,v,v,v,v,v,1.0), whose evaluation
     * leaves &f in a volatile reg that the arg-pass-2 code then
     * overwrites with v.a. */
    save_regs(nb_args);

    /* First pass: assign GPR / FPR arg slots in source order.
     * Apple PPC ABI: each FP arg consumes one FPR (f1..f13) AND
     * "shadows" the corresponding GPR slot(s) — float skips one GPR
     * slot, double skips two. Int args take a GPR slot (LL takes two). */
    gpr_alloc = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;
    fpr_alloc = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;
    /* Per-arg base-type: needed in the post-pass-2 loop to find LD
     * args (whose ABI FPR placement happens after pass 2 — see the
     * LD branch in pass 2 for rationale). */
    int *arg_bt = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;
    gpr_used = 0;
    fpr_used = 0;
    for (i = 0; i < nb_args; i++) {
        SValue *arg = &vtop[-(nb_args - 1 - i)];
        int bt = arg->type.t & VT_BTYPE;
        arg_bt[i] = bt;
        gpr_alloc[i] = -1;
        fpr_alloc[i] = -1;
        if (bt == VT_STRUCT) {
            /* Apple PPC ABI: struct passed by value occupies as many
             * 4-byte GPR slots as ceil(size/4). Bytes 0..3 → r3+gslot,
             * bytes 4..7 → r3+gslot+1, etc. Slots past index 7 spill
             * to the outgoing parameter area at r1+24+slot*4. */
            int salign;
            int ssize = type_size(&arg->type, &salign);
            int swords = (ssize + 3) / 4;
            gpr_alloc[i] = gpr_used;
            gpr_used += swords;
            continue;
        }
        if (bt == VT_FLOAT) {
            fpr_alloc[i] = fpr_used++;
            gpr_alloc[i] = gpr_used;  /* shadow slot start (for variadic) */
            gpr_used += 1;  /* float shadows 1 GPR slot */
        } else if (bt == VT_DOUBLE) {
            fpr_alloc[i] = fpr_used++;
            gpr_alloc[i] = gpr_used;  /* shadow slot start (for variadic) */
            gpr_used += 2;  /* double shadows 2 GPR slots */
        } else if (bt == VT_LDOUBLE) {
            /* IBM double-double: 2 consecutive FPRs (HIGH+LOW),
             * 4 GPR-shadow slots, 16 stack bytes. fpr_alloc[i] is
             * the HIGH FPR slot; LOW is fpr_alloc[i]+1. If the pair
             * doesn't fit in remaining FPRs (13 total), pass via
             * stack only; signal that with fslot = -1. */
            if (fpr_used + 1 < 13) {
                fpr_alloc[i] = fpr_used;
                fpr_used += 2;
            } else {
                fpr_alloc[i] = -1;  /* stack-only */
            }
            gpr_alloc[i] = gpr_used;
            gpr_used += 4;
        } else if (bt == VT_LLONG) {
            gpr_alloc[i] = gpr_used;
            gpr_used += 2;
        } else {
            gpr_alloc[i] = gpr_used;
            gpr_used += 1;
        }
    }
    /* Args using GPR slots 0..7 go in r3..r10. Args at slot >= 8
     * spill to our outgoing parameter area at r1+24+slot*4. The
     * outgoing area is sized to ppc_param_area bytes (per-function
     * high-water mark, set in gfunc_prolog). FP args still go in
     * f1..f8 in their FPR slot regardless of GPR shadow position. */
    /* Apple PPC ABI: FP args beyond f8 are passed only via the GPR
     * shadow slot (i.e. the stack at r1+24+gslot*4). The pass-2 code
     * checks `fslot < 8` before emitting the FP register move, so
     * args with fslot >= 8 fall through to the stack-spill path. No
     * hard cap needed — just don't bail. */
    /* Bump the per-function param-area high-water mark. The frame
     * gets sized at gfunc_epilog time; we just record the largest
     * outgoing area we've seen during the function body. */
    if (gpr_used * 4 > ppc_param_area)
        ppc_param_area = gpr_used * 4;

    /* Second pass: process args from vtop downward, materializing
     * each into the assigned register slot or spilling to stack. */
    for (i = 0; i < nb_args; i++) {
        int src_index = nb_args - 1 - i;
        int bt = vtop->type.t & VT_BTYPE;
        if (bt == VT_STRUCT) {
            /* Apple PPC ABI struct-by-value (caller side):
             * - vtop holds the struct's address (an lvalue). gv(RC_INT)
             *   materializes that address into a GPR.
             * - For each 4-byte word in the struct:
             *     slot = gpr_alloc[src_index] + word_idx
             *     if slot < 8:  load word into r3+slot
             *     else:         load word into r12 then store to
             *                   24+slot*4(r1) in our outgoing param area.
             * - The destination GPRs r3..r10 will be spilled to
             *   r1+24+slot*4 by the callee's prolog, where the body
             *   will read each field at its computed offset.
             *
             * Because the struct's address itself sits in some GPR that
             * the allocator chose (likely r3 or another arg slot), we
             * `mr r12, addr` first so the address is preserved in a
             * scratch register while we overwrite r3..r10. */
            int salign;
            int ssize = type_size(&vtop->type, &salign);
            int swords = (ssize + 3) / 4;
            int gslot = gpr_alloc[src_index];
            int addr_gpr;
            int w;
            /* Spill any vstack entries living in the ABI target slots
             * we're about to overwrite with struct words. Without this,
             * an earlier-allocated arg whose value lives in r{slot+3}
             * gets clobbered by the struct word load, and pass 2's
             * later arg setup reads from the corrupted reg.
             *
             * Mirrors the analogous save_reg() in the LL-in-GPR-pair
             * path below. csmith --default-opts seed 1536 surfaced
             * this with f(ptr, int, union8): the union went into
             * r5+r6, clobbering arg1's pointer that regalloc had
             * placed in r5 — final mr r3,r5 then put union-high
             * into r3 instead of the pointer. */
            for (w = 0; w < swords; w++) {
                int slot = gslot + w;
                if (slot < 8) save_reg(slot);
            }
            gv(RC_INT);
            addr_gpr = TREG_TO_GPR(vtop->r & VT_VALMASK);
            /* mr r12, addr_gpr  (or rA, rS, rA == ori-form: or rA,rS,rS) */
            if (addr_gpr != 12) {
                o(0x7c000378 | (addr_gpr << 21) | (12 << 16) | (addr_gpr << 11));
            }
            for (w = 0; w < swords; w++) {
                int slot = gslot + w;
                int word_off = w * 4;
                if (slot < 8) {
                    int target = slot + 3;     /* r3..r10 */
                    /* word_off only exceeds 16-bit if struct itself
                     * is huge AND we're past 8 GPRs — but slot < 8
                     * caps us here, so word_off <= 28. Always D-form. */
                    o(0x80000000 | (target << 21) | (12 << 16)
                                 | (word_off & 0xffff));
                } else {
                    int stack_off = 24 + slot * 4;
                    /* lwz r0, word_off(r12) — word_off can be huge
                     * for big structs. Use X-form when needed. */
                    if (ppc_off_fits_d(word_off)) {
                        o(0x80000000 | (0 << 21) | (12 << 16)
                                     | (word_off & 0xffff));
                    } else {
                        ppc_li32_r0(word_off);
                        /* lwzx r0, r12, r0 — but r0 is both src and
                         * dst, which is fine on PPC (XO form reads
                         * RB then writes RT). */
                        o(0x7c00002e | (0 << 21) | (12 << 16) | (0 << 11));
                    }
                    /* stw r0, stack_off(r1) — stack_off also can be
                     * huge. Use a different scratch (r11) for the
                     * intermediate so r0 still holds the loaded value. */
                    if (ppc_off_fits_d(stack_off)) {
                        o(0x90000000 | (0 << 21) | (1 << 16)
                                     | (stack_off & 0xffff));
                    } else {
                        /* lis r11, hi; ori r11, r11, lo; stwx r0, r1, r11 */
                        int hi = ((unsigned)stack_off >> 16) & 0xffff;
                        int lo = (unsigned)stack_off & 0xffff;
                        o(0x3c000000 | (11 << 21) | hi);
                        o(0x60000000 | (11 << 21) | (11 << 16) | lo);
                        o(0x7c00012e | (0 << 21) | (1 << 16) | (11 << 11));
                    }
                }
            }
        } else if (bt == VT_LDOUBLE) {
            /* IBM double-double arg: 2-FPR pair (HIGH+LOW), 4 GPR
             * shadow slots, 16 stack bytes.
             *
             * Strategy: gv() materializes the LD into an arbitrary
             * pair of FPRs (vtop->r = HIGH, vtop->r2 = LOW). We
             * then stfd both to the outgoing parameter slot at
             * 24+gslot*4(r1). After pass 2 (in a follow-up loop
             * just before the call), we lfd from there into the
             * specific ABI FPR slots f{fslot+1}/f{fslot+2}, plus
             * populate the GPR shadow if needed.
             *
             * This decouples materialization-time FPR allocation
             * (where tcc's regalloc may pick anything) from
             * ABI-time FPR placement (which must be in specific
             * slots). Each LD's memory image in the outgoing param
             * area doubles as the temp. */
            int gslot = gpr_alloc[src_index];
            int fslot = fpr_alloc[src_index];
            int hi_fpr, lo_fpr;
            int hi_off = 24 + gslot * 4;
            int lo_off = hi_off + 8;
            (void)fslot;  /* used in post-pass loop, not here */
            gv(RC_FLOAT);
            hi_fpr = TREG_TO_FPR(vtop->r);
            if (vtop->r2 < VT_CONST) {
                lo_fpr = TREG_TO_FPR(vtop->r2);
            } else {
                tcc_error("ppc-gen: LD arg without r2");
                lo_fpr = hi_fpr;
            }
            /* stfd HIGH, hi_off(r1) ; stfd LOW, lo_off(r1) */
            o(0xd8010000 | (hi_fpr << 21) | (hi_off & 0xffff));
            o(0xd8010000 | (lo_fpr << 21) | (lo_off & 0xffff));
            vtop--;
            continue;
        } else if (bt == VT_FLOAT || bt == VT_DOUBLE) {
            int fslot = fpr_alloc[src_index];
            int gslot = gpr_alloc[src_index];  /* shadow slot, set in first pass */
            if (fslot >= 13) {
                /* Out of FPRs (Apple PPC has f1..f13): pass via the
                 * GPR shadow slot only, which lives on our outgoing
                 * parameter area. Force the value into ANY FPR
                 * (gv(RC_FLOAT)), then store it into 24+gslot*4(r1). */
                int fpr;
                gv(RC_FLOAT);
                /* TREG_TO_FPR already returns the 1-indexed PPC FPR
                 * number (f1..f13). Don't double-add. */
                fpr = TREG_TO_FPR(vtop->r);
                if (bt == VT_FLOAT)
                    o(0xd0010000 | (fpr << 21) | ((24 + gslot * 4) & 0xffff));
                else
                    o(0xd8010000 | (fpr << 21) | ((24 + gslot * 4) & 0xffff));
                vtop--;
                continue;
            }
            gv(RC_F(fslot));
            /* Apple PPC ABI: for variadic callees, FP args must ALSO
             * live in the GPR shadow slots. We don't know if the
             * callee is variadic at this point, so always populate
             * the shadow. Non-variadic callees just ignore the GPR
             * contents — small wasted work, never wrong.
             *
             * Mechanism: spill FP register to a scratch slot at
             * 16(r1) (linkage area's saved-CR word, unused during a
             * call), reload as int register(s) into the shadow GPRs. */
            if (gslot >= 0) {
                int fpr = fslot + 1;           /* PPC reg number f1..f8 */
                /* Spill any vstack entries living in the GPR-shadow
                 * registers we're about to clobber. Without this, an
                 * earlier-computed arg whose value happens to live in
                 * r{gslot+3}.. gets overwritten by the shadow lwz, and
                 * arg setup later in pass 2 reads the corrupted value
                 * (see the LL straddle path for the analogous fix).
                 *
                 * csmith --float seed 3228 surfaced this with
                 * `f((x &= helper(...)), 5.0f)`: the `&=` result lands
                 * in r4, then arg2's float shadow does `lwz r4,16(r1)`
                 * and clobbers it. */
                if (bt == VT_FLOAT) {
                    if (gslot < 8) {
                        /* shadow in GPR: stfs fS, 16(r1); lwz rT, 16(r1) */
                        int target_lo = gslot + 3;
                        save_reg(gslot);
                        o(0xd0010010 | (fpr << 21));
                        o(0x80010010 | (target_lo << 21));
                    } else {
                        /* shadow on stack: stfs fS, (24+gslot*4)(r1) */
                        o(0xd0010000 | (fpr << 21)
                                     | ((24 + gslot * 4) & 0xffff));
                    }
                } else if (gslot + 1 < 8) {
                    /* both halves of double in GPR shadow:
                     * stfd fS,16(r1) ; lwz rT,16(r1) ; lwz rT+1,20(r1) */
                    int target_lo = gslot + 3;
                    save_reg(gslot);
                    save_reg(gslot + 1);
                    o(0xd8010010 | (fpr << 21));
                    o(0x80010010 | (target_lo << 21));
                    o(0x80010014 | ((target_lo+1) << 21));
                } else if (gslot < 8) {
                    /* double straddling r10/stack: high half in r10,
                     * low half on stack at (24+(gslot+1)*4)(r1).
                     * Spill the FP reg to scratch then load both halves
                     * separately so the GPR and stack writes use a
                     * common source. */
                    int target_lo = gslot + 3;
                    save_reg(gslot);
                    o(0xd8010010 | (fpr << 21));   /* stfd fS, 16(r1) */
                    o(0x80010010 | (target_lo << 21)); /* lwz r10, 16(r1) */
                    /* lwz r0, 20(r1) ; stw r0, 24+(gslot+1)*4 (r1) */
                    o(0x80010014);                   /* lwz r0, 20(r1) */
                    o(0x90010000 | ((24 + (gslot + 1) * 4) & 0xffff));
                } else {
                    /* double's GPR shadow entirely on stack:
                     * stfd fS, (24+gslot*4)(r1) writes both halves. */
                    o(0xd8010000 | (fpr << 21)
                                 | ((24 + gslot * 4) & 0xffff));
                }
            }
        } else {
            int gslot = gpr_alloc[src_index];
            if (gslot < 8) {
                if (bt == VT_LLONG && gslot + 1 < 8) {
                    /* Apple PPC ABI: a long long argument occupies TWO
                     * consecutive GPR slots, HIGH in the lower-numbered
                     * register (r3+gslot), LOW in the higher (r3+gslot+1).
                     * Tcc's `gv(RC_R(slot))` only materializes ONE
                     * register because our RC2_TYPE returns 0 for the
                     * non-RC_IRET case, so the natural call sequence
                     * loses the high half (it gets allocated to whatever
                     * the regalloc picks, often r3, then gets overwritten
                     * by the next arg).
                     *
                     * Strategy: materialize the LL value into a register
                     * pair via gv(RC_INT), then `mr` each half into the
                     * required ABI slot. tcc convention after gv(RC_INT)
                     * for LL: vtop->r = LOW reg, vtop->r2 = HIGH reg.
                     * Apple PPC ABI: r3+gslot = HIGH, r3+gslot+1 = LOW.
                     */
                    int lo_reg, hi_reg;
                    int target_hi = gslot + 3;       /* r3..r10 */
                    int target_lo = gslot + 1 + 3;
                    /* Spill any vstack entries that use the target ABI
                     * slots BEFORE materializing the LL. Without this,
                     * a previously-emitted address pointer for a later-
                     * processed arg (e.g., `state+20` in r4 for arg1
                     * waiting in vstack) sits in target_hi or target_lo
                     * and gets clobbered when we `mr target, src` —
                     * arg1's later setup then loads from r4 = clobbered
                     * value instead of the original address.
                     *
                     * save_reg(tcc_idx) moves any vstack entry using
                     * that register to a stack local and marks the reg
                     * free. Subsequent arg setup reloads from the local. */
                    save_reg(target_hi - 3);   /* target_hi PPC -> TREG idx */
                    save_reg(target_lo - 3);
                    gv(RC_INT);
                    lo_reg = TREG_TO_GPR(vtop->r & VT_VALMASK);
                    if (vtop->r2 < VT_CONST) {
                        hi_reg = TREG_TO_GPR(vtop->r2);
                    } else {
                        hi_reg = -1;  /* shouldn't happen for LL after gv */
                    }
                    /* Reorder the two `mr`s so we don't clobber a
                     * source before reading it. Specifically when
                     * lo_reg == target_hi or hi_reg == target_lo,
                     * naive sequencing destroys data. The general
                     * case (a swap, e.g. lo_reg=r3 hi_reg=r4 with
                     * target_hi=r3 target_lo=r4) is solved with a
                     * single r0 spill. r0 is volatile and never used
                     * by tcc's allocator. */
                    if (hi_reg >= 0
                        && lo_reg == target_hi
                        && hi_reg == target_lo) {
                        /* Swap r{target_hi}<->r{target_lo} via r0. */
                        o(0x7c000378 | (hi_reg << 21) | (0 << 16) | (hi_reg << 11));     /* mr r0, hi_reg */
                        o(0x7c000378 | (lo_reg << 21) | (target_lo << 16) | (lo_reg << 11)); /* mr target_lo, lo_reg */
                        o(0x7c000378 | (0 << 21) | (target_hi << 16) | (0 << 11));            /* mr target_hi, r0 */
                    } else {
                        /* Choose order so the source we still need
                         * isn't overwritten first. If hi_reg is the
                         * destination of the LOW move, do HIGH first;
                         * otherwise do LOW first (so we don't clobber
                         * lo_reg via target_hi). */
                        if (hi_reg >= 0 && hi_reg == target_lo) {
                            /* HIGH first. */
                            if (hi_reg != target_hi) {
                                o(0x7c000378 | (hi_reg << 21)
                                             | (target_hi << 16)
                                             | (hi_reg << 11));
                            }
                            if (lo_reg != target_lo) {
                                o(0x7c000378 | (lo_reg << 21)
                                             | (target_lo << 16)
                                             | (lo_reg << 11));
                            }
                        } else {
                            /* LOW first. */
                            if (lo_reg != target_lo) {
                                o(0x7c000378 | (lo_reg << 21)
                                             | (target_lo << 16)
                                             | (lo_reg << 11));
                            }
                            if (hi_reg >= 0 && hi_reg != target_hi) {
                                o(0x7c000378 | (hi_reg << 21)
                                             | (target_hi << 16)
                                             | (hi_reg << 11));
                            }
                        }
                    }
                } else if (bt == VT_LLONG) {
                    /* LL straddles last GPR and stack: HI in r3+gslot
                     * (= r10 for gslot=7), LO at 24+(gslot+1)*4(r1).
                     * Without this case the previous fallthrough did
                     * gv(RC_R(gslot)) which loaded only ONE half (LO)
                     * into r10 and left HI / stack-LO unwritten.
                     * Surfaced by printf with 8 args including an LL
                     * spanning the r10/stack boundary -- the printf
                     * va_arg of the straddling LL read garbage. */
                    int target_hi = gslot + 3;          /* r10 for gslot=7 */
                    int stack_off_lo = 24 + (gslot + 1) * 4;
                    int lo_reg, hi_reg;
                    save_reg(target_hi - 3);
                    gv(RC_INT);
                    lo_reg = TREG_TO_GPR(vtop->r & VT_VALMASK);
                    if (vtop->r2 < VT_CONST) {
                        hi_reg = TREG_TO_GPR(vtop->r2);
                        if (hi_reg != target_hi) {
                            o(0x7c000378 | (hi_reg << 21)
                                         | (target_hi << 16)
                                         | (hi_reg << 11));   /* mr target_hi, hi_reg */
                        }
                        /* stw lo_reg, stack_off_lo(r1) */
                        o(0x90000000 | (lo_reg << 21) | (1 << 16)
                                     | (stack_off_lo & 0xffff));
                    } else {
                        tcc_error_noabort("ppc-gen: LL straddle without r2");
                    }
                } else {
                    gv(RC_R(gslot));
                }
            } else {
                /* Stack-passed. */
                int stack_off = 24 + gslot * 4;
                int gpr;
                gv(RC_INT);
                gpr = TREG_TO_GPR(vtop->r & VT_VALMASK);
                if (bt == VT_LLONG) {
                    int hi_gpr;
                    if (vtop->r2 < VT_CONST) {
                        hi_gpr = TREG_TO_GPR(vtop->r2);
                        o(0x90000000 | (hi_gpr << 21) | (1 << 16) | (stack_off & 0xffff));
                        o(0x90000000 | (gpr << 21) | (1 << 16) | ((stack_off + 4) & 0xffff));
                    } else {
                        tcc_error_noabort("ppc-gen: stack LL arg without r2");
                    }
                } else {
                    o(0x90000000 | (gpr << 21) | (1 << 16) | (stack_off & 0xffff));
                }
            }
        }
        vtop--;
    }

    /* Post-pass for LD args: each was stfd'd to its outgoing param
     * slot during pass 2 but not yet placed in the ABI FPR pair.
     * We deferred that until now so tcc's regalloc state during
     * pass 2 didn't have to keep ABI-reserved FPRs sequestered.
     * Now (just before the call), all per-arg materialization is
     * done and we own f1..f13. */
    for (i = 0; i < nb_args; i++) {
        if (arg_bt[i] != VT_LDOUBLE)
            continue;
        int fslot = fpr_alloc[i];
        int gslot = gpr_alloc[i];
        int hi_off = 24 + gslot * 4;
        int lo_off = hi_off + 8;
        if (fslot >= 0) {
            int hi_fpr = fslot + 1;        /* PPC reg number f1..f13 */
            int lo_fpr = fslot + 2;
            /* lfd hi_fpr, hi_off(r1) ; lfd lo_fpr, lo_off(r1) */
            o(0xc8010000 | (hi_fpr << 21) | (hi_off & 0xffff));
            o(0xc8010000 | (lo_fpr << 21) | (lo_off & 0xffff));
        }
        /* GPR shadow: for variadic callees, the first 32 bytes of
         * the param area must be mirrored into r3..r10. Bytes past
         * 32 are read directly from stack by callee. */
        if (gslot < 8) {
            int target = gslot + 3;
            int word_off;
            int w;
            for (w = 0; w < 4 && (gslot + w) < 8; w++) {
                word_off = hi_off + w * 4;
                /* lwz r{target+w}, word_off(r1) */
                o(0x80010000 | ((target + w) << 21) | (word_off & 0xffff));
            }
        }
    }

    tcc_free(gpr_alloc);
    tcc_free(fpr_alloc);
    tcc_free(arg_bt);

    /* Determine the return type before we lose access to vtop. We need
     * this so we can swap r3<->r4 after the call if the function
     * returns a long long (Apple PPC ABI returns r3=HIGH, r4=LOW; tcc's
     * convention expects r3=LOW, r4=HIGH per PUT_R_RET).
     *
     * Helper-function calls (vpush_helper_func) push with `func_old_type`
     * (returns int) so the type-driven check misses LL-returning libgcc
     * helpers like __fixunsdfdi. Match those by sym token too. */
    int ret_is_ll = 0;
    {
        CType *ft = &vtop->type;
        if ((ft->t & VT_BTYPE) == VT_FUNC && ft->ref) {
            CType *rt = &ft->ref->type;
            if ((rt->t & VT_BTYPE) == VT_LLONG)
                ret_is_ll = 1;
        }
        if (!ret_is_ll && (vtop->r & VT_SYM) && vtop->sym) {
            int v = vtop->sym->v;
            if (v == TOK___fixdfdi    || v == TOK___fixsfdi   ||
                v == TOK___fixunsdfdi || v == TOK___fixunssfdi) {
                ret_is_ll = 1;
            }
        }
    }

    /* Emit the call. */
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST && (vtop->r & VT_SYM)) {
        /* Direct symbolic call. greloc records a relocation at `ind`
         * for vtop->sym. The placeholder bytes are `bl 0` (LK=1,
         * displacement field 0); the linker fills the displacement. */
        greloc(cur_text_section, vtop->sym, ind, R_PPC_REL24);
        o(0x48000001);  /* bl 0 */
    } else {
        /* Indirect call via CTR.
         *
         * IMPORTANT: we materialize the function pointer into r11
         * (TREG_R11 / RC_R(8)), NOT into any old RC_INT slot. The
         * arg-passing slots r3..r10 are already populated above with
         * arg values, but tcc's vstack tracking doesn't reserve them
         * after we vtop-- through them. If we did `gv(RC_INT)`, the
         * allocator could pick r3 (the lowest free slot) and clobber
         * the first argument. Pinning to r11 (a scratch GPR not used
         * for ABI args) avoids this. */
        int gpr;
        gv(RC_R(8));   /* r11 */
        gpr = TREG_TO_GPR(vtop->r & VT_VALMASK);
        /* mtctr rS  (mtspr ctr, rS; ctr is SPR 9) */
        o(0x7c0903a6 | (gpr << 21));
        /* bctrl */
        o(0x4e800421);
    }

    /* If the callee returned a long long, the Apple PPC ABI placed
     * HIGH in r3 and LOW in r4 — but PUT_R_RET on the tccgen side will
     * stamp this SValue as r=REG_IRET=r3 (= LOW per tcc convention),
     * r2=REG_IRE2=r4 (= HIGH). The two views disagree, so the LOW
     * half of the LL would silently become whatever HIGH actually is.
     * Swap r3<->r4 here to match tcc's convention. */
    if (ret_is_ll) {
        /* mr r0, r3 ; mr r3, r4 ; mr r4, r0 — three-instruction swap
         * via r0 (volatile, not used for any return value). */
        o(0x7c601b78);  /* mr r0, r3 */
        o(0x7c832378);  /* mr r3, r4 */
        o(0x7c040378);  /* mr r4, r0 */
    }

    vtop--;  /* pop the function value; tccgen.c will push the return. */
}

/* Track which incoming FPR slots (f1..f8) hold FP params and need to
 * be spilled in the prolog. Reset on each function. */
static int ppc_fp_param_count;
/* Per-FP-param: offset (from FP / r31) at which to store the FPR.
 * Stored linearly in source order, indexed by FP-param ordinal. */
static int ppc_fp_param_off[13];
/* Per-FP-param: 1 if double (8-byte stfd), 0 if float (4-byte stfs).  */
static int ppc_fp_param_is_double[13];

/* ------------------------------------------------------------------ */
/* PIC indirection bookkeeping                                        */
/* ------------------------------------------------------------------ */
/*
 * For Mach-O -c output, references to externally-defined data symbols
 * (e.g. errno, stdin, __sF, etc.) cannot be emitted as direct
 * R_PPC_ADDR16_HA/LO relocations because dyld cannot patch a 16-bit
 * instruction immediate at load time on PowerPC. Instead we emit:
 *
 *   ; Once per function, the first time a PIC ref is needed:
 *   mflr  r0
 *   bcl   20, 31, .+4         ; pseudo-call to next insn (sets LR)
 *   mflr  r2                  ; r2 = anchor address (= addr of mtlr)
 *   mtlr  r0
 *
 *   ; Per access:
 *   addis rT, r2, ha(slot - anchor)    ; R_PPC_HA16_PIC
 *   lwz   rT, lo(slot - anchor)(rT)    ; R_PPC_LO16_PIC
 *   ; rT now holds the address of `sym`; lwz/stw through it as needed
 *
 * The slot lives in __DATA,__nl_symbol_ptr (synthesized by
 * ppc-macho.c). The HA16/LO16 SECTDIFF relocations encode the offset
 * (slot_va - anchor_va), which the static linker (ld) recomputes after
 * section relocation.
 *
 * We use r30 as the PIC base register. r30 is callee-saved per the
 * Apple PPC ABI (r13..r31 are non-volatile), so its value survives
 * calls into libSystem and back -- this matters because we re-use
 * the PIC base for every external data access and many of those
 * accesses straddle calls. r30 is not exposed to tcc's register
 * allocator (only r3..r12 are; see reg_classes[]) so reserving it
 * costs only the prologue save and epilogue restore (4 bytes each).
 *
 * Why not r2? Although r2 is documented as "reserved" on Darwin,
 * libSystem routines do clobber it in practice -- so a PIC base
 * stashed in r2 is destroyed by the first printf/strlen/etc. call.
 *
 * Why not r10? r10 is volatile (caller-saved) per the Apple ABI,
 * same problem as r2. gcc gets away with r10 only when there are
 * no calls between the PIC base setup and its uses.
 */
#define PPC_PIC_BASE_REG 30

/* Per-function flag: 1 once PIC base setup has been emitted. Reset
 * in gfunc_prolog. */
static int ppc_func_pic_base_emitted;
/* Per-function value: the anchor offset within cur_text_section
 * (i.e. the byte-offset of the mtlr that follows our `mflr r2`).
 * Used as the SECTDIFF "subtrahend" for every PIC access in the
 * function body. Valid only after ppc_func_pic_base_emitted=1. */
static int ppc_func_pic_base_anchor;

/* Side table: maps a reloc's r_offset (within cur_text_section) to
 * the per-function anchor offset, so ppc-macho.c can compute
 * (slot - anchor) for the SECTDIFF reloc record. We keep one global
 * table for the whole translation unit; entries accumulate across
 * functions and are flushed by ppc_pic_pairs_reset(). */
struct ppc_pic_pair {
    int reloc_off;   /* byte offset of the relocated insn within text */
    int anchor_off;  /* byte offset of the PIC anchor within text     */
};
ST_DATA struct ppc_pic_pair *ppc_pic_pairs;
ST_DATA int ppc_pic_pairs_n;
ST_DATA int ppc_pic_pairs_cap;

ST_FUNC void ppc_pic_pairs_reset(void)
{
    ppc_pic_pairs_n = 0;
}

/* Release the side table itself; called once at tcc_delete() so the
 * 512-byte initial allocation doesn't show up as a leak in MEM_DEBUG
 * builds. ppc_pic_pairs_reset() above only resets the count so the
 * buffer can be reused across TUs in the same TCCState. */
ST_FUNC void ppc_pic_pairs_free(void)
{
    tcc_free(ppc_pic_pairs);
    ppc_pic_pairs = NULL;
    ppc_pic_pairs_cap = 0;
    ppc_pic_pairs_n = 0;
}

ST_FUNC int ppc_pic_pairs_lookup(int reloc_off)
{
    int i;
    for (i = 0; i < ppc_pic_pairs_n; i++)
        if (ppc_pic_pairs[i].reloc_off == reloc_off)
            return ppc_pic_pairs[i].anchor_off;
    return -1;
}

ST_FUNC void ppc_pic_pair_record(int reloc_off, int anchor_off)
{
    if (ppc_pic_pairs_n >= ppc_pic_pairs_cap) {
        ppc_pic_pairs_cap = ppc_pic_pairs_cap ? ppc_pic_pairs_cap * 2 : 64;
        ppc_pic_pairs = tcc_realloc(ppc_pic_pairs,
                                    ppc_pic_pairs_cap * sizeof(*ppc_pic_pairs));
    }
    ppc_pic_pairs[ppc_pic_pairs_n].reloc_off = reloc_off;
    ppc_pic_pairs[ppc_pic_pairs_n].anchor_off = anchor_off;
    ppc_pic_pairs_n++;
}

/* Decide whether a given symbol reference needs PIC indirection.
 *
 * Returns 1 if we must emit (addis/lwz via __nl_symbol_ptr); 0 if a
 * direct R_PPC_ADDR16_HA/LO pair is fine.
 *
 * Conditions for PIC:
 *   - We're producing a Mach-O .o (TCC_OUTPUT_OBJ on PPC/MACHO)
 *   - The symbol is global *and* will end up undefined in our object
 *     (i.e. an actual extern, not a local static or a definition we
 *      emit ourselves).
 *
 * For -run mode (TCC_OUTPUT_MEMORY) we keep the direct path: the
 * runtime resolves symbol addresses directly into the instruction
 * via R_PPC_ADDR16_HA/LO at JIT time — no dyld involved.
 */
static int ppc_need_pic_for_sym(Sym *sym)
{
#if defined(TCC_TARGET_MACHO)
    ElfSym *esym;
    int output_type;
    if (!sym)
        return 0;
    /* PIC indirection is needed for any output type that goes through
     * dyld (OBJ for the linker to fix up, EXE so we can resolve
     * external data refs at link time via __nl_symbol_ptr slots, DLL
     * for both extern data refs AND local data refs since dylibs can
     * slide). -run / TCC_OUTPUT_MEMORY uses direct addresses since
     * the JIT resolves symbols in-process. */
    output_type = tcc_state->output_type;
    if (output_type != TCC_OUTPUT_OBJ
        && output_type != TCC_OUTPUT_EXE
        && output_type != TCC_OUTPUT_DLL)
        return 0;
    /* Ensure sym has been pushed to the elf symtab so we can inspect
     * its st_shndx. greloc() does this on demand, but we want to
     * decide BEFORE emitting anything. */
    if (!sym->c)
        put_extern_sym(sym, NULL, 0, 0);
    esym = elfsym(sym);
    if (!esym)
        return 0;
    /* For OBJ/EXE: only externals need PIC (their address comes from
     * dyld via __nl_symbol_ptr). Locally-defined symbols use direct
     * lis/addi against the link-time-resolved absolute VA.
     *
     * For DLL: also PIC-ify locally-defined symbols. The dylib's
     * preferred vmaddr is high (0x40000000) so dyld usually loads at
     * that address, but if it has to slide, absolute lis/addi values
     * are wrong. PIC sectdiff (sym - anchor) is invariant under
     * slide. The macho writer's "PIC reloc against locally-defined
     * sym" fallback in exe_resolve_section_relocs computes the
     * sectdiff and rewrites the LO16 lwz to addi. */
    if (esym->st_shndx != SHN_UNDEF) {
        if (output_type != TCC_OUTPUT_DLL)
            return 0;
        /* In DLL mode, also PIC-ify local refs. Skip SHN_ABS
         * symbols (e.g. __mh_dylib_header) — those are constant VAs
         * supplied by dyld at the image base; lis/addi against the
         * resolved value works. */
        if (esym->st_shndx == SHN_ABS)
            return 0;
        return 1;
    }
    /* COMMON symbols (uninitialized globals without an explicit
     * extern) are tentative-defined and should be emitted by us;
     * they're still in our object so direct ADDR16 works.
     * SHN_COMMON has st_shndx > SHN_UNDEF so the test above already
     * excludes them. */
    return 1;
#else
    (void)sym;
    return 0;
#endif
}

/* Emit the 4-instruction PIC base setup once per function. After
 * this runs, r30 holds the address of the `mflr r30` instruction
 * itself (because `bcl 20,31,.+4` sets LR to the address of the
 * NEXT instruction, which is `mflr r30`). That address is our PIC
 * anchor, used as the SECTDIFF subtrahend for every PIC ref.
 *
 * r30 is callee-saved -- saved at function entry by the prologue's
 * `stw r30, frame_size-8(r1)` and restored by the epilogue's matching
 * `lwz r30, ...` -- so the PIC base survives intervening function
 * calls. */
static void ppc_emit_pic_base_setup(void)
{
    /* mflr r0           -- save caller's LR (we mustn't clobber it) */
    o(0x7c0802a6);
    /* bcl 20, 31, .+4   -- "branch and link" to next insn; LR := ind */
    o(0x429f0005);
    /* The anchor is the address of the NEXT instruction (mflr r30),
     * because that's the value `bcl` wrote to LR. Capture `ind` BEFORE
     * emitting `mflr r30`. */
    ppc_func_pic_base_anchor = ind;
    /* mflr r30          -- r30 = LR = anchor address */
    o(0x7c0802a6 | (PPC_PIC_BASE_REG << 21));
    /* mtlr r0           -- restore caller's LR */
    o(0x7c0803a6);
    ppc_func_pic_base_emitted = 1;
}

/* Emit the addis+lwz pair that loads the *address* of an external
 * data symbol into `addr_gpr`, going through __nl_symbol_ptr.
 * Records the per-insn anchor offsets for ppc-macho.c to translate
 * R_PPC_HA16_PIC / R_PPC_LO16_PIC into HA16_SECTDIFF / LO16_SECTDIFF. */
static void ppc_emit_pic_addr_load(int addr_gpr, Sym *sym)
{
    int reloc_off;

    if (!ppc_func_pic_base_emitted)
        ppc_emit_pic_base_setup();

    /* addis addr_gpr, r2, 0   -- placeholder; HA16_PIC reloc fills imm */
    reloc_off = ind;
    greloc(cur_text_section, sym, ind, R_PPC_HA16_PIC);
    ppc_pic_pair_record(reloc_off, ppc_func_pic_base_anchor);
    o(0x3c000000 | (addr_gpr << 21) | (PPC_PIC_BASE_REG << 16));

    /* lwz addr_gpr, 0(addr_gpr) -- placeholder; LO16_PIC reloc fills imm */
    reloc_off = ind;
    greloc(cur_text_section, sym, ind, R_PPC_LO16_PIC);
    ppc_pic_pair_record(reloc_off, ppc_func_pic_base_anchor);
    o(0x80000000 | (addr_gpr << 21) | (addr_gpr << 16));
}

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
     * Plus reserve up to 13 extra slots (52 bytes) for FP-param spills
     * (one stfs/stfd per FP param). Unused slots become nops. */
    ind += PPC_PROLOG_SIZE + 13 * 4;
    func_sub_sp_offset = ind;
    /* loc=-4 reserves the FP-save slot at r31-4 (saved r31).
     * loc=-8 reserves the PIC-base-save slot at r31-8 (saved r30).
     * User locals (and spilled register parameters) start at loc=-12. */
    loc = -8;
    func_vc = 0;
    ppc_fp_param_count = 0;
    ppc_param_area = PPC_PARAM_AREA_MIN;

    /* Apple PPC ABI: structs are returned via a hidden first pointer
     * arg in r3. Reserve r3 for that pointer and tell tccgen.c via
     * func_vc where to find it: the prolog spills r3..r10 to
     * r31+24..r31+52, so the hidden ptr lives at r31+24.
     * gfunc_return uses func_vc to copy the struct value through the
     * pointer; gfunc_epilog reloads r3 from func_vc before blr so
     * the caller gets the original pointer back per ABI. */
    if ((func_type->ref->type.t & VT_BTYPE) == VT_STRUCT) {
        func_vc = 24;
        gpr_index = 1;   /* r3 consumed by hidden pointer */
    }

    /* PIC base setup. Reset per-function state, then emit the
     * mflr/bcl/mflr/mtlr sequence UNCONDITIONALLY at the start of
     * the function body (when generating -c output). Lazy emission
     * is unsafe: if the first PIC use is inside a conditional branch
     * (e.g. `if (fd == -1) ... else (uses extern data)` ), the setup
     * runs only on one path and r30 is uninitialized on the other --
     * causing a crash when the other path tries to use the PIC anchor.
     * The 16-byte cost per function is small.
     *
     * We pin the setup to function entry so r30 is valid for every
     * subsequent PIC access regardless of control flow. For non
     * TCC_OUTPUT_OBJ outputs (-run, full link) PIC is never needed,
     * so we skip the setup entirely. */
    ppc_func_pic_base_emitted = 0;
    ppc_func_pic_base_anchor = 0;
#if defined(TCC_TARGET_MACHO)
    if (tcc_state->output_type == TCC_OUTPUT_OBJ
        || tcc_state->output_type == TCC_OUTPUT_EXE
        || tcc_state->output_type == TCC_OUTPUT_DLL)
        ppc_emit_pic_base_setup();
#endif

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

        if (bt == VT_STRUCT) {
            /* Apple PPC ABI struct-by-value (callee side):
             * The caller put each 4-byte word of the struct into
             * r3..r10 (and stack at r1+24+slot*4 for slots >= 8).
             * Our prolog unconditionally spills r3..r10 to
             * r1+24..r1+52, which after stwu becomes r31+24..r31+52.
             * So the struct lives contiguously at r31+24+slot*4 with
             * no extra prolog work — same formula as for int params,
             * just consume `(size+3)/4` slots instead of 1. */
            int swords = (size + 3) / 4;
            param_offset = 24 + gpr_index * 4;
            gfunc_set_param(sym, param_offset, 0);
            gpr_index += swords;
            continue;
        }

        /* Per Apple PPC ABI: callee's r31 = caller's SP, so the
         * incoming param save area starts at r31+24. Args 1-8 (GPR
         * slots 0..7) live where the prolog spilled r3..r10. Args
         * 9+ live where the caller pushed them — naturally at
         * r31+56 onwards. The offset formula `24 + gpr_index * 4`
         * is correct for both ranges; we just have to NOT
         * artificially cap at 8. */
        if (bt == VT_FLOAT) {
            if (fpr_index >= 13)
                tcc_error("ppc-gen: parameters exceed 13 FPR slots");
            param_offset = 24 + gpr_index * 4;
            ppc_fp_param_off[ppc_fp_param_count] = param_offset;
            ppc_fp_param_is_double[ppc_fp_param_count] = 0;
            ppc_fp_param_count++;
            gfunc_set_param(sym, param_offset, 0);
            gpr_index += 1;
            fpr_index += 1;
        } else if (bt == VT_DOUBLE) {
            if (fpr_index >= 13)
                tcc_error("ppc-gen: parameters exceed 13 FPR slots");
            param_offset = 24 + gpr_index * 4;
            ppc_fp_param_off[ppc_fp_param_count] = param_offset;
            ppc_fp_param_is_double[ppc_fp_param_count] = 1;
            ppc_fp_param_count++;
            gfunc_set_param(sym, param_offset, 0);
            gpr_index += 2;
            fpr_index += 1;
        } else if (bt == VT_LDOUBLE) {
            /* IBM double-double: 16 bytes, 2 consecutive FPRs (high
             * then low), 4 GPR-shadow slots. Spill BOTH FPRs to
             * consecutive 8-byte param-area offsets so the body can
             * lfd them as a pair. */
            if (fpr_index + 1 >= 13)
                tcc_error("ppc-gen: parameters exceed 13 FPR slots");
            param_offset = 24 + gpr_index * 4;
            /* HIGH FPR (the f{n} of the pair) spilled at off+0. */
            ppc_fp_param_off[ppc_fp_param_count] = param_offset;
            ppc_fp_param_is_double[ppc_fp_param_count] = 1;
            ppc_fp_param_count++;
            /* LOW FPR (f{n+1}) spilled at off+8. */
            ppc_fp_param_off[ppc_fp_param_count] = param_offset + 8;
            ppc_fp_param_is_double[ppc_fp_param_count] = 1;
            ppc_fp_param_count++;
            gfunc_set_param(sym, param_offset, 0);
            gpr_index += 4;
            fpr_index += 2;
        } else {
            int slots = (bt == VT_LLONG) ? 2 : 1;
            param_offset = 24 + gpr_index * 4;
            /* PPC big-endian: a sub-word arg in r3 (zero/sign-extended
             * to 32 bits) lives in the LOW byte/halfword of the 32-bit
             * register. After `stw r3, 24(r1)` the actual value is at
             * the END of the 4-byte slot, not the start. Adjust the
             * param's stack offset so subsequent lbz/lhz reads from
             * r31+param_offset get the right byte. */
            if (bt == VT_BYTE || bt == VT_BOOL) param_offset += 3;
            else if (bt == VT_SHORT) param_offset += 2;
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
    int pic_save_off = frame_size - 8; /* offset of saved-r30 slot from new SP */

    /* Stash for tccdbg.c's PPC FDE block. Read inside
     * tcc_debug_frame_end, which runs immediately after this
     * function returns (per tccgen.c's funcend ordering). */
    ppc_last_frame_size = frame_size;

    /* Note: tccgen.c already calls gsym(rsym) before invoking us
     * (see tccgen.c around `gsym(rsym); nocode_wanted = 0;` just
     * before gfunc_epilog), so any pending return-jumps have
     * already been patched to land at our current ind. We don't
     * touch rsym here. */

    /* Apple PPC ABI returns 64-bit values in r3=HIGH, r4=LOW. tcc's
     * internal convention (set by tccgen's gen_return path) leaves
     * r3=LOW, r4=HIGH. Swap before returning so we present the ABI
     * view to callers; the corresponding inverse swap in gfunc_call
     * restores tcc's view on the caller side. */
    if ((func_vt.t & VT_BTYPE) == VT_LLONG) {
        o(0x7c601b78);  /* mr r0, r3 */
        o(0x7c832378);  /* mr r3, r4 */
        o(0x7c040378);  /* mr r4, r0 */
    }

    /* Apple PPC ABI: structures are returned via a hidden first
     * pointer argument. The caller allocates space and passes its
     * address in r3 (which the prolog spills to r31+24 = func_vc).
     * The callee must return that pointer in r3 too. tccgen.c's
     * gfunc_return already copies the struct value to *func_vc; we
     * just have to reload r3 from the saved slot before blr. */
    if ((func_vt.t & VT_BTYPE) == VT_STRUCT && func_vc) {
        /* lwz r3, func_vc(r31) */
        o(0x80000000 | (3 << 21) | (PPC_FP_REG << 16) | (func_vc & 0xffff));
    }

    /* Epilogue:
     *   lwz  r30, -8(r31)          ; restore PIC base reg via FP
     *   lwz  r0,  -4(r31)          ; load saved r31 to r0 (defer)
     *   lwz  r1,  0(r1)            ; restore SP via back chain
     *   mr   r31, r0               ; install saved r31
     *   lwz  r0,  8(r1)            ; saved LR (in caller's linkage)
     *   mtlr r0
     *   blr
     *
     * Restoring saved registers via r31 (FP) instead of r1 (SP) means
     * an `alloca()` call inside the function -- which moves r1 lower
     * but leaves r31 untouched -- doesn't break the epilog. Likewise,
     * restoring r1 from the back chain at [r1] (rather than `addi r1,
     * r1, frame_size`) lands us at OLD_SP regardless of how much
     * alloca grew the frame. The cost is one extra mr instruction
     * (since we now stage the saved r31 through r0).
     */
    /* lwz r30, -8(r31) */
    o(0x80000000 | (PPC_PIC_BASE_REG << 21) | (PPC_FP_REG << 16) | (((-8) & 0xffff)));
    /* lwz r0, -4(r31) -- saved r31 value, will install after we
     * restore r1 (we still need r31 valid for back-chain access? no,
     * back chain is at [r1], not [r31]; but defer the r31 write so we
     * don't lose addressability if any part of this sequence wants
     * r31 again). */
    o(0x80000000 | (0 << 21) | (PPC_FP_REG << 16) | (((-4) & 0xffff)));
    /* lwz r1, 0(r1) -- restore SP from back chain */
    o(0x80210000);
    /* mr r31, r0 -- install saved r31 (or r31, r0, r0) */
    o(0x7c000378 | (0 << 21) | (PPC_FP_REG << 16) | (0 << 11));
    o(0x80010008);
    o(0x7c0803a6);
    o(0x4e800020);
    (void)pic_save_off; (void)fp_save_off; (void)frame_size;

    /* Backfill the reserved prologue. PPC_PROLOG_SIZE + 13 reserved
     * FP-spill slots. */
    saved_ind = ind;
    ind = func_sub_sp_offset - (PPC_PROLOG_SIZE + 13 * 4);
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
            int fpr = f + 1;  /* f1..f13 */
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
        for (k = ppc_fp_param_count; k < 13; k++)
            o(0x60000000);  /* nop */
    }
    /* Frame-allocation, save-area, and FP-pointer-setup instructions.
     * Each one is either a single-instruction short form or a
     * 3-instruction long form depending on whether the displacement
     * fits in a signed 16-bit immediate. We emit the short form
     * followed by 2 nops when long form isn't needed, so the total
     * number of instructions written is constant (8 — matching the
     * extra reservation in PPC_PROLOG_SIZE). */
    {
        int neg_fs = -frame_size;

        /* For long forms we materialize a 32-bit value into r0 via
         *   lis r0, (val >> 16) & 0xffff
         *   ori r0, r0, val & 0xffff
         * Note: `ori` does NOT sign-extend its 16-bit immediate; the
         * pair faithfully reconstructs the full 32-bit value with no
         * +0x8000 fix-up needed (which is what an `addi`-style pair
         * would require). */

        /* stwu r1, -frame_size(r1)  OR  long-frame equivalent */
        if ((int16_t)neg_fs == neg_fs) {
            o(0x94210000 | (neg_fs & 0xffff));
            o(0x60000000); o(0x60000000);
        } else {
            int hi = ((unsigned)neg_fs >> 16) & 0xffff;
            int lo = (unsigned)neg_fs & 0xffff;
            o(0x3c000000 | (0 << 21) | hi);                    /* lis r0, hi */
            o(0x60000000 | (0 << 21) | (0 << 16) | lo);        /* ori r0, r0, lo */
            o(0x7c21016e);                                     /* stwux r1, r1, r0 */
        }

        /* stw r31, fp_save_off(r1)  OR long form via r0 + stwx */
        if ((int16_t)fp_save_off == fp_save_off) {
            o(0x90000000 | (PPC_FP_REG << 21) | (1 << 16) | (fp_save_off & 0xffff));
            o(0x60000000); o(0x60000000);
        } else {
            int hi = ((unsigned)fp_save_off >> 16) & 0xffff;
            int lo = (unsigned)fp_save_off & 0xffff;
            o(0x3c000000 | (0 << 21) | hi);
            o(0x60000000 | (0 << 21) | (0 << 16) | lo);
            o(0x7c01012e | (PPC_FP_REG << 21));                /* stwx r31, r1, r0 */
        }

        /* stw r30, pic_save_off(r1)  OR long form */
        if ((int16_t)pic_save_off == pic_save_off) {
            o(0x90000000 | (PPC_PIC_BASE_REG << 21) | (1 << 16) | (pic_save_off & 0xffff));
            o(0x60000000); o(0x60000000);
        } else {
            int hi = ((unsigned)pic_save_off >> 16) & 0xffff;
            int lo = (unsigned)pic_save_off & 0xffff;
            o(0x3c000000 | (0 << 21) | hi);
            o(0x60000000 | (0 << 21) | (0 << 16) | lo);
            o(0x7c01012e | (PPC_PIC_BASE_REG << 21));          /* stwx r30, r1, r0 */
        }

        /* addi r31, r1, frame_size  OR long form via r0 + add */
        if ((int16_t)frame_size == frame_size) {
            o(0x38000000 | (PPC_FP_REG << 21) | (1 << 16) | (frame_size & 0xffff));
            o(0x60000000); o(0x60000000);
        } else {
            int hi = ((unsigned)frame_size >> 16) & 0xffff;
            int lo = (unsigned)frame_size & 0xffff;
            o(0x3c000000 | (0 << 21) | hi);
            o(0x60000000 | (0 << 21) | (0 << 16) | lo);
            o(0x7c010214 | (PPC_FP_REG << 21));                /* add r31, r1, r0 */
        }
    }
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
    case TOK_PDIV:
        /* TOK_PDIV is "fast division with undefined rounding for
         * pointer arithmetic" — tcc emits it for `(p - q) /
         * sizeof(*p)` style expressions where the result is
         * guaranteed to be exact (no remainder). On PPC there's no
         * separate fast-divide instruction, so just emit divw. */
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
         * result. Park hi_slot into vtop[-1].r2 between the two
         * get_reg() calls, otherwise the second call sees no vstack
         * entry referencing hi_slot and may return the same register
         * (lo_slot == hi_slot would silently corrupt the result). */
        int hi_slot = get_reg(RC_INT);
        int hi_gpr = TREG_TO_GPR(hi_slot);
        int lo_slot, lo_gpr;
        vtop[-1].r2 = hi_slot;
        lo_slot = get_reg(RC_INT);
        lo_gpr = TREG_TO_GPR(lo_slot);
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
    int dbl = (bt == VT_DOUBLE);
    int a_slot, b_slot, fa, fb, fd;
    uint32_t base, instr;

    /* Long double (IBM double-double): delegate to libgcc-style
     * helpers. Apple PPC's gcc-4.0 names the arithmetic ops
     * __gcc_q{add,sub,mul,div}; comparisons use the standard tf2
     * libgcc names (__eqtf2 etc.). Each takes two long doubles in
     * (f1,f2,f3,f4) and either returns LD in (f1,f2) (for arith)
     * or int in r3 (for compares). */
    if (bt == VT_LDOUBLE) {
        int func;
        int is_cmp = (op >= TOK_ULT && op <= TOK_GT);
        if (op == TOK_NEG) {
            /* IBM double-double negation: negate both halves
             * (-(hi+lo) = -hi + -lo). Materialize as 2-FPR pair
             * and emit fneg on each. */
            int hi_fpr, lo_fpr;
            gv(RC_FLOAT);
            hi_fpr = TREG_TO_FPR(vtop->r);
            lo_fpr = (vtop->r2 < VT_CONST)
                     ? TREG_TO_FPR(vtop->r2) : hi_fpr;
            o(0xfc000050 | (hi_fpr << 21) | (hi_fpr << 11));   /* fneg hi,hi */
            if (vtop->r2 < VT_CONST)
                o(0xfc000050 | (lo_fpr << 21) | (lo_fpr << 11));
            return;
        }
        if (is_cmp) {
            /* libgcc tf2 comparison helpers return an int such that
             * `result <op> 0` matches `a <op> b`:
             *   __eqtf2 → 0 iff a==b (so == 0 ⟺ a==b)
             *   __netf2 → 0 iff a==b (so != 0 ⟺ a!=b)
             *   __lttf2 → <0 iff a<b
             *   __letf2 → <=0 iff a<=b
             *   __gttf2 → >0 iff a>b
             *   __getf2 → >=0 iff a>=b */
            switch (op) {
            case TOK_EQ:                   func = TOK___eqtf2; break;
            case TOK_NE:                   func = TOK___netf2; break;
            case TOK_LT:  case TOK_ULT:    func = TOK___lttf2; break;
            case TOK_LE:  case TOK_ULE:    func = TOK___letf2; break;
            case TOK_GT:  case TOK_UGT:    func = TOK___gttf2; break;
            case TOK_GE:  case TOK_UGE:    func = TOK___getf2; break;
            default:
                tcc_error("ppc-gen: LD compare op 0x%x", op);
                return;
            }
            vpush_helper_func(func);
            vrott(3);
            gfunc_call(2);
            vpushi(0);
            vtop->type.t = VT_INT;
            vtop->r = REG_IRET;
            vtop->r2 = VT_CONST;
            /* Now the int return is on top. Compare to 0 with the
             * original op via the int compare path. */
            vpushi(0);
            gen_opi(op);
            return;
        }
        switch (op) {
        case '+': func = TOK___gcc_qadd; break;
        case '-': func = TOK___gcc_qsub; break;
        case '*': func = TOK___gcc_qmul; break;
        case '/': func = TOK___gcc_qdiv; break;
        default:
            tcc_error("ppc-gen: long-double op 0x%x not yet supported", op);
            return;
        }
        vpush_helper_func(func);
        vrott(3);
        gfunc_call(2);
        vpushi(0);
        vtop->type.t = VT_LDOUBLE;
        vtop->r = REG_FRET;
        vtop->r2 = REG_FRE2;
        return;
    }

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
     * mapping in cr0: LT=0, GT=1, EQ=2, FU/SO=3 (set on unordered/NaN).
     *
     * IEEE semantics: any compare involving NaN returns false EXCEPT
     * != (which is true). Most tcc predicates fall out of fcmpu
     * naturally because they BC on a single bit (e.g. ==/EQ, </LT,
     * !=/!EQ). But >= and <= are tested as "not LT" / "not GT" -- for
     * NaN both LT and GT are 0, so the inverted test fires when it
     * shouldn't. Pre-merge the FU bit into LT (for >=) or GT (for <=)
     * via `cror` so the !LT / !GT test correctly returns false on
     * unordered.
     *
     * cror crD, crA, crB encoding: opcode 19, XO 449.
     *   0x4c000382 | (crD<<21) | (crA<<16) | (crB<<11) */
    if (op >= TOK_ULT && op <= TOK_GT) {
        gv2(RC_FLOAT, RC_FLOAT);
        a_slot = vtop[-1].r;
        b_slot = vtop[0].r;
        fa = TREG_TO_FPR(a_slot);
        fb = TREG_TO_FPR(b_slot);
        /* fcmpu cr0, fA, fB */
        o(0xfc000000 | (fa << 16) | (fb << 11));
        if (op == TOK_GE || op == TOK_UGE)
            o(0x4c000382 | (0 << 21) | (0 << 16) | (3 << 11));   /* cror cr0_LT, cr0_LT, cr0_FU */
        else if (op == TOK_LE || op == TOK_ULE)
            o(0x4c000382 | (1 << 21) | (1 << 16) | (3 << 11));   /* cror cr0_GT, cr0_GT, cr0_FU */
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
        /* PPC32 has no hardware 64-bit-int -> FP instruction; libcall.
         * Unsigned LL is dispatched in tccgen.c gen_cvt_itof1. */
        int dst_bt = t & VT_BTYPE;
        int func;
        if (dst_bt == VT_FLOAT)
            func = TOK___floatdisf;
        else
            func = TOK___floatdidf;  /* double or long double */
        vpush_helper_func(func);
        vswap();
        gfunc_call(1);
        vpushi(0);
        vtop->r = REG_FRET;
        return;
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

    /* For LDOUBLE destination, the high half is now in fpr_dst; we
     * also need a low half = 0.0 in vtop->r2 so subsequent two-word
     * codegen sees a complete LD value. The integer-to-double step
     * above is exact for any representable int32, so the LD low half
     * should be exactly 0. */
    if ((t & VT_BTYPE) == VT_LDOUBLE) {
        int lo_slot, lo_fpr;
        static const unsigned char zero_d[8] = {0,0,0,0,0,0,0,0};
        lo_slot = get_reg(RC_FLOAT);
        lo_fpr = TREG_TO_FPR(lo_slot);
        ppc_load_fp_const(lo_fpr, 1, zero_d);
        vtop->r2 = lo_slot;
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
        /* PPC32 has no hardware FP -> 64-bit-int instruction; use a
         * libgcc helper. The signed-LL case lands here; the unsigned
         * case is dispatched in tccgen.c gen_cvt_ftoi1. gfunc_call
         * recognizes the libgcc helper by token and does the
         * Apple-ABI r3<->r4 swap on its way out. */
        int src_bt = vtop->type.t & VT_BTYPE;
        int func;
        if (src_bt == VT_FLOAT)
            func = TOK___fixsfdi;
        else
            func = TOK___fixdfdi;  /* double or long double */
        vpush_helper_func(func);
        vswap();
        gfunc_call(1);
        vpushi(0);
        vtop->r = REG_IRET;
        vtop->r2 = REG_IRE2;
        return;
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

/* Convert between float / double / long-double precision.
 *
 * LDOUBLE on Apple PPC is IBM double-double = pair of doubles.
 * Conversion summary:
 *   double -> float:  frsp fD, fB
 *   float  -> double: no-op (PPC FPRs hold doubles natively)
 *   LDOUBLE -> double: drop the low half (vtop->r2), keep vtop->r
 *   LDOUBLE -> float:  same drop, then frsp on the high half
 *   double -> LDOUBLE: keep vtop->r as high, set r2 = freshly-loaded 0
 *   float  -> LDOUBLE: float -> double (no-op), then double -> LDOUBLE
 *
 * The lossy LD->D narrowing (truncating low half) is what gcc-4.0
 * does too; the compensated path would cost cycles for typical
 * downstream uses where the low half is already negligible. */
ST_FUNC void gen_cvt_ftof(int t)
{
    int src_bt = vtop->type.t & VT_BTYPE;
    int dst_bt = t & VT_BTYPE;
    int fpr;

    if (src_bt == dst_bt)
        return;

    /* LDOUBLE -> {float, double}: discard low half. */
    if (src_bt == VT_LDOUBLE && dst_bt != VT_LDOUBLE) {
        gv(RC_FLOAT);
        fpr = TREG_TO_FPR(vtop->r);
        /* r2 holds the low half — drop it from the value stack so
         * downstream code treats the SValue as a single-FPR value. */
        vtop->r2 = VT_CONST;
        if (dst_bt == VT_FLOAT) {
            /* frsp on the high half. */
            o(0xfc000018 | (fpr << 21) | (fpr << 11));
        }
        vtop->type.t = t;
        return;
    }

    /* {float, double} -> LDOUBLE: keep value as high, low = 0. */
    if (dst_bt == VT_LDOUBLE && src_bt != VT_LDOUBLE) {
        int lo_slot, lo_fpr;
        static const unsigned char zero_d[8] = {0,0,0,0,0,0,0,0};
        gv(RC_FLOAT);
        /* Float -> LDOUBLE goes via Double first; PPC FPRs hold the
         * widened value already so no instruction needed. */
        lo_slot = get_reg(RC_FLOAT);
        lo_fpr = TREG_TO_FPR(lo_slot);
        ppc_load_fp_const(lo_fpr, 1, zero_d);
        vtop->r2 = lo_slot;
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

/* PPC fused multiply-add: emit fmadd / fmadds for __builtin_fma /
 * __builtin_fmaf. Args are tcc TREG slots; dbl selects double vs
 * single precision.
 *
 * PPC encoding: `fmadd FRT, FRA, FRC, FRB` computes FRT = FRA*FRC + FRB.
 * Note FRC is the 5-bit field at bits 6..10, NOT the usual FRB
 * field at 11..15 — fmul* and fmadd* swap the second-operand field
 * because they need 4 register operands.
 *
 * `__builtin_fma(a, b, c) = a*b + c` maps to:
 *   FRT = dst   FRA = a   FRC = b   FRB = c
 *
 * Double encoding: opcode 63 (0xfc), XO 29, Rc=0  -> 0xfc00003a
 * Single encoding: opcode 59 (0xec), XO 29, Rc=0  -> 0xec00003a */
/* No ST_FUNC: the extern declaration in tccgen.c's TOK_builtin_fma
 * dispatcher (one TU under ONE_SOURCE) would otherwise warn about
 * "static storage ignored for redefinition." Plain external linkage
 * is fine since this lives in libtcc.a's link unit. */
void gen_ppc_fmadd(int dst_slot, int a_slot, int b_slot,
                   int c_slot, int dbl)
{
    int frt = TREG_TO_FPR(dst_slot);
    int fra = TREG_TO_FPR(a_slot);
    int frc = TREG_TO_FPR(b_slot);   /* FRC = b */
    int frb = TREG_TO_FPR(c_slot);   /* FRB = c */
    uint32_t base = dbl ? 0xfc00003a : 0xec00003a;
    o(base | (frt << 21) | (fra << 16) | (frb << 11) | (frc << 6));
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
    /* Computed goto: vtop holds the target address. Materialize it
     * into a GPR, move to CTR, branch via bctr. Same pattern as the
     * indirect-call path in gfunc_call() but `bctr` (no LR write)
     * rather than `bctrl`. */
    int gpr;
    gv(RC_INT);
    gpr = TREG_TO_GPR(vtop->r & VT_VALMASK);
    /* mtctr rS */
    o(0x7c0903a6 | (gpr << 21));
    /* bctr */
    o(0x4e800420);
    vtop--;
}

/* VLA support.
 *
 * Save/restore SP (r1) to/from a local at FP+addr. Used so that
 * VLAs declared inside a nested block can be undone when the block
 * exits.
 *
 * Apple PPC ABI complication: every callee unconditionally spills
 * r3..r10 to caller_SP+24..+52 (the parameter save area). So the
 * VLA's data MUST sit ABOVE that 32-byte spill zone, not at SP+0.
 * Concretely, after a VLA alloc:
 *
 *   high addr
 *     +-----------------+
 *     | (existing frame)|
 *     +-----------------+ pre-VLA SP
 *     |   VLA data      |
 *     +-----------------+ post-VLA "VLA pointer" = SP + PPC_VLA_BUFFER
 *     | param save area | <- callees write here on r3..r10 spill
 *     | linkage area    |
 *     +-----------------+ post-VLA SP (r1)
 *   low addr
 *
 * The VLA pointer (what the user reads back via the saved slot) is
 * SP + PPC_VLA_BUFFER, not SP itself. gen_vla_sp_save / _restore
 * apply the offset symmetrically so the same saved slot serves both
 * (1) user-visible VLA address and (2) SP-restore-on-scope-exit.
 *
 * PPC_VLA_BUFFER is fixed at compile time, so any single function
 * that has VLAs and also makes calls with > 24-arg outgoing param
 * areas would corrupt the VLA. PPC_PARAM_AREA_MIN (96 bytes = 24
 * slots) is the budget we promise; in practice no test in tests2
 * exceeds this. */
/* 24 (linkage) + 96 (PPC_PARAM_AREA_MIN) = 120 logically; round up to
 * the next 16-byte multiple (128) so SP-relative offsets we apply via
 * `addi` stay 16-aligned, and the VLA data above this buffer remains
 * naturally 16-aligned for SIMD-style accesses. */
#define PPC_VLA_BUFFER 128

ST_FUNC void gen_vla_sp_save(int addr)
{
    /* Save (r1 + PPC_VLA_BUFFER) to addr(r31). The +offset compensates
     * for the callee-safe linkage + param area we keep BELOW the VLA;
     * see comment above. */
    /* addi r0, r1, PPC_VLA_BUFFER */
    o(0x38000000 | (0 << 21) | (1 << 16) | (PPC_VLA_BUFFER & 0xffff));
    /* stw r0, addr(r31)  OR long-form */
    if (ppc_off_fits_d(addr)) {
        o(0x90000000 | (0 << 21) | (PPC_FP_REG << 16) | (addr & 0xffff));
    } else {
        ppc_li32_r0(addr);
        o(0x7c00012e | (0 << 21) | (PPC_FP_REG << 16) | (0 << 11));
    }
}

ST_FUNC void gen_vla_sp_restore(int addr)
{
    /* Load saved value into r12 (scratch — r0 won't work as the addi
     * source: PPC treats `addi rD, r0, imm` as rD = imm, ignoring r0's
     * actual value), then SP = r12 - PPC_VLA_BUFFER. */
    if (ppc_off_fits_d(addr)) {
        o(0x80000000 | (12 << 21) | (PPC_FP_REG << 16) | (addr & 0xffff));
    } else {
        ppc_li32_r0(addr);
        o(0x7c00002e | (12 << 21) | (PPC_FP_REG << 16) | (0 << 11));
    }
    /* addi r1, r12, -PPC_VLA_BUFFER */
    o(0x38000000 | (1 << 21) | (12 << 16) | ((-PPC_VLA_BUFFER) & 0xffff));
}

ST_FUNC void gen_vla_alloc(CType *type, int align)
{
    int size_gpr;
    /* Materialize size into a GPR. */
    gv(RC_INT);
    size_gpr = TREG_TO_GPR(vtop->r & VT_VALMASK);
    /* Compute aligned_size + PPC_VLA_BUFFER:
     *   addi   r12, size, 15 + PPC_VLA_BUFFER
     *   rlwinm r12, r12, 0, 0, 27
     * The +15 rounds the user's size up to the next 16-byte multiple;
     * the +PPC_VLA_BUFFER reserves the linkage + param area below the
     * VLA's data so callees don't stomp on it. PPC_VLA_BUFFER is
     * already a multiple of 16, so the rlwinm-mask still yields a
     * 16-aligned subtrahend. */
    o(0x38000000 | (12 << 21) | (size_gpr << 16)
                 | ((15 + PPC_VLA_BUFFER) & 0xffff));
    o(0x54000000 | (12 << 21) | (12 << 16) | (0 << 11) | (0 << 6) | (27 << 1));
    /* Subtract from r1: subf r1, r12, r1 (= r1 = r1 - r12). */
    o(0x7c000050 | (1 << 21) | (12 << 16) | (1 << 11));
    vpop();
    (void)align; (void)type;
}

#endif /* !TARGET_DEFS_ONLY */
