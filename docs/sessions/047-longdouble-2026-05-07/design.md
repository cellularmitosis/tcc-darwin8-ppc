# Design — long double = IBM double-double on PPC Darwin

## Decision tree

### Option A — full register pair (LL parallel)
Treat VT_LDOUBLE in the value stack as a **two-FPR-register pair**:
`vtop->r = high FPR`, `vtop->r2 = low FPR`. Mirrors how VT_LLONG
uses 2 GPRs. Need `RC_FRE2`, `REG_FRE2`, and tccgen.c
`R2_RET / RC2_TYPE` to recognize VT_LDOUBLE on PPC.

* Pro: fits tcc's existing two-word abstraction; gv() / spill /
  reload handle pairs natively.
* Pro: ABI is "natural" — caller passes `r, r2` in `f1,f2`;
  callee receives in `f1,f2`; no marshaling.
* Con: invasive — touches tccgen.c R2_RET, RC2_TYPE, USING_TWO_WORDS
  logic. Risk of breaking other arches.

### Option B — heap/blob with libgcc helpers (RISC-V parallel)
Keep VT_LDOUBLE values as a memory blob with a single-register
"handle" (similar to RISC-V putting LD in 2 INT regs). All
arithmetic and casts via libgcc helper calls.

* Pro: less coupling to tccgen.c value-stack.
* Con: completely different from how PPC LD actually flows in the
  ABI. We'd be emitting an LD value into 2 INT regs and copying to
  2 FPRs at the call boundary. Wasted shuffle on every op.

### Choice: A — register pair.

Tcc's two-register abstraction is exactly designed for this. The
RISC-V approach is a workaround for an arch where LD really IS in
INT regs (no quad FPR). On PPC, LD really IS in FPR pair. Use the
direct mapping.

## Specific changes

### Header / target defs (ppc-gen.c top, tcc.h)

```c
#define LDOUBLE_SIZE    16
#define LDOUBLE_ALIGN   4   /* Apple PPC32 ABI: LD is 4-aligned */
#define REG_FRE2        TREG_F(1)  /* second-half return slot = f2 */
#define RC_FRE2         RC_F(1)
```

### tccgen.c R2_RET / RC2_TYPE

Add PPC32 case:
```c
#elif defined TCC_TARGET_PPC32
    if (t == VT_LDOUBLE)
        return REG_FRE2;
```
in R2_RET. `RC2_TYPE` already routes `RC_FRET → RC_FRE2` if defined.

### gfunc_prolog

For VT_LDOUBLE param:
* Allocate 2 FPR slots (`fpr_index += 2`).
* Allocate 4 GPR shadow slots (`gpr_index += 4`).
* Record TWO entries in `ppc_fp_param_off` so the prolog spills
  both FPRs to consecutive 8-byte offsets in the param save area.
* `gfunc_set_param(sym, param_offset, 0)` — points at the 16-byte
  param image. Body code reads as 2 lfd.

Need to grow `ppc_fp_param_off / is_double / param_count` arrays
to 26 entries (13 FPRs / 2 = max 13 LDs but each LD has 2 entries).
Or change schema: each entry = one FPR-spill instruction.

### gfunc_call

For VT_LDOUBLE arg:
* Allocate 2 FPR slots, 4 GPR shadow slots.
* `gv(RC_F(fslot))` — but for two-word types, this materializes
  `r` and `r2` (the existing LL precedent). Need to verify gv()
  on a 2-FPR type allocates two consecutive FPR slots starting at
  `fslot`.
* `mr` (or rather, `fmr`) the (r, r2) into (f[fslot+1], f[fslot+2]).
* Populate GPR shadow slots from the FPR pair (matters for variadic).

### gfunc_epilog / gfunc_return

LD return: result already in (REG_FRET, REG_FRE2) = (f1, f2). No
swap needed (unlike LL which swaps r3/r4).

### load() / store()

```c
} else if (bt == VT_LDOUBLE) {
    /* lfd r_hi, off(base);  lfd r_lo, off+8(base) */
    int hi_fpr = TREG_TO_FPR(r);
    int lo_fpr = TREG_TO_FPR(sv->r2 < VT_CONST ? sv->r2 : <allocate>);
    ...
}
```

The trick: when `load()` is called with destination `r` (one FPR
slot), we need to know the SECOND slot too. tccgen's `load()` only
takes one `r` arg. Workaround: in the caller (`gv()` for two-word
types), tcc allocates two slots and calls load() twice. Look at how
LL does it.

### gen_opf for LDOUBLE

```c
if (bt == VT_LDOUBLE) {
    int func;
    switch (op) {
    case '+': func = TOK___gcc_qadd; break;
    case '-': func = TOK___gcc_qsub; break;
    case '*': func = TOK___gcc_qmul; break;
    case '/': func = TOK___gcc_qdiv; break;
    /* comparisons: __eqtf2 etc.; deferred for now */
    default: tcc_error("ppc-gen: LD op 0x%x not supported", op);
    }
    /* Helper signature: long double __gcc_qXX(long double, long double).
     * Both LDs already on vtop[-1] and vtop[0]; gfunc_call's existing
     * 2-FPR-pair handling carries them into (f1,f2,f3,f4). */
    vpush_helper_func(func);
    vrott(3);
    gfunc_call(2);
    vpushi(0);
    vtop->type.t = VT_LDOUBLE;
    vtop->r = REG_FRET;
    vtop->r2 = REG_FRE2;
    return;
}
```

### gen_cvt_ftof / gen_cvt_itof / gen_cvt_ftoi

For LD↔float/double, route through libgcc helpers:
* `__extendsftf2(float)` → LD (high = float, low = 0)
* `__extenddftf2(double)` → LD (high = double, low = 0)
* `__trunctfsf2(LD)` → float
* `__trunctfdf2(LD)` → double
* `__floatsitf(int)` / `__floatunsitf(uint)` → LD
* `__floatditf(LL)` / `__floatunditf(ULL)` → LD
* `__fixtfsi(LD)` → int / `__fixunstfsi(LD)` → uint
* `__fixtfdi(LD)` → LL / `__fixunstfdi(LD)` → ULL

Or implement them inline (simple cases like LD → double = drop low half).

### lib-ppc.c

Rewrite `__gcc_qadd / qsub / qmul / qdiv` to return long double
(was: returned `struct ppc_dd`, ABI mismatch). Body can stay lossy
(plain double arith) — ABI is what matters for abitest.

Add `__extendsftf2 / extenddftf2 / trunctfsf2 / trunctfdf2 /
floatsitf / floatunsitf / fixtfsi / fixunstfsi`. May not need
the LL ones for abitest.

### Token table additions

`tcc.h` `DEF` block: add `__gcc_qadd / qsub / qmul / qdiv` as
known helper names. Also __extendsftf2 etc. if used.
