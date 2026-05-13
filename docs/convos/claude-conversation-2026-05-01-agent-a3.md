# Claude Conversation Log

Session ID: agent-a344ac4ce2d6cf814
Date: 2026-05-01 03:32:05

---

## 🤖 Claude (Orchestrator)

Add 32-bit single + 64-bit double floating point codegen to `/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c`. This is session 013 of an ongoing PPC backend port for tcc. The PPC G3 has full IEEE 754 single + double FP hardware (FPU, registers f0..f31).

**Study first:**
- `/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c` — the existing backend; especially the `load()`, `store()`, `gen_opi()`, `gen_opf()` (currently a stub), `gen_cvt_itof()`, `gen_cvt_ftoi()`, `gen_cvt_ftof()` (all stubs), and the macros at the top (RC_FLOAT, TREG_F, REG_FRET, IS_FREG).
- `/Users/cell/claude/tcc-darwin8-ppc/tcc/arm64-gen.c` lines 489-695 (load/store) and 1848-2090 (gen_opf, conversions) — the cleanest reference for an RISC FP backend, similar register/arch shape to PPC.
- `/Users/cell/claude/tcc-darwin8-ppc/tcc/include/tccdefs.h` — see `__powerpc__` branch if any (probably none, falls through to a default).

**What we already have (don't duplicate):**
- Register layout: TREG_F(0..7) → PPC f1..f8 (10 GPRs r3-r12 + 8 FPRs f1-f8 = NB_REGS 18). RC_FLOAT, RC_F(x), REG_FRET = TREG_F(0).
- The big-endian instruction emitter `o(uint32_t)`.
- `IS_FREG(r)` macro.
- A working `mr` (`fmr` for FPRs) inside `load()` for register-to-register move.
- The frame pointer convention: r31 is FP, locals at negative offsets (r31-X), incoming params at positive offsets (r31+24+i*4).

**What you need to add:**

1. **FP load/store in `load()` and `store()`:**
   - For `VT_FLOAT` lvalues (local or pointer-deref): `lfs fD, off(rA)` (opcode 48: `0xc0000000 | (fpr << 21) | (rA << 16) | (off & 0xffff)`).
   - For `VT_DOUBLE` lvalues: `lfd fD, off(rA)` (opcode 50: `0xc8000000 | ...`).
   - Stores: `stfs` (opcode 52: `0xd0000000`), `stfd` (opcode 54: `0xd8000000`).
   - Use `TREG_TO_FPR(r)` to get the PPC FPR number (1..8) from a TREG slot (10..17).

2. **FP constant load** in `load()` for VT_CONST + VT_FLOAT/VT_DOUBLE: place the FP constant in the rodata section, emit `lfs/lfd` from a R_PPC_ADDR16_HA + R_PPC_ADDR16_LO addressing pair (or use a different scheme — see how arm64 does literal pool loads). For tcc-internal use, the rodata section is `cur_text_section`-adjacent; tcc has helpers like `section_add` and emits relocations via `greloc`. Note: PPC literal addressing uses lis + lfd-with-offset, two instructions.

3. **`gen_opf(int op)`:** binary FP operations. Pattern follows `gen_opi`:
   - `gv2(RC_FLOAT, RC_FLOAT)` to materialize both operands.
   - For each op (`+`, `-`, `*`, `/`):
     - Single precision: `fadds`/`fsubs`/`fmuls`/`fdivs` (opcode 59 for these, with extended opcodes).
     - Double precision: `fadd`/`fsub`/`fmul`/`fdiv` (opcode 63).
     - The choice of single vs double is based on `(vtop->type.t & VT_BTYPE) == VT_FLOAT` or VT_DOUBLE.
   - For unary `TOK_NEG`: `fneg fD, fB`.
   - For comparisons (TOK_ULT..TOK_GT range): `fcmpu cr0, fA, fB` followed by `vset_VT_CMP(op)`. This is similar to gen_opi's comparison handling. The condition bit selection for `gjmp_cond` is the same (LT=0, GT=1, EQ=2 in cr0).
   - PPC encodings (XO-form, XO field at bits 22-30):
     - fadd: opcode 63, XO 21 → 0xfc00002a
     - fsub: opcode 63, XO 20 → 0xfc000028
     - fmul: opcode 63, XO 25 → 0xfc000032 (NOTE: rB field is bits 6-10, fC field is bits 21-25 — fmul is special "A-form")
     - fdiv: opcode 63, XO 18 → 0xfc000024
     - fadds: opcode 59 → 0xec00002a
     - fsubs: opcode 59 → 0xec000028
     - fmuls: opcode 59 → 0xec000032
     - fdivs: opcode 59 → 0xec000024
     - fneg: opcode 63, XO 40 → 0xfc000050 | (fD<<21) | (fB<<11)
     - fcmpu: opcode 63, XO 0 → 0xfc000000 | (BF<<23) | (fA<<16) | (fB<<11), BF=0 for cr0
   
   **CRITICAL:** fmul / fmuls have a quirk — they use the C field (bits 21-25, where rB normally is) for the second operand instead of the B field. Check the PPC ISA doc to be sure. (For our purposes: `fmul fD, fA, fC` encoded as `0xfc000032 | (fD<<21) | (fA<<16) | (fC<<6)`.)

4. **`gen_cvt_itof(int t)`** — convert int (currently in a GPR, vtop->r) to float/double in fD. PPC has no direct integer-to-FP instruction in the user ISA (it's a multi-step process):
   ```
   ; Trick: write the int to memory as the low half of a 64-bit value
   ; whose high half is a magic constant (0x43300000 for sign convert),
   ; then load as double and subtract the magic.
   ; Apple ABI uses the standard sequence.
   ```
   Or simpler: use a tcc-internal libcall to libgcc's `__floatsidf` / `__floatsisf`. For now, output a tcc_error_noabort with "FP int-to-FP conversion not yet implemented" — defer. We can add the magic-double trick or libcall later.

5. **`gen_cvt_ftoi(int t)`** — convert FP to int. PPC has `fctiwz fD, fB` (convert to integer, round toward zero). The result is a 32-bit integer in the low half of the FPR. Then store the FPR to memory (`stfd`) and load the low half via `lwz`. Sequence:
   ```
   fctiwz fD, fB     ; convert
   stfd fD, -8(r1)   ; store somewhere (use a small scratch slot)
   lwz rD, -4(r1)    ; load low 32 bits
   ```
   Use a temporary stack slot — careful not to step on locals. Alternative: store at `r1+24` (start of outgoing param area, which is unused outside of an active call).
   
   tcc's `t` parameter is the target int type (int / long long / etc.). For long long target, the conversion is more complex; bail for that case.

6. **`gen_cvt_ftof(int t)`** — float<->double. PPC: `frsp fD, fB` (round to single) for double→float; reverse direction is implicit (single in FPR is already representable as double). Encoding: `frsp` opcode 63, XO 12 → `0xfc000018 | (fD<<21) | (fB<<11)`.

7. **FP arg passing in `gfunc_call`:** Apple PPC ABI passes FP args in f1..f13. When an FP arg is passed, the corresponding GPR slot is ALSO consumed/skipped (so a function `f(int a, double b, int c)` passes a in r3, b in f1 AND skips r4+r5 (since double is 8 bytes), c in r6). This is critical for ABI conformance.

   For our v1, simplification: extend the existing `gpr_alloc` two-pass logic in `gfunc_call`. Add an `fpr_used` counter. For each arg:
   - VT_FLOAT: consumes one FPR (f1+fpr_used) AND one GPR slot (skipped).
   - VT_DOUBLE: consumes one FPR AND two GPR slots (skipped).
   - Materialize FP args via `gv(RC_F(fpr_used))`.

   Don't worry about >13 FP args (rare); error if exceeded.

8. **FP arg unpacking in `gfunc_prolog`:** for FP params, spill the incoming FPR to a stack slot AND the corresponding GPR slot is unused. Per Apple ABI, FP args are in BOTH f1..f13 AND the corresponding GPR slot (so the callee can find them either way for varargs). Current prolog spills r3..r10 unconditionally. Add: also spill f1..f13 for variadic functions, OR for any function with FP params.

   Simpler for v1: spill f1..f8 unconditionally (8 instructions, 32 bytes added to prolog). Then the FP param's address is at a known offset.

9. **`gfunc_sret` may need updating** if structs containing only one FP member are returned in an FPR per Apple ABI — but since we don't yet support struct returns, defer.

10. **Test on imacg3:**
   ```
   ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && touch ppc-gen.c && /opt/make-4.3/bin/make 2>&1 | tail -5'
   ```
   Then test with:
   ```c
   int main(void){double x=3.14;double y=2.86;return (int)(x*y);}  /* expect 8 */
   ```
   Via:
   ```
   ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/h.c <<EOF
   ...test code...
   EOF
   ./tcc -B. -run /tmp/h.c; echo "exit=$?"'
   ```

**What NOT to do:**
- Don't tackle long double (= double-double on Apple). Treat it as an alias for double for now (the existing macros: `LDOUBLE_SIZE 8`, `LDOUBLE_ALIGN 8`).
- Don't worry about FP varargs for FP types specifically — let them work via the existing GPR spill machinery.
- Don't try to fully optimize the int↔FP conversion. A working tcc_error_noabort for cases we don't yet handle is fine.

**Workflow:**
- Edit `/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c` directly.
- Sync to imacg3 with `rsync -az /Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c imacg3:~/tcc-darwin8-ppc/tcc/`.
- Touch the file on imacg3 (`touch ppc-gen.c`) and rebuild: `/opt/make-4.3/bin/make 2>&1 | tail -5`.
- The G3 host is sometimes loaded; if a build/run hangs >60s, run `killall -9 tcc gcc-4.0 cc1 2>/dev/null` to clean up.

**Don't break existing tests:**
After each change, verify these still work:
```
int main(void){return 42;}                                                          → exit 42
int sq(int x){return x*x;}int main(void){return sq(7);}                             → exit 49
int fib(int n){if(n<2)return n;return fib(n-1)+fib(n-2);}int main(void){return fib(10);}  → exit 55
long long add64(long long a,long long b){return a+b;}int main(void){return (int)add64(0x12345678LL,0x10000000LL);}  → exit 120
```

If you can't make the FP-to-int conversion work cleanly, at minimum implement load/store + arithmetic + comparison + the move (fmr) and report which tests pass and which need conversion. tcc may sometimes inline-fold small FP expressions to constants, in which case the test passes without needing any conversion code.

Aim for clarity over completeness. ~200-400 lines added to ppc-gen.c. When done, give me a brief summary: which FP operations work, which abort, and a sample test program that runs end-to-end.

---

## 🤖 Claude (Subagent)

I'll start by studying the existing code, then implement FP support incrementally.

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c"
}


---

## 🤖 Claude (Subagent)

Now let me look at the arm64 reference for FP load/store and gen_opf patterns:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/arm64-gen.c",
  "offset": 489,
  "limit": 220
}


---

## 🤖 Claude (Subagent)

Now look at arm64 gen_opf:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/arm64-gen.c",
  "offset": 1848,
  "limit": 250
}


---

## 🤖 Claude (Subagent)

Now let me check tcc.h for relocation types and rodata section helpers:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"R_PPC_ADDR16\" /Users/cell/claude/tcc-darwin8-ppc/tcc/elf.h | head -20",
  "description": "Find PPC relocation constants"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"R_PPC_REL24\\|R_PPC_ADDR16_HA\\|R_PPC_ADDR16_LO\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccmacho.c /Users/cell/claude/tcc-darwin8-ppc/tcc/tccrun.c 2>/dev/null | head -30",
  "description": "Check macho relocations support"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"R_PPC\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccmacho.c /Users/cell/claude/tcc-darwin8-ppc/tcc/tccrun.c 2>/dev/null | head -30",
  "description": "Find R_PPC usage"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -rn \"R_PPC\" /Users/cell/claude/tcc-darwin8-ppc/tcc/*.c 2>/dev/null | head -30",
  "description": "Search all .c files for R_PPC usage"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-link.c"
}


---

## 🤖 Claude (Subagent)

Good — relocs ADDR16_HA and ADDR16_LO are already supported. Now let me check how other tcc backends place FP literals in the rodata section. Look at i386 or arm for a pattern:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"data_section\\|rodata\\|data_section ->\" /Users/cell/claude/tcc-darwin8-ppc/tcc/i386-gen.c | head -20",
  "description": "Find rodata usage in i386 backend"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"VT_CONST\" /Users/cell/claude/tcc-darwin8-ppc/tcc/i386-gen.c | head -30",
  "description": "Find VT_CONST FP loading in i386"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"VT_FLOAT\\|VT_DOUBLE\\|VT_LDOUBLE\" /Users/cell/claude/tcc-darwin8-ppc/tcc/arm-gen.c | head -40",
  "description": "Find FP handling in arm backend"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/arm-gen.c",
  "offset": 615,
  "limit": 90
}


---

## 🤖 Claude (Subagent)

Let me look at how arm uses the rodata literal pool:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"data_section\\|literal_pool\\|fp_constants\\|rodata\" /Users/cell/claude/tcc-darwin8-ppc/tcc/arm-gen.c | head -20",
  "description": "Find rodata references"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"data_section\\|put_extern_sym\\|section_add\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -20",
  "description": "Find section helper APIs in tccgen"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 8330,
  "limit": 50
}


---

## 🤖 Claude (Subagent)

I have enough context. Let me now look at how arm-gen handles VT_CONST FP load and how tccgen places FP constants:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"VT_CONST.*VT_LVAL\\|vpush_ref\\|fp_const\\|VT_FLOAT\\|VT_DOUBLE\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | grep -i \"fp\\|float\\|double\\|const\" | head -30",
  "description": "Find FP constant handling in tccgen"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 2555,
  "limit": 80
}


---

## 🤖 Claude (Subagent)

Good — tcc folds FP constants when both operands are constant. Now let me see how an FP constant is presented to us in load() — vtop->r form. Let me check the symbol-based load path used for FP literals:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"VT_LVAL.*VT_CONST.*VT_SYM\\|VT_SYM.*VT_CONST.*VT_LVAL\" /Users/cell/claude/tcc-darwin8-ppc/tcc/arm-gen.c | head -10",
  "description": "Find how arm handles VT_LVAL+VT_CONST+VT_SYM"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/arm-gen.c",
  "offset": 555,
  "limit": 90
}


---

## 🤖 Claude (Subagent)

Good. The pattern for FP literal: tcc gives us a `VT_CONST | VT_LVAL` (with possibly VT_SYM). For a constant FP value with no symbol it gives us `VT_CONST | VT_LVAL` and we need to materialize it ourselves.

Let me also check what tcc sets when you write `double x = 3.14`. Look for where FP constants are pushed:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"vpushd\\|vpushf\\|c\\.f =\\|c\\.d =\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -20",
  "description": "Find FP value pushing"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 5615,
  "limit": 35
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"push_tokc\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -10",
  "description": "Find push_tokc definition"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 5595,
  "limit": 20
}


---

## 🤖 Claude (Subagent)

So `vtop->r == VT_CONST` (no VT_SYM, no VT_LVAL) and `vtop->c.f` or `vtop->c.d` holds the constant. We need to handle this in load() when r is a FREG.

Now let me look at the gjmp_cond's BO/BI handling for FP. For FP comparisons we use cr0 same way. Let me look at one more thing — where is `vset_VT_CMP`:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"vset_VT_CMP\\|VT_CMP\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -20",
  "description": "Look for vset_VT_CMP definition"
}


---

## 🤖 Claude (Subagent)

OK, we have everything. Now I'll write the FP support. The plan is concrete. Let me start editing:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "/* Emit a load-immediate of a 32-bit value into GPR `gpr`. Uses\n *   li  rD, simm                  (one instruction, signed 16-bit)\n *   lis rD, hi                    (one instruction, low 16 zero)\n *   lis rD, hi  / ori rD, rD, lo  (two instructions, full 32-bit)\n */\nstatic void ppc_emit_li(int gpr, int32_t v)\n{\n    if (v >= -0x8000 && v < 0x8000) {\n        /* li rD, v  ->  addi rD, 0, v  (sign-extends) */\n        o(0x38000000 | (gpr << 21) | (v & 0xffff));\n    } else if ((v & 0xffff) == 0) {\n        /* lis rD, hi  ->  addis rD, 0, hi */\n        o(0x3c000000 | (gpr << 21) | ((v >> 16) & 0xffff));\n    } else {\n        /* lis rD, hi ; ori rD, rD, lo */\n        o(0x3c000000 | (gpr << 21) | ((v >> 16) & 0xffff));\n        o(0x60000000 | (gpr << 21) | (gpr << 16) | (v & 0xffff));\n    }\n}",
  "new_string": "/* Emit a load-immediate of a 32-bit value into GPR `gpr`. Uses\n *   li  rD, simm                  (one instruction, signed 16-bit)\n *   lis rD, hi                    (one instruction, low 16 zero)\n *   lis rD, hi  / ori rD, rD, lo  (two instructions, full 32-bit)\n */\nstatic void ppc_emit_li(int gpr, int32_t v)\n{\n    if (v >= -0x8000 && v < 0x8000) {\n        /* li rD, v  ->  addi rD, 0, v  (sign-extends) */\n        o(0x38000000 | (gpr << 21) | (v & 0xffff));\n    } else if ((v & 0xffff) == 0) {\n        /* lis rD, hi  ->  addis rD, 0, hi */\n        o(0x3c000000 | (gpr << 21) | ((v >> 16) & 0xffff));\n    } else {\n        /* lis rD, hi ; ori rD, rD, lo */\n        o(0x3c000000 | (gpr << 21) | ((v >> 16) & 0xffff));\n        o(0x60000000 | (gpr << 21) | (gpr << 16) | (v & 0xffff));\n    }\n}\n\n/* Place a 4- or 8-byte FP constant in rodata and return its offset.\n * The bytes are written big-endian to match the host PPC's view. */\nstatic unsigned int ppc_emit_fp_const(const void *bytes, int size, int align)\n{\n    unsigned int off;\n    Section *s = rodata_section;\n    /* section_add returns the starting offset and bumps data_offset. */\n    off = section_add(s, size, align);\n    /* The constant table lives in rodata which is little/big-endian-\n     * neutral memory; we want the IEEE 754 bytes in the standard\n     * big-endian layout for PPC's lfs/lfd. The host ints we got from\n     * c.f / c.d are already in host byte order. Since we're cross-\n     * building from a possibly-LE host, swap if needed. */\n    {\n        unsigned char *p = s->data + off;\n        const unsigned char *src = (const unsigned char *)bytes;\n        /* Detect endianness at runtime. */\n        unsigned int test = 1;\n        int host_le = (*(unsigned char *)&test) == 1;\n        int i;\n        if (host_le) {\n            for (i = 0; i < size; i++)\n                p[i] = src[size - 1 - i];\n        } else {\n            for (i = 0; i < size; i++)\n                p[i] = src[i];\n        }\n    }\n    return off;\n}\n\n/* Materialize an FP constant from the rodata pool into FPR `fpr`.\n * Uses the standard PPC two-instruction literal-load idiom:\n *     lis  r12, ha16(addr)\n *     lfs  fD, lo16(addr)(r12)   ; or lfd for double\n * The HA/LO halves are filled in by R_PPC_ADDR16_HA/LO relocations\n * pointing at the rodata symbol that owns our literal. */\nstatic void ppc_load_fp_const(int fpr, int is_double, const void *bytes)\n{\n    unsigned int off;\n    Sym *sym;\n    int size = is_double ? 8 : 4;\n    int align = is_double ? 8 : 4;\n    /* Rather than synthesize a fresh anonymous symbol per constant\n     * (which works but spams the symbol table), reuse a single\n     * label for the whole rodata section: __ppc_fp_const_pool, and\n     * use the byte-offset within it. We push the actual literal\n     * bytes here and pin a sym pointing at this offset. */\n    off = ppc_emit_fp_const(bytes, size, align);\n    /* Create an anonymous local symbol attached to the rodata\n     * section at our literal's offset. */\n    sym = get_sym_ref(is_double ? &char_pointer_type : &char_pointer_type,\n                      rodata_section, off, size);\n    /* lis r12, ha16(literal_addr) */\n    greloc(cur_text_section, sym, ind, R_PPC_ADDR16_HA);\n    o(0x3c000000 | (12 << 21));  /* lis r12, 0 */\n    if (is_double) {\n        /* lfd fD, lo16(literal)(r12) */\n        greloc(cur_text_section, sym, ind, R_PPC_ADDR16_LO);\n        o(0xc8000000 | (fpr << 21) | (12 << 16));\n    } else {\n        /* lfs fD, lo16(literal)(r12) */\n        greloc(cur_text_section, sym, ind, R_PPC_ADDR16_LO);\n        o(0xc0000000 | (fpr << 21) | (12 << 16));\n    }\n}"
}


---

## 🤖 Claude (Subagent)

Wait — I need to check what type `char_pointer_type` and `get_sym_ref` are. Let me confirm:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"get_sym_ref\\|char_pointer_type\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -10",
  "description": "Find get_sym_ref and char_pointer_type"
}


---

## 🤖 Claude (Subagent)

Good. Now update the `load()` function to handle FP loads:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Integer constant immediate. */\n    if (v == VT_CONST && !(sv->r & VT_LVAL) && !(sv->r & VT_SYM)) {\n        if (IS_FREG(r)) {\n            tcc_error(\"ppc-gen: load FP constant not yet implemented\");\n        }\n        gpr = TREG_TO_GPR(r);\n        ppc_emit_li(gpr, (int32_t)sv->c.i);\n        return;\n    }",
  "new_string": "    /* Integer constant immediate. */\n    if (v == VT_CONST && !(sv->r & VT_LVAL) && !(sv->r & VT_SYM)) {\n        if (IS_FREG(r)) {\n            int bt = sv->type.t & VT_BTYPE;\n            int fpr = TREG_TO_FPR(r);\n            if (bt == VT_FLOAT) {\n                /* Convert the constant to single precision (it may have\n                 * been parsed as double; tcc stores in c.f for VT_FLOAT). */\n                float f = sv->c.f;\n                ppc_load_fp_const(fpr, 0, &f);\n                return;\n            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n                double d = (bt == VT_LDOUBLE) ? (double)sv->c.ld : sv->c.d;\n                ppc_load_fp_const(fpr, 1, &d);\n                return;\n            } else {\n                tcc_error(\"ppc-gen: load FP constant of bt 0x%x\", bt);\n            }\n        }\n        gpr = TREG_TO_GPR(r);\n        ppc_emit_li(gpr, (int32_t)sv->c.i);\n        return;\n    }"
}


---

## 🤖 Claude (Subagent)

Now FP load via pointer in `load()`:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Pointer dereference: lvalue read from memory at the address\n     * held in a register (e.g. *p where p is in a register).\n     *\n     * Convention (matching i386/arm): for `(reg | VT_LVAL)`, the\n     * effective address is `reg + 0`. sv->c.i is residual from the\n     * pre-gv state and must not be added \u2014 the register already\n     * holds the full address. */\n    if (v < VT_CONST && (sv->r & VT_LVAL)) {\n        int bt = sv->type.t & VT_BTYPE;\n        int base_gpr;\n        if (IS_FREG(v))\n            tcc_error(\"ppc-gen: dereference of FP register?\");\n        base_gpr = TREG_TO_GPR(v);\n        if (IS_FREG(r))\n            tcc_error(\"ppc-gen: FP load via pointer not yet implemented\");\n        gpr = TREG_TO_GPR(r);\n        switch (bt) {",
  "new_string": "    /* Pointer dereference: lvalue read from memory at the address\n     * held in a register (e.g. *p where p is in a register).\n     *\n     * Convention (matching i386/arm): for `(reg | VT_LVAL)`, the\n     * effective address is `reg + 0`. sv->c.i is residual from the\n     * pre-gv state and must not be added \u2014 the register already\n     * holds the full address. */\n    if (v < VT_CONST && (sv->r & VT_LVAL)) {\n        int bt = sv->type.t & VT_BTYPE;\n        int base_gpr;\n        if (IS_FREG(v))\n            tcc_error(\"ppc-gen: dereference of FP register?\");\n        base_gpr = TREG_TO_GPR(v);\n        if (IS_FREG(r)) {\n            int fpr = TREG_TO_FPR(r);\n            if (bt == VT_FLOAT) {\n                /* lfs fD, 0(rA) */\n                o(0xc0000000 | (fpr << 21) | (base_gpr << 16));\n                return;\n            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n                /* lfd fD, 0(rA) */\n                o(0xc8000000 | (fpr << 21) | (base_gpr << 16));\n                return;\n            } else {\n                tcc_error(\"ppc-gen: FP load via ptr of bt 0x%x\", bt);\n            }\n        }\n        gpr = TREG_TO_GPR(r);\n        switch (bt) {"
}


---

## 🤖 Claude (Subagent)

Now FP load from VT_LOCAL:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Local variable, lvalue read (load from memory at FP+offset). */\n    if (v == VT_LOCAL && (sv->r & VT_LVAL)) {\n        int bt = sv->type.t & VT_BTYPE;\n        offset = (int)sv->c.i;\n        if (IS_FREG(r)) {\n            tcc_error(\"ppc-gen: FP local load not yet implemented\");\n        }\n        gpr = TREG_TO_GPR(r);",
  "new_string": "    /* Local variable, lvalue read (load from memory at FP+offset). */\n    if (v == VT_LOCAL && (sv->r & VT_LVAL)) {\n        int bt = sv->type.t & VT_BTYPE;\n        offset = (int)sv->c.i;\n        if (IS_FREG(r)) {\n            int fpr = TREG_TO_FPR(r);\n            if (bt == VT_FLOAT) {\n                /* lfs fD, off(r31) */\n                o(0xc0000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));\n                return;\n            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n                /* lfd fD, off(r31) */\n                o(0xc8000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));\n                return;\n            } else {\n                tcc_error(\"ppc-gen: FP local load of bt 0x%x\", bt);\n            }\n        }\n        gpr = TREG_TO_GPR(r);"
}


---

## 🤖 Claude (Subagent)

Now `store()` for FP via pointer and FP local:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Store-via-pointer: lvalue is a register holding the destination\n     * address. Same offset-zero convention as load(). */\n    if (v < VT_CONST && (sv->r & VT_LVAL)) {\n        int base_gpr;\n        if (IS_FREG(v))\n            tcc_error(\"ppc-gen: store via FP register?\");\n        base_gpr = TREG_TO_GPR(v);\n        if (IS_FREG(r))\n            tcc_error(\"ppc-gen: FP store via pointer not yet implemented\");\n        gpr = TREG_TO_GPR(r);\n        switch (bt) {",
  "new_string": "    /* Store-via-pointer: lvalue is a register holding the destination\n     * address. Same offset-zero convention as load(). */\n    if (v < VT_CONST && (sv->r & VT_LVAL)) {\n        int base_gpr;\n        if (IS_FREG(v))\n            tcc_error(\"ppc-gen: store via FP register?\");\n        base_gpr = TREG_TO_GPR(v);\n        if (IS_FREG(r)) {\n            int fpr = TREG_TO_FPR(r);\n            if (bt == VT_FLOAT) {\n                /* stfs fS, 0(rA) */\n                o(0xd0000000 | (fpr << 21) | (base_gpr << 16));\n                return;\n            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n                /* stfd fS, 0(rA) */\n                o(0xd8000000 | (fpr << 21) | (base_gpr << 16));\n                return;\n            } else {\n                tcc_error(\"ppc-gen: FP store via ptr of bt 0x%x\", bt);\n            }\n        }\n        gpr = TREG_TO_GPR(r);\n        switch (bt) {"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    if (v == VT_LOCAL) {\n        offset = (int)sv->c.i;\n        if (IS_FREG(r)) {\n            tcc_error(\"ppc-gen: store FP local not yet implemented\");\n        }\n        gpr = TREG_TO_GPR(r);\n        switch (bt) {",
  "new_string": "    if (v == VT_LOCAL) {\n        offset = (int)sv->c.i;\n        if (IS_FREG(r)) {\n            int fpr = TREG_TO_FPR(r);\n            if (bt == VT_FLOAT) {\n                /* stfs fS, off(r31) */\n                o(0xd0000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));\n                return;\n            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n                /* stfd fS, off(r31) */\n                o(0xd8000000 | (fpr << 21) | (PPC_FP_REG << 16) | (offset & 0xffff));\n                return;\n            } else {\n                tcc_error(\"ppc-gen: store FP local of bt 0x%x\", bt);\n            }\n        }\n        gpr = TREG_TO_GPR(r);\n        switch (bt) {"
}


---

## 🤖 Claude (Subagent)

Now `gen_opf()`:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "ST_FUNC void gen_opf(int op)\n{\n    tcc_error(\"ppc-gen: gen_opf stub (op=0x%x)\", op);\n}",
  "new_string": "/* Floating-point binary ops, unary negate, and comparisons.\n *\n * PPC FP encoding shapes (instruction = 4 bytes, big-endian):\n *   single-precision arithmetic (opcode 59 = 0xec000000):\n *     fadds  XO=21 -> 0xec00002a\n *     fsubs  XO=20 -> 0xec000028\n *     fmuls  XO=25 -> 0xec000032   (uses fC field at bits 21-25, A-form)\n *     fdivs  XO=18 -> 0xec000024\n *   double-precision arithmetic (opcode 63 = 0xfc000000):\n *     fadd   XO=21 -> 0xfc00002a\n *     fsub   XO=20 -> 0xfc000028\n *     fmul   XO=25 -> 0xfc000032   (uses fC field at bits 21-25, A-form)\n *     fdiv   XO=18 -> 0xfc000024\n *   misc (opcode 63):\n *     fneg   XO=40 -> 0xfc000050   (X-form: fD,fB)\n *     frsp   XO=12 -> 0xfc000018   (round to single)\n *     fcmpu  XO=0  -> 0xfc000000   (BF<<23 | fA<<16 | fB<<11)\n *\n * A-form layout (fadd, fsub, fdiv): fD=21..25, fA=16..20, fB=11..15.\n * fmul/fmuls use fC=6..10 instead of fB. (We emit it as fA*fC.) */\nST_FUNC void gen_opf(int op)\n{\n    int bt = vtop[0].type.t & VT_BTYPE;\n    int dbl = (bt == VT_DOUBLE || bt == VT_LDOUBLE);\n    int a_slot, b_slot, fa, fb, fd;\n    uint32_t base, instr;\n\n    if (op == TOK_NEG) {\n        int slot;\n        gv(RC_FLOAT);\n        slot = vtop[0].r;\n        fb = TREG_TO_FPR(slot);\n        fd = fb;\n        /* fneg fD, fB */\n        o(0xfc000050 | (fd << 21) | (fb << 11));\n        return;\n    }\n\n    /* Comparison: fcmpu cr0, fA, fB then VT_CMP. The condition bit\n     * mapping in cr0 is the SAME as for the integer comparisons we\n     * already handle in gjmp_cond (LT=0, GT=1, EQ=2). For unordered\n     * (NaN) results bit 3 is set; we treat NaN like \"not equal /\n     * unordered\"; tcc's predicates work correctly under fcmpu's\n     * IEEE semantics for finite operands. */\n    if (op >= TOK_ULT && op <= TOK_GT) {\n        gv2(RC_FLOAT, RC_FLOAT);\n        a_slot = vtop[-1].r;\n        b_slot = vtop[0].r;\n        fa = TREG_TO_FPR(a_slot);\n        fb = TREG_TO_FPR(b_slot);\n        /* fcmpu cr0, fA, fB */\n        o(0xfc000000 | (fa << 16) | (fb << 11));\n        vtop--;\n        vset_VT_CMP(op);\n        return;\n    }\n\n    gv2(RC_FLOAT, RC_FLOAT);\n    a_slot = vtop[-1].r;\n    b_slot = vtop[0].r;\n    fa = TREG_TO_FPR(a_slot);\n    fb = TREG_TO_FPR(b_slot);\n    fd = fa;  /* result lands in vtop[-1]'s slot */\n    base = dbl ? 0xfc000000 : 0xec000000;\n\n    switch (op) {\n    case '+':\n        /* fadd[s] fD, fA, fB */\n        instr = base | 0x2a | (fd << 21) | (fa << 16) | (fb << 11);\n        break;\n    case '-':\n        /* fsub[s] fD, fA, fB */\n        instr = base | 0x28 | (fd << 21) | (fa << 16) | (fb << 11);\n        break;\n    case '*':\n        /* fmul[s] fD, fA, fC -- second operand goes in fC field\n         * (bits 6..10, NOT the fB field at 11..15). */\n        instr = base | 0x32 | (fd << 21) | (fa << 16) | (fb << 6);\n        break;\n    case '/':\n        /* fdiv[s] fD, fA, fB */\n        instr = base | 0x24 | (fd << 21) | (fa << 16) | (fb << 11);\n        break;\n    default:\n        tcc_error(\"ppc-gen: gen_opf op 0x%x not yet supported\", op);\n    }\n    o(instr);\n    vtop--;\n}"
}


---

## 🤖 Claude (Subagent)

Now `gen_cvt_itof`, `gen_cvt_ftoi`, `gen_cvt_ftof`:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "ST_FUNC void gen_cvt_itof(int t)\n{\n    tcc_error(\"ppc-gen: gen_cvt_itof stub\");\n}\n\nST_FUNC void gen_cvt_ftoi(int t)\n{\n    tcc_error(\"ppc-gen: gen_cvt_ftoi stub\");\n}\n\nST_FUNC void gen_cvt_ftof(int t)\n{\n    tcc_error(\"ppc-gen: gen_cvt_ftof stub\");\n}",
  "new_string": "/* Convert an integer to FP. PPC has no direct user-mode int->FP\n * instruction; we use the classical \"magic constant\" trick:\n *\n *   For a signed 32-bit int x:\n *     1) flip the sign bit:           y = x XOR 0x80000000\n *     2) build the double:            d = 0x4330_0000_8000_0000 (high) | y (low)\n *     3) interpret as IEEE double:    d_val = 2^52 + 2^31 + (signed_x + 2^31)\n *     4) subtract magic:              d_val - (2^52 + 2^31) = signed_x\n *\n * Concretely we write the two halves of a double to a small scratch\n * area at r1+24 (start of caller's outgoing param area; idle when\n * we're not in a call), load it as a double, and subtract the magic\n * double which we've placed in rodata.  */\nST_FUNC void gen_cvt_itof(int t)\n{\n    int src_bt = vtop->type.t & VT_BTYPE;\n    int unsign = vtop->type.t & VT_UNSIGNED;\n    int dbl = (t == VT_DOUBLE || t == VT_LDOUBLE);\n    int gpr_int, fpr_dst, fpr_tmp;\n    int dst_slot, tmp_slot;\n    static const unsigned char magic_signed[8] = {\n        0x43, 0x30, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00\n    };\n    static const unsigned char magic_unsigned[8] = {\n        0x43, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00\n    };\n\n    if (src_bt == VT_LLONG) {\n        tcc_error(\"ppc-gen: long long to FP conversion not yet implemented\");\n    }\n\n    /* Materialize the int operand into a GPR. */\n    gv(RC_INT);\n    gpr_int = TREG_TO_GPR(vtop->r);\n\n    /* Allocate destination FPR. */\n    vtop--;\n    dst_slot = get_reg(RC_FLOAT);\n    fpr_dst = TREG_TO_FPR(dst_slot);\n    /* Allocate a second FPR for the magic constant. */\n    tmp_slot = get_reg(RC_FLOAT);\n    fpr_tmp = TREG_TO_FPR(tmp_slot);\n    vtop++;\n    vtop->r = dst_slot;\n    vtop->type.t = t;\n\n    /* Step 1: build the double in a scratch slot at r1+24..31. We\n     * need to write 0x43300000 to the high word and (gpr_int XOR\n     * 0x80000000 for signed) to the low word. We also need a third\n     * GPR for the high half \u2014 use r0 (always available scratch). */\n    if (!unsign) {\n        /* xor with 0x80000000 to flip sign bit. xoris rA, rS, 0x8000 */\n        o(0x6c000000 | (gpr_int << 21) | (0 << 16) | 0x8000);\n        /* That's xoris r0, gpr_int, 0x8000. Save back to r0. */\n        /* Actually: xoris encoding is opcode 27 = 0x6c000000:\n         *   xoris rA, rS, UI  ->  0x6c000000 | (rS<<21) | (rA<<16) | UI\n         * So our emit above does: rA=0, rS=gpr_int, UI=0x8000. Good. */\n    } else {\n        /* Just copy gpr_int into r0: mr r0, gpr_int = or r0, gpr_int, gpr_int */\n        o(0x7c000378 | (gpr_int << 21) | (0 << 16) | (gpr_int << 11));\n    }\n\n    /* Step 2: write 0x43300000 to (r1+24), and the (xor'd) low half\n     * to (r1+28). Load the high constant into a temp GPR (r12). */\n    /* lis r12, 0x4330 */\n    o(0x3c000000 | (12 << 21) | 0x4330);\n    /* stw r12, 24(r1) */\n    o(0x90000000 | (12 << 21) | (1 << 16) | 24);\n    /* stw r0, 28(r1) */\n    o(0x90000000 | (0 << 21) | (1 << 16) | 28);\n\n    /* Step 3: lfd fpr_dst, 24(r1) */\n    o(0xc8000000 | (fpr_dst << 21) | (1 << 16) | 24);\n\n    /* Step 4: load the magic double (matching signed/unsigned). */\n    ppc_load_fp_const(fpr_tmp, 1, unsign ? magic_unsigned : magic_signed);\n\n    /* fsub fpr_dst, fpr_dst, fpr_tmp  (double precision subtract) */\n    o(0xfc000028 | (fpr_dst << 21) | (fpr_dst << 16) | (fpr_tmp << 11));\n\n    /* If destination is float, round to single. */\n    if (!dbl) {\n        /* frsp fD, fB */\n        o(0xfc000018 | (fpr_dst << 21) | (fpr_dst << 11));\n    }\n}\n\n/* Convert FP to integer. Sequence:\n *   fctiwz fD, fB     ; convert to int32, round-toward-zero\n *   stfd   fD, 24(r1) ; spill\n *   lwz    rD, 28(r1) ; load low word (the integer result)\n * For unsigned int target, the same fctiwz instruction works for\n * values in [0, 2^31); larger values would need a different path\n * (we error out for now).  */\nST_FUNC void gen_cvt_ftoi(int t)\n{\n    int dst_bt = t & VT_BTYPE;\n    int gpr_dst, fpr_src, fpr_tmp;\n    int dst_slot, tmp_slot;\n\n    if (dst_bt == VT_LLONG) {\n        tcc_error(\"ppc-gen: FP to long long conversion not yet implemented\");\n    }\n\n    /* Materialize FP operand. */\n    gv(RC_FLOAT);\n    fpr_src = TREG_TO_FPR(vtop->r);\n\n    /* Allocate result GPR. */\n    vtop--;\n    dst_slot = get_reg(RC_INT);\n    gpr_dst = TREG_TO_GPR(dst_slot);\n    /* Allocate scratch FPR for the converted value (don't clobber the\n     * source if it's still live downstream). */\n    tmp_slot = get_reg(RC_FLOAT);\n    fpr_tmp = TREG_TO_FPR(tmp_slot);\n    vtop++;\n    vtop->r = dst_slot;\n    vtop->type.t = t;\n\n    /* fctiwz fD, fB -- opcode 63, XO 15 -> 0xfc00001e */\n    o(0xfc00001e | (fpr_tmp << 21) | (fpr_src << 11));\n    /* stfd fS, 24(r1) */\n    o(0xd8000000 | (fpr_tmp << 21) | (1 << 16) | 24);\n    /* lwz rD, 28(r1)  (low word of the spilled double = our int) */\n    o(0x80000000 | (gpr_dst << 21) | (1 << 16) | 28);\n}\n\n/* Convert between float and double precision.\n *   double -> float:  frsp fD, fB\n *   float  -> double: no-op (float in FPR is already representable\n *                     as double; PPC FPRs are always 64-bit and\n *                     internally hold the converted value). */\nST_FUNC void gen_cvt_ftof(int t)\n{\n    int src_bt = vtop->type.t & VT_BTYPE;\n    int dst_bt = t & VT_BTYPE;\n    int fpr;\n\n    if (src_bt == dst_bt)\n        return;\n\n    /* Treat LDOUBLE as DOUBLE (Apple PPC + our LDOUBLE_SIZE=8). */\n    if (src_bt == VT_LDOUBLE) src_bt = VT_DOUBLE;\n    if (dst_bt == VT_LDOUBLE) dst_bt = VT_DOUBLE;\n    if (src_bt == dst_bt) {\n        vtop->type.t = t;\n        return;\n    }\n\n    gv(RC_FLOAT);\n    fpr = TREG_TO_FPR(vtop->r);\n\n    if (src_bt == VT_DOUBLE && dst_bt == VT_FLOAT) {\n        /* frsp fD, fB -- round to single. Result stays in same FPR. */\n        o(0xfc000018 | (fpr << 21) | (fpr << 11));\n    } else {\n        /* float -> double: no instruction needed. */\n    }\n    vtop->type.t = t;\n}"
}


---

## 🤖 Claude (Subagent)

Now `gfunc_call` for FP arg passing and `gfunc_prolog` for FP param spilling:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "ST_FUNC void gfunc_call(int nb_args)\n{\n    int i;\n    int gpr_used;     /* count of GPR arg slots already consumed by args\n                         processed (we walk in source order to compute) */\n    int *gpr_alloc;   /* per-source-arg starting GPR slot (0..7) */\n\n    /* Spill any vstack values living in caller-saved regs that the\n     * call would clobber. tcc walks the vstack and uses our\n     * reg_classes[] mask to decide what can stay. */\n    save_regs(nb_args + 1);\n\n    /* First pass: assign GPR arg slots in source order. Long long\n     * consumes two consecutive GPR slots; everything else consumes\n     * one. Bail if total exceeds 8. */\n    gpr_alloc = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;\n    gpr_used = 0;\n    for (i = 0; i < nb_args; i++) {\n        SValue *arg = &vtop[-(nb_args - 1 - i)];\n        int bt = arg->type.t & VT_BTYPE;\n        if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n            tcc_free(gpr_alloc);\n            tcc_error(\"ppc-gen: floating-point arguments not yet supported\");\n        }\n        if (bt == VT_STRUCT) {\n            tcc_free(gpr_alloc);\n            tcc_error(\"ppc-gen: struct arguments not yet supported\");\n        }\n        gpr_alloc[i] = gpr_used;\n        gpr_used += (bt == VT_LLONG) ? 2 : 1;\n    }\n    if (gpr_used > 8) {\n        tcc_free(gpr_alloc);\n        tcc_error(\"ppc-gen: argument list exceeds 8 GPR slots (got %d)\",\n                  gpr_used);\n    }\n\n    /* Second pass: process args from vtop downward, materializing\n     * each into the assigned register slot. For long long, the low\n     * half lands in RC_R(slot) and tcc auto-allocates RC_R(slot+1)\n     * for the high half via the rc2 mechanism in gv(). */\n    for (i = 0; i < nb_args; i++) {\n        int src_index = nb_args - 1 - i;\n        int target_slot = gpr_alloc[src_index];\n        gv(RC_R(target_slot));\n        vtop--;\n    }\n    tcc_free(gpr_alloc);",
  "new_string": "ST_FUNC void gfunc_call(int nb_args)\n{\n    int i;\n    int gpr_used;     /* count of GPR arg slots already consumed */\n    int fpr_used;     /* count of FPR arg slots already consumed */\n    int *gpr_alloc;   /* per-source-arg starting GPR slot (0..7), or -1 for FP */\n    int *fpr_alloc;   /* per-source-arg FPR slot (0..7), or -1 for non-FP */\n\n    /* Spill any vstack values living in caller-saved regs that the\n     * call would clobber. tcc walks the vstack and uses our\n     * reg_classes[] mask to decide what can stay. */\n    save_regs(nb_args + 1);\n\n    /* First pass: assign GPR / FPR arg slots in source order.\n     * Apple PPC ABI: each FP arg consumes one FPR (f1..f13) AND\n     * \"shadows\" the corresponding GPR slot(s) \u2014 float skips one GPR\n     * slot, double skips two. Int args take a GPR slot (LL takes two). */\n    gpr_alloc = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;\n    fpr_alloc = nb_args ? tcc_malloc(nb_args * sizeof(int)) : NULL;\n    gpr_used = 0;\n    fpr_used = 0;\n    for (i = 0; i < nb_args; i++) {\n        SValue *arg = &vtop[-(nb_args - 1 - i)];\n        int bt = arg->type.t & VT_BTYPE;\n        gpr_alloc[i] = -1;\n        fpr_alloc[i] = -1;\n        if (bt == VT_STRUCT) {\n            tcc_free(gpr_alloc); tcc_free(fpr_alloc);\n            tcc_error(\"ppc-gen: struct arguments not yet supported\");\n        }\n        if (bt == VT_FLOAT) {\n            fpr_alloc[i] = fpr_used++;\n            gpr_used += 1;  /* float shadows 1 GPR slot */\n        } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n            fpr_alloc[i] = fpr_used++;\n            gpr_used += 2;  /* double shadows 2 GPR slots */\n        } else if (bt == VT_LLONG) {\n            gpr_alloc[i] = gpr_used;\n            gpr_used += 2;\n        } else {\n            gpr_alloc[i] = gpr_used;\n            gpr_used += 1;\n        }\n    }\n    if (gpr_used > 8) {\n        tcc_free(gpr_alloc); tcc_free(fpr_alloc);\n        tcc_error(\"ppc-gen: argument list exceeds 8 GPR slots (got %d)\",\n                  gpr_used);\n    }\n    if (fpr_used > 8) {\n        tcc_free(gpr_alloc); tcc_free(fpr_alloc);\n        tcc_error(\"ppc-gen: argument list exceeds 8 FPR slots (got %d)\",\n                  fpr_used);\n    }\n\n    /* Second pass: process args from vtop downward, materializing\n     * each into the assigned register slot. */\n    for (i = 0; i < nb_args; i++) {\n        int src_index = nb_args - 1 - i;\n        int bt = vtop->type.t & VT_BTYPE;\n        if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n            int fslot = fpr_alloc[src_index];\n            gv(RC_F(fslot));\n        } else {\n            int gslot = gpr_alloc[src_index];\n            gv(RC_R(gslot));\n        }\n        vtop--;\n    }\n    tcc_free(gpr_alloc);\n    tcc_free(fpr_alloc);"
}


---

## 🤖 Claude (Subagent)

Now `gfunc_prolog` for FP params and spilling. Let me check what the framework expects and consider what `gfunc_set_param` does for FP params. Looking at the existing code, the param offset is the GPR-slot offset. For an FP param in the Apple ABI, the value is in BOTH the FPR and the GPR shadow slot. So a callee can read it from r3+24+i*4. But for FP, double uses 2 slots — so the spill location for the high half is at +X and low half at +X+4. After our prologue spills r3..r10 to those exact slots, the value is reachable as a double at the right offset.

But wait — the FPR holds a properly-typed FP value, while what's in GPR shadow slots is the same bit pattern as the FPR (loaded by the caller from memory typically). Per Apple ABI, the caller stores both the FPR copy AND the int copy. We don't currently have the caller storing FPRs, so the GPR shadow contains whatever the caller's gv(RC_R(...)) produced — which we no longer do.

Let me reread my caller-side code. For float/double args, I only put them in FPRs and skip the GPR slots — meaning GPRs r3..r10 may have stale/uninitialized values. That's OK if the callee reads only from the FPR. But our prolog spills GPRs and the body reads from spilled GPR memory, so an FP param read would get garbage.

Solution: in `gfunc_prolog`, for FP params, spill the FPR (not the GPR) to the corresponding stack slot. The FPR has the right value.

Let me fix `gfunc_prolog`:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "ST_FUNC void gfunc_prolog(Sym *func_sym)\n{\n    CType *func_type = &func_sym->type;\n    Sym *sym;\n    int param_index = 0;\n\n    /* Reserve PROLOG_SIZE bytes for backfill in gfunc_epilog. The\n     * actual mflr/stw/stwu/stw r31/addi r31 instructions are emitted\n     * at gfunc_epilog time once we know the final frame size. */\n    ind += PPC_PROLOG_SIZE;\n    func_sub_sp_offset = ind;\n    /* loc=-4 reserves the FP-save slot at r31-4. User locals (and\n     * spilled register parameters) start at loc=-8. */\n    loc = -4;\n    func_vc = 0;\n\n    /* Apple PPC ABI: all 8 GPR arg slots (r3..r10) get spilled to\n     * the caller's parameter save area in our prologue (see\n     * PROLOG_SIZE doc above). Body code reads params at positive\n     * offsets from FP: param at slot i lives at r31 + 24 + i*4.\n     * gfunc_set_param tells tcc where each named param lives;\n     * variadic args (above the named ones) are reachable by\n     * walking forward from the last fixed param (the standard\n     * char*-based va_list mechanism in tccdefs.h). */\n    for (sym = func_type->ref->next; sym; sym = sym->next) {\n        CType *type = &sym->type;\n        int size, align;\n        int bt = type->t & VT_BTYPE;\n        int slots = (bt == VT_LLONG) ? 2 : 1;\n        int param_offset;\n\n        size = type_size(type, &align);\n        size = (size + 3) & ~3;\n\n        if (param_index + slots > 8)\n            tcc_error(\"ppc-gen: parameters exceed 8 GPR slots\");\n        if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE)\n            tcc_error(\"ppc-gen: floating-point parameters not yet supported\");\n        if (bt == VT_STRUCT)\n            tcc_error(\"ppc-gen: struct parameters not yet supported\");\n\n        param_offset = 24 + param_index * 4;\n        gfunc_set_param(sym, param_offset, 0);\n        param_index += slots;\n    }\n}",
  "new_string": "/* Track which incoming FPR slots (f1..f8) hold FP params and need to\n * be spilled in the prolog. Reset on each function. */\nstatic int ppc_fp_param_count;\n/* Per-FP-param: offset (from FP / r31) at which to store the FPR.\n * Stored linearly in source order, indexed by FP-param ordinal. */\nstatic int ppc_fp_param_off[8];\n/* Per-FP-param: 1 if double (8-byte stfd), 0 if float (4-byte stfs).  */\nstatic int ppc_fp_param_is_double[8];\n\nST_FUNC void gfunc_prolog(Sym *func_sym)\n{\n    CType *func_type = &func_sym->type;\n    Sym *sym;\n    int gpr_index = 0;     /* GPR slots consumed (FP args also shadow) */\n    int fpr_index = 0;     /* FPR slots consumed by FP params */\n\n    /* Reserve PROLOG_SIZE bytes for backfill in gfunc_epilog. The\n     * actual mflr/stw/stwu/stw r31/addi r31 instructions are emitted\n     * at gfunc_epilog time once we know the final frame size. */\n    ind += PPC_PROLOG_SIZE;\n    func_sub_sp_offset = ind;\n    /* loc=-4 reserves the FP-save slot at r31-4. User locals (and\n     * spilled register parameters) start at loc=-8. */\n    loc = -4;\n    func_vc = 0;\n    ppc_fp_param_count = 0;\n\n    /* Apple PPC ABI: all 8 GPR arg slots (r3..r10) get spilled to\n     * the caller's parameter save area in our prologue. Body code\n     * reads INTEGER params at positive offsets from FP:\n     *   slot i lives at r31 + 24 + i*4.\n     * For FP params (float/double), the value lives in f1..f8 (and\n     * the corresponding GPR shadow slot, but our caller path doesn't\n     * populate GPR shadows for FP args). So in our prolog we ALSO\n     * spill FPRs to the same address space; the param is then\n     * addressable as an in-memory float or double from the body.\n     *\n     * gfunc_set_param tells tcc where each named param lives. For\n     * FP params we point them at the same param-area offset; load()\n     * will emit lfs/lfd from r31+offset. */\n    for (sym = func_type->ref->next; sym; sym = sym->next) {\n        CType *type = &sym->type;\n        int size, align;\n        int bt = type->t & VT_BTYPE;\n        int param_offset;\n\n        size = type_size(type, &align);\n        size = (size + 3) & ~3;\n\n        if (bt == VT_STRUCT)\n            tcc_error(\"ppc-gen: struct parameters not yet supported\");\n\n        if (bt == VT_FLOAT) {\n            if (gpr_index + 1 > 8 || fpr_index >= 8)\n                tcc_error(\"ppc-gen: parameters exceed reg slots\");\n            param_offset = 24 + gpr_index * 4;\n            ppc_fp_param_off[ppc_fp_param_count] = param_offset;\n            ppc_fp_param_is_double[ppc_fp_param_count] = 0;\n            ppc_fp_param_count++;\n            gfunc_set_param(sym, param_offset, 0);\n            gpr_index += 1;\n            fpr_index += 1;\n        } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n            if (gpr_index + 2 > 8 || fpr_index >= 8)\n                tcc_error(\"ppc-gen: parameters exceed reg slots\");\n            param_offset = 24 + gpr_index * 4;\n            ppc_fp_param_off[ppc_fp_param_count] = param_offset;\n            ppc_fp_param_is_double[ppc_fp_param_count] = 1;\n            ppc_fp_param_count++;\n            gfunc_set_param(sym, param_offset, 0);\n            gpr_index += 2;\n            fpr_index += 1;\n        } else {\n            int slots = (bt == VT_LLONG) ? 2 : 1;\n            if (gpr_index + slots > 8)\n                tcc_error(\"ppc-gen: parameters exceed 8 GPR slots\");\n            param_offset = 24 + gpr_index * 4;\n            gfunc_set_param(sym, param_offset, 0);\n            gpr_index += slots;\n        }\n    }\n}"
}


---

## 🤖 Claude (Subagent)

Wait — the prolog instruction count is fixed at PPC_PROLOG_SIZE (52 bytes, 13 insns). If we add FP-spill instructions, that changes. Let me look again:

The current prolog is 13 instructions. We need to add an stfs/stfd per FP param. The prolog size needs to be variable, or we need to reserve enough space upfront for the worst case.

The cleanest approach: add the FP spills as part of a variable-sized prolog. Currently `ind += PPC_PROLOG_SIZE` reserves a fixed amount. Let's add reservation for up to 8 stfd instructions (32 more bytes max). Actually the simpler scheme: emit the FP spills BEFORE the framework prolog backfill — meaning we reserve more space.

Let me restructure: we reserve PROLOG_SIZE + FP_PARAMS*4 bytes, and emit FP spills in epilog backfill. This requires we know fp_param count at prolog time (we do).

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "offset": 800,
  "limit": 90
}


---

## 🤖 Claude (Subagent)

I need to make the prolog reservation larger to fit the FP spills. The simpler approach: reserve PROLOG_SIZE + 8*4 (32 bytes max for 8 stfs) extra, then in epilog backfill emit nops for unused slots. Let me update:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Reserve PROLOG_SIZE bytes for backfill in gfunc_epilog. The\n     * actual mflr/stw/stwu/stw r31/addi r31 instructions are emitted\n     * at gfunc_epilog time once we know the final frame size. */\n    ind += PPC_PROLOG_SIZE;\n    func_sub_sp_offset = ind;",
  "new_string": "    /* Reserve PROLOG_SIZE bytes for backfill in gfunc_epilog. The\n     * actual mflr/stw/stwu/stw r31/addi r31 instructions are emitted\n     * at gfunc_epilog time once we know the final frame size.\n     *\n     * Plus reserve up to 8 extra slots (32 bytes) for FP-param spills\n     * (one stfs/stfd per FP param). Unused slots become nops. */\n    ind += PPC_PROLOG_SIZE + 8 * 4;\n    func_sub_sp_offset = ind;"
}


---

## 🤖 Claude (Subagent)

Now update the epilog backfill to emit FP spills + nops:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Backfill the reserved prologue. 13 instructions / 52 bytes. */\n    saved_ind = ind;\n    ind = func_sub_sp_offset - PPC_PROLOG_SIZE;\n    /* mflr r0 */\n    o(0x7c0802a6);\n    /* stw r0, 8(r1) */\n    o(0x90010008);\n    /* Spill all 8 arg-passing GPRs (r3..r10) to caller's parameter\n     * save area. Done UNCONDITIONALLY so the prolog size is constant.\n     * Trade: 32 bytes per function for not needing flow analysis\n     * to detect \"is this function variadic\" at prolog-emit time. */\n    {\n        int g;\n        for (g = 3; g <= 10; g++) {\n            /* stw r(g), (24 + (g-3)*4)(r1) */\n            int off = 24 + (g - 3) * 4;\n            o(0x90000000 | (g << 21) | (1 << 16) | (off & 0xffff));\n        }\n    }\n    /* stwu r1, -frame_size(r1) */\n    o(0x94210000 | ((-frame_size) & 0xffff));\n    /* stw r31, fp_save_off(r1) */\n    o(0x90000000 | (PPC_FP_REG << 21) | (1 << 16) | (fp_save_off & 0xffff));\n    /* addi r31, r1, frame_size */\n    o(0x38000000 | (PPC_FP_REG << 21) | (1 << 16) | (frame_size & 0xffff));\n    ind = saved_ind;\n}",
  "new_string": "    /* Backfill the reserved prologue. PPC_PROLOG_SIZE + 8 reserved\n     * FP-spill slots = 13 + 8 = 21 instructions / 84 bytes. */\n    saved_ind = ind;\n    ind = func_sub_sp_offset - (PPC_PROLOG_SIZE + 8 * 4);\n    /* mflr r0 */\n    o(0x7c0802a6);\n    /* stw r0, 8(r1) */\n    o(0x90010008);\n    /* Spill all 8 arg-passing GPRs (r3..r10) to caller's parameter\n     * save area. Done UNCONDITIONALLY so the prolog size is constant.\n     * Trade: 32 bytes per function for not needing flow analysis\n     * to detect \"is this function variadic\" at prolog-emit time. */\n    {\n        int g;\n        for (g = 3; g <= 10; g++) {\n            /* stw r(g), (24 + (g-3)*4)(r1) */\n            int off = 24 + (g - 3) * 4;\n            o(0x90000000 | (g << 21) | (1 << 16) | (off & 0xffff));\n        }\n    }\n    /* Spill FP params: for each FP param recorded in gfunc_prolog,\n     * emit stfs / stfd at the same offset that the corresponding\n     * GPR-shadow slot would have used. Body code reads these as\n     * `lfs/lfd offset(r31)` after the FP-relative addressing kicks\n     * in. Note: the offset here is from r1, which equals (r31 - 0)\n     * BEFORE the stwu below adjusts r1. After stwu, r31 == old_sp =\n     * old_r1, so the GPR-shadow address from r1's perspective is\n     * (r31 + offset) post-prolog. */\n    {\n        int f, k;\n        for (f = 0; f < ppc_fp_param_count; f++) {\n            int fpr = f + 1;  /* f1..f8 */\n            int off = ppc_fp_param_off[f];\n            if (ppc_fp_param_is_double[f]) {\n                /* stfd fS, off(r1) -- opcode 54 */\n                o(0xd8000000 | (fpr << 21) | (1 << 16) | (off & 0xffff));\n            } else {\n                /* stfs fS, off(r1) -- opcode 52 */\n                o(0xd0000000 | (fpr << 21) | (1 << 16) | (off & 0xffff));\n            }\n        }\n        /* Pad unused FP-spill slots with nops. */\n        for (k = ppc_fp_param_count; k < 8; k++)\n            o(0x60000000);  /* nop */\n    }\n    /* stwu r1, -frame_size(r1) */\n    o(0x94210000 | ((-frame_size) & 0xffff));\n    /* stw r31, fp_save_off(r1) */\n    o(0x90000000 | (PPC_FP_REG << 21) | (1 << 16) | (fp_save_off & 0xffff));\n    /* addi r31, r1, frame_size */\n    o(0x38000000 | (PPC_FP_REG << 21) | (1 << 16) | (frame_size & 0xffff));\n    ind = saved_ind;\n}"
}


---

## 🤖 Claude (Subagent)

Now let me sync to imacg3 and try the build:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "rsync -az /Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c imacg3:~/tcc-darwin8-ppc/tcc/ && ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && touch ppc-gen.c && /opt/make-4.3/bin/make 2>&1 | tail -20'",
  "description": "Sync to imacg3 and build",
  "timeout": 120000
}


---

## 🤖 Claude (Subagent)

Build succeeded. Let me run the existing regression tests:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/h1.c <<EOF\nint main(void){return 42;}\nEOF\ncat > /tmp/h2.c <<EOF\nint sq(int x){return x*x;}\nint main(void){return sq(7);}\nEOF\ncat > /tmp/h3.c <<EOF\nint fib(int n){if(n<2)return n;return fib(n-1)+fib(n-2);}\nint main(void){return fib(10);}\nEOF\ncat > /tmp/h4.c <<EOF\nlong long add64(long long a,long long b){return a+b;}\nint main(void){return (int)add64(0x12345678LL,0x10000000LL);}\nEOF\nfor f in h1 h2 h3 h4; do\n  ./tcc -B. -run /tmp/$f.c\n  echo \"$f exit=$?\"\ndone'",
  "description": "Run existing regression tests",
  "timeout": 90000
}


---

## 🤖 Claude (Subagent)

All existing tests pass. Let me try the FP test:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp1.c <<EOF\nint main(void){double x=3.14;double y=2.86;return (int)(x*y);}\nEOF\n./tcc -B. -run /tmp/fp1.c\necho \"fp1 exit=$?\"'",
  "description": "FP test 1: double mult and ftoi",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

A fp_const is being requested in load() with sv->r = 0x330. Let me decode that — VT_VALMASK is presumably low bits. 0x330 = VT_CONST(0x30) | VT_LVAL(0x100) | VT_SYM(0x200), and type 0x9 = VT_DOUBLE | VT_LONG. So load() is being asked to load an FP constant via VT_CONST | VT_LVAL | VT_SYM — i.e., the constant has been placed in rodata and we're asked to load it through a symbol address.

We need to handle this in load(). Let me check VT_VALMASK first:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"^#define VT_VALMASK\\|^#define VT_CONST\\|^#define VT_LVAL\\|^#define VT_SYM\\|^#define VT_LOCAL\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tcc.h | head -20",
  "description": "Find VT_ constants"
}


---

## 🤖 Claude (Subagent)

OK so 0x330 = VT_CONST(0x30) | VT_LVAL(0x100) | VT_SYM(0x200). We need to add a load path for `VT_CONST | VT_LVAL | VT_SYM` (and probably also `VT_CONST | VT_SYM` for an address-of). Let me add it:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Local variable, lvalue read (load from memory at FP+offset). */\n    if (v == VT_LOCAL && (sv->r & VT_LVAL)) {",
  "new_string": "    /* Symbolic constant, lvalue OR address-of: load from a symbol's\n     * address. Used for FP literals (placed in rodata by tccgen) and\n     * for global variable accesses.\n     *\n     * Two cases:\n     *   VT_CONST | VT_SYM            -- want the symbol's address.\n     *   VT_CONST | VT_LVAL | VT_SYM  -- want *(symbol + sv->c.i).\n     *\n     * We use the standard PPC two-instruction literal-address idiom\n     * with R_PPC_ADDR16_HA / R_PPC_ADDR16_LO relocations:\n     *   lis  rT, ha16(sym)\n     *   addi rT, rT, lo16(sym)        ; for \"address\" form\n     * Or:\n     *   lis  rT, ha16(sym)\n     *   l[fbsh][zad] rD, lo16(sym)(rT) ; for \"load\" form\n     */\n    if (v == VT_CONST && (sv->r & VT_SYM)) {\n        int bt = sv->type.t & VT_BTYPE;\n        int extra_off = (int)sv->c.i;\n        int addr_gpr;\n        int want_load = (sv->r & VT_LVAL) != 0;\n\n        /* Choose a GPR to hold the symbol address. If our destination\n         * is a GPR and we want_load is FALSE (just want the address),\n         * use r itself. Otherwise use r12 as a scratch. */\n        if (!want_load && !IS_FREG(r)) {\n            addr_gpr = TREG_TO_GPR(r);\n        } else {\n            addr_gpr = 12;\n        }\n\n        /* lis addr_gpr, ha16(sym) */\n        greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_HA);\n        o(0x3c000000 | (addr_gpr << 21));\n        if (!want_load) {\n            /* addi rD, addr_gpr, lo16(sym) */\n            int dst_gpr = TREG_TO_GPR(r);\n            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);\n            o(0x38000000 | (dst_gpr << 21) | (addr_gpr << 16));\n            return;\n        }\n        /* Otherwise, emit the typed load with lo16(sym) as the disp. */\n        if (IS_FREG(r)) {\n            int fpr = TREG_TO_FPR(r);\n            if (bt == VT_FLOAT) {\n                /* lfs fD, lo16(sym)(addr_gpr) */\n                greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);\n                o(0xc0000000 | (fpr << 21) | (addr_gpr << 16));\n            } else if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {\n                /* lfd fD, lo16(sym)(addr_gpr) */\n                greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);\n                o(0xc8000000 | (fpr << 21) | (addr_gpr << 16));\n            } else {\n                tcc_error(\"ppc-gen: FP load via sym of bt 0x%x\", bt);\n            }\n            (void)extra_off; /* TODO: handle nonzero c.i */\n            return;\n        }\n        gpr = TREG_TO_GPR(r);\n        switch (bt) {\n        case VT_BYTE:\n            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);\n            o(0x88000000 | (gpr << 21) | (addr_gpr << 16));\n            if (!(sv->type.t & VT_UNSIGNED))\n                o(0x7c000774 | (gpr << 21) | (gpr << 16));  /* extsb */\n            return;\n        case VT_SHORT:\n            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);\n            if (sv->type.t & VT_UNSIGNED)\n                o(0xa0000000 | (gpr << 21) | (addr_gpr << 16));\n            else\n                o(0xa8000000 | (gpr << 21) | (addr_gpr << 16));\n            return;\n        case VT_INT:\n        case VT_PTR:\n        case VT_FUNC:\n            greloc(cur_text_section, sv->sym, ind, R_PPC_ADDR16_LO);\n            o(0x80000000 | (gpr << 21) | (addr_gpr << 16));\n            return;\n        default:\n            tcc_error(\"ppc-gen: load via sym of bt 0x%x not yet supported\", bt);\n        }\n    }\n\n    /* Local variable, lvalue read (load from memory at FP+offset). */\n    if (v == VT_LOCAL && (sv->r & VT_LVAL)) {"
}


---

## 🤖 Claude (Subagent)

Now since tccgen handles FP literals via rodata symbols, my own rodata-pool path in `ppc_load_fp_const` may never be reached for normal C source FP constants. But it's still useful for our int->FP conversion's magic constant. Let me also remove an unused `size` variable in gfunc_prolog:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "rsync -az /Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c imacg3:~/tcc-darwin8-ppc/tcc/ && ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && touch ppc-gen.c && /opt/make-4.3/bin/make 2>&1 | tail -10'",
  "description": "Rebuild on imacg3",
  "timeout": 120000
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && ./tcc -B. -run /tmp/fp1.c; echo \"fp1 exit=$?\"; for f in h1 h2 h3 h4; do ./tcc -B. -run /tmp/$f.c; echo \"$f exit=$?\"; done'",
  "description": "Run fp1 and regression tests",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

Existing tests still pass. fp1 is producing 169 = 0xa9 = floor(3.14*2.86)=8 ... that's wrong. Expected 8. We got 169.

Maybe the FP-to-int conversion is producing wrong bytes. Let me trace: stfd writes 8 bytes; we then load lwz from offset+4 which gives the LOW 32 bits of the double. The fctiwz instruction puts the integer in the LOW 32 bits of an FPR, but the register layout is "the integer is in bits 32-63 of the FPR" — meaning when stored as a double, the integer bytes occupy bytes 4..7 of the 8-byte big-endian memory image. So lwz at +28 (which is +4 from base 24) should get the integer. Let me check IBM docs:

Actually, fctiwz puts the result in the LOW-ORDER 32 BITS of the destination FPR. When stored with stfd, the FPR is written as 8 bytes big-endian. The LOW 32 bits of the FPR (bits 32-63) are written to the LOW addresses... no, wait. PPC FPRs are 64 bits. stfd writes them MSB-first (big-endian). So FPR bits 0-31 (high) go to memory bytes 0-3, FPR bits 32-63 (low) go to memory bytes 4-7.

So if fctiwz puts the integer in the LOW 32 bits of the FPR, after stfd the integer is in memory bytes 4-7, i.e., at offset +4 from where we stfd'd. So `lwz rD, 28(r1)` after `stfd 24(r1)` should be correct.

Wait — 169 is suspicious. Let me check: 3.14 * 2.86 = 8.9804. As an int = 8. But 169 = 0xa9. Hmm.

Let me try a simpler test:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp2.c <<EOF\nint main(void){double x=8.0;return (int)x;}\nEOF\n./tcc -B. -run /tmp/fp2.c; echo \"fp2 exit=$?\"'",
  "description": "Test simple ftoi",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

fp2 says 0. Should be 8. So the ftoi is broken — getting 0 means we likely fetched the wrong word (got the high-half = 0x40200000 (which has 0 in low byte) vs low-half).

Wait actually 0 means our lwz might be picking up zeroed-out memory. Could be the param area at +24 isn't what we think. Let me think — we use r1+24..31 as scratch. After our prolog, r1 has been adjusted DOWN (stwu r1, -frame_size(r1)). So the new r1 is the new frame's NEW_SP. r1+24 is the start of the OUTGOING parameter area (still inside our frame, still valid). Yes, that's what comment says.

But there's a subtle bug: when the integer-to-FP conversion runs, and we use r1+24..31, that area might also be getting used by something else. Let me re-examine fp2.

Actually fp2 is just a constant load + ftoi. Let me check whether fctiwz works on the value stored in the literal:

The value 8.0 as double IEEE: 0x4020000000000000.

Our load: lis r12, ha16(sym); lfd fD, lo16(sym)(r12) — looks right.

Then fctiwz fD, fB — produces integer 8 in the low 32 bits of fD.

Then stfd at r1+24, then lwz at r1+28. The stfd writes 8 bytes; the integer 8 is in bytes 4-7 (the low half). So lwz at +28 should read 8. But we got 0.

Let me check the fctiwz encoding. fctiwz: opcode 63 (0xfc000000), XO=15, with X-form encoding: bits 21-25 = D (dest fpr), 16-20 = 0, 11-15 = B (src fpr). XO = 15 in bits 1-10... wait I had `0xfc00001e` which is XO=15 << 1 | 0 = 0x1e. Let me double check.

PPC instruction encoding for X-form: bits 0-5 (opcode), bits 6-10 (D/S), bits 11-15 (A), bits 16-20 (B), bits 21-30 (XO), bit 31 (Rc).

Wait, I'm confusing PowerPC bit numbering. PowerPC numbers MSB=0. So in a 32-bit big-endian word:
- bits 0-5 = opcode (top 6 bits)
- bits 6-10 = next 5 bits (D field)
- bits 11-15 = next 5 (A)
- bits 16-20 = next 5 (B)
- bits 21-30 = next 10 (XO)
- bit 31 = bottom bit (Rc)

So D at bits 6-10 = positions (32-11)..(32-6) = 21..26 from LSB. Let me convert: in a uint32_t (LSB at bit 0), the D field is at bits 21-25 (occupying positions where IBM bits 6-10 are). XO is at bits 1-10 (IBM bits 21-30).

For fctiwz: XO = 15 in IBM bit-numbering of bits 21-30. So in uint32 LSB terms, XO at bits 1..10 with value 15 = `15 << 1 = 0x1e`. Correct.

So 0xfc00001e + (D<<21) + (B<<11). D=fpr_tmp, B=fpr_src. OK.

Hmm, but actually the issue might be that the destination tmp_slot allocated by get_reg is the same FPR as the source! The FREG slot 10 is f1. If the source is f1 (REG_FRET) and the get_reg returns f1 also (since vtop was decremented), then the source gets overwritten BEFORE we store it.

Wait no, after gv we decrement vtop, then call get_reg which sees the source FPR slot as free. So get_reg may return the SAME slot. But that's fine — we write fctiwz to it which overwrites with integer; the source isn't needed afterward.

Let me check if the issue is somewhere else. Maybe stfd isn't writing what I think. Let me try with just a simpler value path. Actually let me check whether the issue is the cast from constant:

Actually, when we do `(int)x` where x is a local double, tccgen might fold... no, x is at a runtime address. tccgen will gv it to RC_FLOAT then call gen_cvt_ftoi.

Let me check whether tcc's `gen_cvt_ftoi` is called at all or whether tcc converts via casting through a different path. Let me look for where gen_cvt_ftoi gets called:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"gen_cvt_ftoi\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -10",
  "description": "Find gen_cvt_ftoi callsites"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 3360,
  "limit": 35
}


---

## 🤖 Claude (Subagent)

OK, our gen_cvt_ftoi gets called with `t` = VT_INT for `(int)x`. Let me trace by hand. Do we have any debug output? Let me write a simpler test that prints the result to make sure we're seeing a real exit code, not zeroed memory:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp3.c <<EOF\nint main(void){double x=42.0;int y=(int)x;return y;}\nEOF\n./tcc -B. -run /tmp/fp3.c; echo \"fp3 exit=$?\"'",
  "description": "FP test 3: simple ftoi via local var",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

Hmm got 0. Now let me make a sanity test that bypasses ftoi:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp4.c <<EOF\nint main(void){double x=42.0;double y=x+1.0;return 7;}\nEOF\n./tcc -B. -run /tmp/fp4.c; echo \"fp4 exit=$?\"'",
  "description": "Test fp store to local + add",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

FP add+store works. Now ftoi alone:

Let me think — fctiwz needs to put result in the LOW 32 bits of the FPR. That's IBM bits 32-63 of the FPR. When stfd writes the FPR to memory, MSB-first big-endian, the high bits go first. So the LOW 32 bits land at bytes 4-7 of the 8-byte stfd output.

So at offset (24, 28): bytes 24-27 = high 32 bits of FPR (undefined), bytes 28-31 = low 32 bits = our integer. lwz at +28 reads bytes 28-31 as a 32-bit big-endian word. That should give the integer.

Wait — could the issue be that I'm using r1+24 but r1 isn't pointing where I think? After our prolog, r1 = (old_r1) - frame_size. The PARAMETER AREA in our frame layout (per ppc_frame_size comment) is at NEW_SP+24..55. So r1+24 is valid.

Let me check what gets generated. Add a printf to confirm. Actually let me write a test that decoys: store an INT to that area and read it back, to see if r1+24 area is actually accessible:

Actually maybe the issue is that the get_reg(RC_INT) is allocating r3 (REG_IRET). After we put result in r3 and decrement vtop, the function returns r3 as the int. But the CALLER's spill of r3 to caller's param area happens BEFORE the addi r31, etc. Wait no — gfunc_prolog spills r3..r10 into r1+24..52 BEFORE stwu adjusts r1. So at the time of spill, r1 is the OLD SP. After stwu r1, -framesize(r1), r1 is the NEW SP. So old (r1+24) = new (r1+24+frame_size).

So in MAIN's frame, the GPR-spill area is at r1+24+frame_size, but our SCRATCH access uses r1+24. These are TWO DIFFERENT addresses in main's frame — the latter is the OUTGOING param area, which is fine to use as scratch.

Hmm wait — in main, the body might call into our gen_cvt_ftoi where we use r1+24. But return is via r3 = our result. Let me examine the actual machine code emitted:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && ./tcc -B. -c -o /tmp/fp3.o /tmp/fp3.c && /opt/cctools/bin/otool -tV /tmp/fp3.o 2>/dev/null || objdump -d /tmp/fp3.o 2>/dev/null || echo \"no disasm\"'",
  "description": "Disassemble fp3.o",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'which otool gdb objdump 2>/dev/null; ls /usr/bin/ | grep -i obj 2>/dev/null'",
  "description": "Find disassembly tools"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && /usr/local/bin/otool -tV /tmp/fp3.o'",
  "description": "Disassemble via otool"
}


---

## 🤖 Claude (Subagent)

I see the issue. Look at line 0x68: `stfd f1, 0x18(r1)` (24) and `lwz r3, 0x1c(r1)` (28). The fctiwz wrote to f1, then stfd stored it to 24(r1). Then lwz loads from 28(r1) — that's the LOW word.

But wait — there's a load-FP-constant issue at line 54-58: `lis r12, ha16(_main)` — that's wrong! It's using ha16 of `_main`, not of the literal. Then `lfd f1, lo16(_L.0)(r12)`. The two relocations got mixed up — the lis is using main's address, the lfd uses literal's lo16. That's broken — the lis should target the LITERAL's symbol.

So the issue is actually with my load() path for VT_CONST|VT_LVAL|VT_SYM — I'm using `sv->sym` but that may not be the literal's sym. Wait no — I do call `greloc(... sv->sym ...)` for both halves. Otool's display might be showing the wrong symbol because the relocations both point to the same offset within the same section (rodata) and otool just labels by closest symbol. Let me check — actually the line "lis r12, ha16(_main)" — `_main` is the SECTION SYMBOL probably. The symbol `_L.0` is the literal label. The HA reloc is targeting some symbol _main(?). That looks fishy.

Actually otool's symbolization may just be confused. The bigger issue: result is 0 at runtime. Let me check the load constant first by trying a direct constant return:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp5.c <<EOF\ndouble get_d(void){return 42.0;}\nint main(void){double x=get_d();int y=(int)x;return y;}\nEOF\n./tcc -B. -run /tmp/fp5.c; echo \"fp5 exit=$?\"'",
  "description": "Test ftoi via FP function return",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

Same. So ftoi is returning 0. Wait, maybe the constant load is broken too — let me test that:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp6.c <<EOF\ndouble f(double x){return x+1.0;}\nint main(void){double r=f(41.0);return (int)r;}\nEOF\n./tcc -B. -run /tmp/fp6.c; echo \"fp6 exit=$?\"'",
  "description": "Test FP arg passing",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

Let me trace more carefully. Look at the disasm again: 

```
0000004c	stw	r31,0x4c(r1)
00000050	addi	r31,r1,0x50  <- r31 = old SP
00000054	lis	r12,ha16(_main)
00000058	lfd	f1,lo16(_L.0)(r12)
0000005c	stfd	f1,0xfff0(r31)   <- store double to local at r31-16
00000060	lfd	f1,0xfff0(r31)   <- load it back
00000064	fctiwz	f1,f1            <- convert to int in low 32 bits of f1
00000068	stfd	f1,0x18(r1)      <- store full 8 bytes to r1+24
0000006c	lwz	r3,0x1c(r1)      <- load r3 from r1+28 (low word)
00000070	stw	r3,0xffec(r31)   <- store r3 to local y at r31-20
00000074	lwz	r3,0xffec(r31)   <- load y back into r3 for return
```

The sequence looks right at the assembly level. The bug must be in the relocation values being filled in. Let me check the lis/lfd: `lis r12, ha16(_main)` — that's the issue. The relocation should target `_L.0` (the literal label) NOT `_main`. So when the relocator computes `ha16(addr_of_main)` — main starts at 0 — it gets ha=0. Then `lfd f1, lo16(_L.0)(r12)` — assuming r12=0, that loads from lo16(_L.0).

Hmm but if both relocs went to _L.0 we'd be fine. Are they going to _main? Let me check — in load() for VT_CONST|VT_SYM, I emit `greloc(..., sv->sym, ...)` for both. sv->sym should be the constant's symbol.

But maybe the issue is sv->sym for an FP constant from tccgen actually points to the SECTION itself, with c.i being the offset inside the section. Looking at `vpush_ref`:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"vpush_ref\\|vpushsym\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -10",
  "description": "Find vpush_ref"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 1115,
  "limit": 35
}


---

## 🤖 Claude (Subagent)

OK so each FP literal gets its own anonymous symbol pointing into rodata at the right offset. The relocations should work correctly.

Wait — I'm passing `c.i` as cval=0. But sv->c.i would also be 0 for the literal. Then my code does the right thing: lis r12, ha16(sym), lfd f1, lo16(sym)(r12). The relocation handler computes ha16 and lo16 of (sym_addr) properly.

Let me check the hex of the actual instruction bytes. 0x0c000000 with f1 (=fpr 1) gives us 0xc8000000 | (1 << 21) | (12 << 16) = 0xc82c0000. The encoding `lfd f1, 0(r12)` = 0xc82c0000. Then the 16-bit displacement gets lo16(_L.0). OK.

Let me try a much simpler diagnosis — maybe the load constant itself is wrong. Test fp4 returned 7 — but it never used the constant for anything. Let me test that the constant actually loads:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp7.c <<EOF\ndouble get(void){return 42.0;}\nint chk(double d){if(d>41.5 && d<42.5)return 99;return 1;}\nint main(void){return chk(get());}\nEOF\n./tcc -B. -run /tmp/fp7.c; echo \"fp7 exit=$?\"'",
  "description": "Test FP constant + return + arg + compare",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

Returns 1, meaning the comparison failed — the value 42.0 wasn't actually loaded correctly. So the FP constant load is wrong.

Let me check: `get()` returns 42.0. Inside get, it loads the constant 42.0 via VT_CONST + VT_LVAL + VT_SYM (or perhaps it's a VT_CONST without sym since 42.0 is a literal).

Actually wait — fp4 returned 7. That test stored `double y=x+1.0`. The 1.0 had to be loaded as a constant. If the FP add was getting garbage, the assignment wouldn't matter (since we don't return y). Let me check:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fp8.c <<EOF\nint chk(double d){if(d>41.5)if(d<42.5)return 99;return 1;}\nint main(void){return chk(42.0);}\nEOF\n./tcc -B. -run /tmp/fp8.c; echo \"fp8 exit=$?\"\n/usr/local/bin/otool -tV /tmp/fp8.c.o 2>/dev/null\n./tcc -B. -c -o /tmp/fp8.o /tmp/fp8.c\n/usr/local/bin/otool -tV /tmp/fp8.o'",
  "description": "Test FP constant arg passing",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

The disasm shows the CALL passes the FP value correctly: lis r12 + lfd f1, then bl _chk. In _chk, f1 is stfd to 0x18(r1) (param area), then the body lfd's it back from 0x18(r31). 

But the comparison fails (returns 1). So the constant 41.5 is being loaded incorrectly. Look at line 0x5c: `lfd f2, lo16(_L.0)(r12)`. r12 = `lis ha16(_chk)`. So r12 holds the high 16 bits of `_chk`'s address (adjusted).

Wait — `lis r12, ha16(_chk)` is wrong! It should be `lis r12, ha16(_L.0)`. The HA reloc is bound to `_chk` instead of `_L.0`. The relocator just does `addr = symbol_addr + addend` and the bytes for HA reloc currently target the lis, which has 16 bits of zero — so it gets set to ha16(_chk_addr).

So the issue is otool's symbolic display? Let me check actual relocations. Actually otool may just be showing the wrong nearest symbol. The two relocations may both correctly target _L.0, but otool labels HA by the closest symbol BEFORE the address.

Actually wait — these are LIS+LFD pairs for the LOAD. Otool's labeling scheme: it probably looks up the relocation entry. If reloc says "this ha16 immediate refers to symbol X with addend 0", otool prints "ha16(X)". The fact that otool says "ha16(_chk)" but "lo16(_L.0)" means the HA reloc DOES target _chk (not _L.0). That's the bug.

So my code emits the HA reloc with the wrong sym. Let me re-check:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "offset": 468,
  "limit": 90
}


---

## 🤖 Claude (Subagent)

The code looks right — both relocs go to `sv->sym`. Let me verify by checking the reloc table for the .o file:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && /usr/local/bin/otool -rv /tmp/fp8.o'",
  "description": "Show relocations"
}


---

## 🤖 Claude (Subagent)

AHA. The relocations are NON-SCATTERED relocations to the SECTION (__TEXT,__const). So the address calculated is just `section_base + addend`. But the addend stored in the instruction word is currently 0 — so they all resolve to section_base + 0, which is the FIRST literal. But there are multiple literals at different offsets. The HA/LO relocations should use addend = literal_offset_within_section.

Wait — but all 3 LO16 reloc instructions show "half = 0x0000" — meaning the implicit half field is 0. But our LO field SHOULD contain the offset (or it's encoded in the relocation's addend somehow).

This is actually the issue with how Mach-O scattered vs non-scattered relocations work, AND it's possibly a tcc Mach-O writer bug. Let me check ppc-macho.c:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-macho.c"
}


---

## 🤖 Claude (Subagent)

OK. The .o file output is what we'd be linking. But the test uses `-run` which runs in tccrun (in-memory). Let me focus on tccrun:

Actually, `-run` uses tccelf-style relocs, not our Mach-O writer. Let me check tccrun. The relocations are applied via `relocate()` (from ppc-link.c). And in tccrun, the symbol is resolved via tcc's normal symbol-resolve path. The reloc takes addend from the existing instruction bits + uses the symbol's resolved address.

So both code paths use ppc-link.c's `relocate()`. Let me check our relocate() — when ELF Rel format is used (no separate addend field), the addend is taken from the bytes themselves at the relocation site:

Looking back at ppc-link.c's relocate():
```
case R_PPC_ADDR16_HA: {
    uint16_t ha = ((val + 0x8000) >> 16) & 0xffff;
```

Here `val` is the symbol's resolved address. There's NO addend addition. But for ELF Rel (not Rela), the addend is the original instruction bits at the patch site. We initialize them to 0 in our greloc-then-emit pattern, so the addend is 0.

But for FP literals, each constant gets its own anonymous symbol pointing at its specific offset within rodata. So the symbol's address IS the literal's address — addend 0 should work.

Wait. Let me re-check `get_sym_ref`:

```c
ST_FUNC Sym *get_sym_ref(CType *type, Section *sec, unsigned long offset, unsigned long size)
{
    int v;
    Sym *sym;

    v = anon_sym++;
    sym = sym_push(v, type, VT_CONST | VT_SYM, 0);
    sym->type.t |= VT_STATIC;
    put_extern_sym(sym, sec, offset, size);
    return sym;
}
```

`put_extern_sym` puts an Elf symbol with `st_value = offset`. So the symbol's address (after relocation) is `section_base + offset`.

Now in tccrun, when resolving: it looks up the symbol's value as `sec->sh_addr + st_value` — which is the in-memory absolute address of the literal. So `val` in `relocate()` is correct.

Then `R_PPC_ADDR16_HA`: writes `((val + 0x8000) >> 16) & 0xffff` to the lis. That's the high half. lis encoded value gets multiplied by 0x10000 by the CPU. R_PPC_ADDR16_LO writes `val & 0xffff` to the lfd. The lfd computes `r12 + (sign-extended lo16(val))`. Combined: r12 (= ha16(val) * 0x10000) + sign-extended lo16(val) = val (because of HA's sign-extension correction).

So the math should work. But fp7 returned 1, meaning the literal load gave wrong value.

Wait — let me check whether sh_addr is correctly populated by tccrun. Let me look:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"sh_addr\\|relocate_sections\\|tcc_run\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccrun.c | head -30",
  "description": "Find sh_addr usage in tccrun"
}


---

## 🤖 Claude (Subagent)

So sh_addr is set BEFORE relocate_sections is called. Good. Let me check the actual relocation by adding debug output. Actually, simpler: let me check whether the issue is something else entirely. Let me dump the rodata bytes of fp8 and check what the literal pool looks like:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && /usr/local/bin/otool -s __TEXT __const /tmp/fp8.o'",
  "description": "Dump rodata contents"
}


---

## 🤖 Claude (Subagent)

Aha — the literal bytes are wrong! 41.5 as double IEEE = 0x404f000000000000. But the dump shows `00000000 00c04440` for the first literal. Read big-endian = 0x0000_0000_00c0_4440 = ... wait that doesn't match any sane double.

But actually otool prints data in groups of 4 bytes, but each WORD might be displayed in HOST byte order. Let me decode considering they may be 32-bit little-endian halves of double.

Actually looking at "00000000 00c04440" — if this is two 32-bit big-endian words, the first word = 0x00000000 and second = 0x00c04440. That's a double 0x00c04440_00000000 which is roughly 5.8e-310 — wrong.

But if these are stored as little-endian halves of a 64-bit double, then the actual bytes in memory at offset 0..7 are: `00 00 00 00 00 c0 44 40`. As a little-endian double: 0x4044c00000000000 = 41.5. YES! That's correct.

But PPC reads these bytes in big-endian order. The lfd at memory offset 0 would interpret bytes 0..7 as a big-endian double = 0x0000000000c04440 — bogus.

So the bug is: tcc's tccgen places FP literals in rodata as host-byte-order doubles. Our PPC backend needs to swap them when targeting big-endian.

Let me check tccgen for where this happens. If tcc's host is x86_64 (LE), then `c.d` is a host double (LE-bytes). When tcc writes the literal to rodata, it's writing LE bytes. PPC reads them as BE. Bad.

Wait — but `-run` runs in-memory on the SAME host that compiled. If tcc is running on imacg3 (a PPC, BE), then host bytes ARE big-endian.

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'uname -m; file /Users/macuser/tcc-darwin8-ppc/tcc/tcc'",
  "description": "Check tcc executable arch"
}


---

## 🤖 Claude (Subagent)

TCC is a PPC binary running on a PPC machine. Host endianness is BIG. So `c.d` should be in BE host order.

But the otool output shows `00000000 00c04440` which decoded as BE = bogus. Decoded as LE = 41.5. So the bytes are actually LE in the file? But host is BE!

Wait, otool is showing memory hex dump — what the bytes actually are on disk. Let me re-examine. Otool shows hex words: "00000000 00c04440". Each "word" = 4 hex bytes. So the actual byte sequence in memory at offset 0 is: 00 00 00 00 00 c0 44 40.

If host BE writes a double 41.5 = 0x404f000000000000, the bytes in memory should be: 40 4f 00 00 00 00 00 00. That's NOT what's there.

Hmm. Otool showed offsets like `00000118` — let me recheck the rodata dump. 

Wait! Otool renders 32-bit words. On Mac OS X 10.4 PPC, otool used to display 32-bit BE words AS BE numbers. So "00000000 00c04440" = first BE word 0x00000000, second BE word 0x00c04440. But this is in DATA section — and these are literal bytes just dumped.

Otool's `-s` displays the bytes as 32-bit integers in NATIVE endianness... but on PPC native = BE. So they should display as BE-encoded.

Let me check what 41.5 in IEEE double actually is. 41.5 = 0x4044800000000000 (let me recompute). 41.5 = 32 + 8 + 1.5 = 41.5. In IEEE double: sign=0, exponent = 1023+5 = 1028 = 0x404 (5 because 41.5 = 1.296875 * 2^5 — no, 41.5 in binary is 101001.1 = 1.010011 * 2^5). exponent = 1023+5 = 1028 = 0b10000000100. Mantissa = 010011_followed_by_46_zeros. So bits = 0_10000000100_0100110000... = 0x4044C00000000000.

Wait let me redo: 41.5 = 0010_1001.1 binary = 1.0100110 * 2^5. Mantissa bits after leading 1: 0100110_followed by zeros. Stored as 52-bit mantissa: 0100110000000...0 (52 bits).

Sign bit (1) + exponent (11) + mantissa (52) = 64 bits.
= 0 | 10000000100 | 0100110000000000000000000000000000000000000000000000

Let me group as nibbles:
0100_0000_0100_0100_1100_0000_0000_0000... = 0x4044_C000_0000_0000.

So 41.5 as BE double = 40 44 C0 00 00 00 00 00.

The dump shows `00000000 00c04440` — that's 8 bytes: 00 00 00 00 00 c0 44 40.

Reverse those 8 bytes: 40 44 c0 00 00 00 00 00 — exactly 41.5 BE encoding!

So the bytes ARE stored in REVERSE byte order. tccgen is writing the LE bytes of the double into the rodata section.

This means tccgen is NOT byteswapping FP constants for cross-compilation, or this tcc version has some inconsistency between host and target endianness.

Let me check init_putv or similar:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"init_putv\\|store_packed_bf\\|VT_DOUBLE\\|c\\.d\\|c\\.f\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | grep -i \"init\\|put\\|write\" | head -20",
  "description": "Find init_putv"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 1880,
  "limit": 25
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 7762,
  "limit": 120
}


---

## 🤖 Claude (Subagent)

Found it! Lines 7875-7879: tcc uses `write32le` and `write64le` for FP literals UNCONDITIONALLY. This is a known limitation — tcc was hard-coded for little-endian target machines.

We need to fix init_putv (or work around it). The cleanest fix is in tccgen.c, but that's invasive. Alternatively we can detect this in our backend and byte-swap on load — but that requires runtime swaps which is ugly.

Actually the cleanest fix: change tccgen.c to use a portable write that uses target endianness. Let me see if there's an existing concept of TARGET_ENDIAN or similar.

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"TARGET_BIG\\|BIG_ENDIAN\\|write32be\\|write64be\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tcc.h /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c | head -20",
  "description": "Find target endian config"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"write32le\\|write64le\\|write16le\\|writele\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tcc.h | head -10",
  "description": "Find write*le helpers"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tcc.h",
  "offset": 1655,
  "limit": 35
}


---

## 🤖 Claude (Subagent)

So `write32le` writes the LSB first regardless of host. Since the host is BE, writing 0x4044C000_00000000 with write64le puts byte pattern: 00 00 00 00 00 C0 44 40 — exactly what we observed.

The simplest fix is to add target-aware write32be/write64be in init_putv when the target is BE. Let me modify init_putv to detect PPC and byte-swap.

Actually the cleanest approach: change init_putv to use write32be/write64be when the target is big-endian. We can add a config macro like `TCC_TARGET_BE`:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"TCC_TARGET_PPC\\|TCC_TARGET_PPC32\\|TCC_TARGET_POWER\" /Users/cell/claude/tcc-darwin8-ppc/tcc/*.h /Users/cell/claude/tcc-darwin8-ppc/tcc/*.c | head -10",
  "description": "Find PPC target macro"
}


---

## 🤖 Claude (Subagent)

Good, TCC_TARGET_PPC exists. Let me modify init_putv to byte-swap FP literals for PPC. The cleanest place is in tccgen.c init_putv. Let me do that:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "offset": 7864,
  "limit": 40
}


---

## 🤖 Claude (Subagent)

Hmm — modifying tccgen.c is invasive but necessary. Let me add minimal target-aware writes. We need to define BE writers for FP, and use them when target is BE. Or: simpler still, byte-swap in init_putv when TCC_TARGET_PPC.

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "old_string": "\t    case VT_FLOAT:\n                write32le(ptr, val);\n\t\tbreak;\n\t    case VT_DOUBLE:\n                write64le(ptr, val);\n\t\tbreak;",
  "new_string": "\t    case VT_FLOAT:\n#if defined TCC_TARGET_PPC\n                /* PPC is big-endian: write bytes MSB-first so lfs reads them\n                 * as a properly-formed IEEE single. */\n                {\n                    unsigned char *q = (unsigned char *)ptr;\n                    uint32_t v = (uint32_t)val;\n                    q[0] = (v >> 24) & 0xff;\n                    q[1] = (v >> 16) & 0xff;\n                    q[2] = (v >> 8)  & 0xff;\n                    q[3] = v & 0xff;\n                }\n#else\n                write32le(ptr, val);\n#endif\n\t\tbreak;\n\t    case VT_DOUBLE:\n#if defined TCC_TARGET_PPC\n                {\n                    unsigned char *q = (unsigned char *)ptr;\n                    uint64_t v = val;\n                    q[0] = (v >> 56) & 0xff;\n                    q[1] = (v >> 48) & 0xff;\n                    q[2] = (v >> 40) & 0xff;\n                    q[3] = (v >> 32) & 0xff;\n                    q[4] = (v >> 24) & 0xff;\n                    q[5] = (v >> 16) & 0xff;\n                    q[6] = (v >> 8)  & 0xff;\n                    q[7] = v & 0xff;\n                }\n#else\n                write64le(ptr, val);\n#endif\n\t\tbreak;"
}


---

## 🤖 Claude (Subagent)

Wait — the `val` variable is filled from `vtop->c.i`. Let me check whether for VT_FLOAT the data is in `c.f` vs `c.i` for the 32-bit interpretation. Let me trace... in the original code, write32le takes val (uint64_t) and writes the low 32 bits. That suggests the union member c.i (int64) and c.f (float) overlap such that the bits are consistent. Let me check:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"CValue\\|c\\.i;\\|c\\.f;\\|c\\.d;\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tcc.h | head -30",
  "description": "Find CValue union"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tcc.h",
  "offset": 483,
  "limit": 15
}


---

## 🤖 Claude (Subagent)

OK so `c.i` is uint64_t, `c.f` is float, `c.d` is double — all in the same union. So `val = vtop->c.i` reads the bytes that were stored as `f` or `d`. write32le takes the low 32 bits of val and writes them — the low 32 bits in HOST byte order are actually the FIRST 4 bytes of the union (on little-endian) or the LAST 4 bytes (on big-endian).

Hmm wait. On a BE host, `vtop->c.f` writes bytes 0..3 (the BE float). `vtop->c.i` reads as uint64_t — bytes 0..7. The low 32 bits of the uint64_t are bytes 4..7 on BE host. So `(uint32_t)val` gets bytes 4..7, NOT the float's bytes (which are at 0..3). That's a BUG in the existing code on BE hosts.

So the issue is more fundamental than I thought. We need to read from `c.f` for VT_FLOAT and `c.d` for VT_DOUBLE.

Let me redo the patch:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c",
  "old_string": "\t    case VT_FLOAT:\n#if defined TCC_TARGET_PPC\n                /* PPC is big-endian: write bytes MSB-first so lfs reads them\n                 * as a properly-formed IEEE single. */\n                {\n                    unsigned char *q = (unsigned char *)ptr;\n                    uint32_t v = (uint32_t)val;\n                    q[0] = (v >> 24) & 0xff;\n                    q[1] = (v >> 16) & 0xff;\n                    q[2] = (v >> 8)  & 0xff;\n                    q[3] = v & 0xff;\n                }\n#else\n                write32le(ptr, val);\n#endif\n\t\tbreak;\n\t    case VT_DOUBLE:\n#if defined TCC_TARGET_PPC\n                {\n                    unsigned char *q = (unsigned char *)ptr;\n                    uint64_t v = val;\n                    q[0] = (v >> 56) & 0xff;\n                    q[1] = (v >> 48) & 0xff;\n                    q[2] = (v >> 40) & 0xff;\n                    q[3] = (v >> 32) & 0xff;\n                    q[4] = (v >> 24) & 0xff;\n                    q[5] = (v >> 16) & 0xff;\n                    q[6] = (v >> 8)  & 0xff;\n                    q[7] = v & 0xff;\n                }\n#else\n                write64le(ptr, val);\n#endif\n\t\tbreak;",
  "new_string": "\t    case VT_FLOAT:\n#if defined TCC_TARGET_PPC\n                /* PPC is big-endian. Use c.f directly (the actual host\n                 * float bytes) and write MSB-first.  Note: the \"low 32\n                 * bits of val\" trick used elsewhere doesn't work on a\n                 * big-endian host because the low 4 bytes of a uint64_t\n                 * union member are the OPPOSITE 4 bytes from the float. */\n                {\n                    union { float f; uint32_t u; } cv;\n                    unsigned char *q = (unsigned char *)ptr;\n                    cv.f = vtop->c.f;\n                    q[0] = (cv.u >> 24) & 0xff;\n                    q[1] = (cv.u >> 16) & 0xff;\n                    q[2] = (cv.u >> 8)  & 0xff;\n                    q[3] = cv.u & 0xff;\n                }\n#else\n                write32le(ptr, val);\n#endif\n\t\tbreak;\n\t    case VT_DOUBLE:\n#if defined TCC_TARGET_PPC\n                {\n                    union { double d; uint64_t u; } cv;\n                    unsigned char *q = (unsigned char *)ptr;\n                    cv.d = vtop->c.d;\n                    q[0] = (cv.u >> 56) & 0xff;\n                    q[1] = (cv.u >> 48) & 0xff;\n                    q[2] = (cv.u >> 40) & 0xff;\n                    q[3] = (cv.u >> 32) & 0xff;\n                    q[4] = (cv.u >> 24) & 0xff;\n                    q[5] = (cv.u >> 16) & 0xff;\n                    q[6] = (cv.u >> 8)  & 0xff;\n                    q[7] = cv.u & 0xff;\n                }\n#else\n                write64le(ptr, val);\n#endif\n\t\tbreak;"
}


---

## 🤖 Claude (Subagent)

Now sync and rebuild:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "rsync -az /Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c /Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c imacg3:~/tcc-darwin8-ppc/tcc/ && ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && touch ppc-gen.c tccgen.c && /opt/make-4.3/bin/make 2>&1 | tail -8'",
  "description": "Rebuild with tccgen fix",
  "timeout": 120000
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && ./tcc -B. -run /tmp/fp1.c; echo \"fp1 exit=$?\"; ./tcc -B. -run /tmp/fp2.c; echo \"fp2 exit=$?\"; ./tcc -B. -run /tmp/fp3.c; echo \"fp3 exit=$?\"; ./tcc -B. -run /tmp/fp4.c; echo \"fp4 exit=$?\"; ./tcc -B. -run /tmp/fp5.c; echo \"fp5 exit=$?\"; ./tcc -B. -run /tmp/fp6.c; echo \"fp6 exit=$?\"; ./tcc -B. -run /tmp/fp7.c; echo \"fp7 exit=$?\"; ./tcc -B. -run /tmp/fp8.c; echo \"fp8 exit=$?\"'",
  "description": "Run all FP tests",
  "timeout": 90000
}


---

## 🤖 Claude (Subagent)

All FP tests pass. Now run the regression suite again to make sure we didn't break anything:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && for f in h1 h2 h3 h4; do ./tcc -B. -run /tmp/$f.c; echo \"$f exit=$?\"; done'",
  "description": "Re-run regression tests",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

All existing tests still pass. Let me run a broader FP test set to make sure our coverage is solid:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fpall.c <<EOF\n/* float arithmetic */\nfloat fadd(float a,float b){return a+b;}\nfloat fsub(float a,float b){return a-b;}\nfloat fmul(float a,float b){return a*b;}\nfloat fdiv(float a,float b){return a/b;}\n/* double arithmetic */\ndouble dadd(double a,double b){return a+b;}\ndouble dsub(double a,double b){return a-b;}\ndouble dmul(double a,double b){return a*b;}\ndouble ddiv(double a,double b){return a/b;}\n/* compares */\nint dlt(double a,double b){return a<b;}\nint dgt(double a,double b){return a>b;}\nint dle(double a,double b){return a<=b;}\nint dge(double a,double b){return a>=b;}\nint deq(double a,double b){return a==b;}\nint dne(double a,double b){return a!=b;}\n/* unary neg */\ndouble dneg(double a){return -a;}\n/* int<->fp */\ndouble i2d(int x){return (double)x;}\nint d2i(double x){return (int)x;}\n/* float<->double */\ndouble f2d(float x){return (double)x;}\nfloat d2f(double x){return (float)x;}\n\nint main(void){\n  int errs=0;\n  if (fadd(1.5f,2.5f)!=4.0f) errs|=1;\n  if (fsub(5.0f,3.0f)!=2.0f) errs|=2;\n  if (fmul(2.0f,3.0f)!=6.0f) errs|=4;\n  if (fdiv(8.0f,2.0f)!=4.0f) errs|=8;\n  if (dadd(1.5,2.5)!=4.0)    errs|=16;\n  if (dsub(5.0,3.0)!=2.0)    errs|=32;\n  if (dmul(2.0,3.0)!=6.0)    errs|=64;\n  if (ddiv(8.0,2.0)!=4.0)    errs|=128;\n  if (!dlt(1.0,2.0))         errs|=256;\n  if (!dgt(2.0,1.0))         errs|=512;\n  if (dlt(2.0,1.0))          errs|=1024;\n  if (!deq(1.0,1.0))         errs|=2048;\n  if (deq(1.0,2.0))          errs|=4096;\n  if (dneg(3.0)!=-3.0)       errs|=8192;\n  if (i2d(42)!=42.0)         errs|=16384;\n  if (d2i(42.7)!=42)         errs|=32768;\n  if (f2d(1.5f)!=1.5)        errs|=65536;\n  if (d2f(1.5)!=1.5f)        errs|=131072;\n  return errs;\n}\nEOF\n./tcc -B. -run /tmp/fpall.c; echo \"fpall exit=$?\"'",
  "description": "Run comprehensive FP test",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

A new path: `sv->r=0x33` = VT_LOCAL(0x32) | VT_SYM(... no, 0x33 = 0x30 | 0x03? Let me decode: 0x33 = 0x32 | 0x01? VT_LOCAL is 0x32. 0x33 = 0x32 + 1, but VT_VALMASK is 0x3f. Hmm. Type 0x3 = VT_LLONG.

Looking back at VT_VALMASK: 0x003f. So `v = 0x33 & 0x3f = 0x33`. And VT_LOCAL = 0x32. So 0x33 is NOT VT_LOCAL — it's some other value in the VALMASK range.

Wait, 0x33 = 51 decimal. Beyond all standard VT_ values. Let me check:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"^#define VT_\" /Users/cell/claude/tcc-darwin8-ppc/tcc/tcc.h | head -20",
  "description": "List VT_ values"
}


---

## 🤖 Claude (Subagent)

0x33 = VT_CMP. So load() is being asked to materialize a VT_CMP into r=0 (a GPR). For `int dlt(double a, double b){return a<b;}` — the comparison goes via gen_opf with TOK_LT, which sets vtop to VT_CMP. Then we need to load it into RC_INT (=r3) for the return. tccgen's load(VT_CMP) path is supposed to be implemented by us — typically translates the CMP into a 0/1 boolean.

We need to handle VT_CMP in load(). Let me look at what we need:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "grep -n \"VT_CMP\" /Users/cell/claude/tcc-darwin8-ppc/tcc/arm-gen.c | head -20",
  "description": "Find VT_CMP handling in arm"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/arm-gen.c",
  "offset": 685,
  "limit": 10
}


---

## 🤖 Claude (Subagent)

OK. For PPC we need to convert the CR bit into a 0/1 boolean. For an integer result of an FP comparison, we can:

1. Materialize via branch: `bc skip; li r=1; b end; skip: li r=0; end:` — slow.
2. Use mfcr + extraction.

Simpler: branch-based. Let me write a VT_CMP load handler. The existing `gjmp_cond` handles VT_CMP for branching but here we need a register value.

Actually we already have a `gjmp_cond` infrastructure. We can do: emit a sequence like `li r, 1; bc <op>, .+8; li r, 0`. The condition codes were already set by gen_opi/gen_opf.

Let me look at the BO/BI mapping logic in gjmp_cond to reuse:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Value already in a register slot (no VT_LVAL): move-register.\n     * tcc requests this when forcing a value into a specific register\n     * class (e.g. RC_IRET) where it currently lives in some other\n     * register. */",
  "new_string": "    /* VT_CMP: condition codes are live in cr0; tcc wants a 0/1 int\n     * value in `r`. We emit:\n     *     li  rD, 1\n     *     bc  <take>, BI, .+8     ; if condition holds, skip the li\n     *     li  rD, 0\n     * Map (op) -> (BO, BI) the same way gjmp_cond does. */\n    if (v == VT_CMP) {\n        int take_bo, bi;\n        int op = (int)sv->c.i;\n        int dst_gpr;\n        if (IS_FREG(r))\n            tcc_error(\"ppc-gen: VT_CMP into FP register?\");\n        dst_gpr = TREG_TO_GPR(r);\n        switch (op) {\n        case TOK_EQ:                take_bo = 12; bi = 2; break;\n        case TOK_NE:                take_bo = 4;  bi = 2; break;\n        case TOK_LT: case TOK_ULT:  take_bo = 12; bi = 0; break;\n        case TOK_GE: case TOK_UGE:  take_bo = 4;  bi = 0; break;\n        case TOK_GT: case TOK_UGT:  take_bo = 12; bi = 1; break;\n        case TOK_LE: case TOK_ULE:  take_bo = 4;  bi = 1; break;\n        default:\n            tcc_error(\"ppc-gen: VT_CMP load with op 0x%x\", op);\n            return;\n        }\n        /* li rD, 1   (addi rD, 0, 1) */\n        o(0x38000001 | (dst_gpr << 21));\n        /* bc take_bo, bi, .+8 */\n        o(0x40000000u | (take_bo << 21) | (bi << 16) | 8);\n        /* li rD, 0 */\n        o(0x38000000 | (dst_gpr << 21));\n        return;\n    }\n\n    /* Value already in a register slot (no VT_LVAL): move-register.\n     * tcc requests this when forcing a value into a specific register\n     * class (e.g. RC_IRET) where it currently lives in some other\n     * register. */"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "rsync -az /Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c imacg3:~/tcc-darwin8-ppc/tcc/ && ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && touch ppc-gen.c && /opt/make-4.3/bin/make 2>&1 | tail -5'",
  "description": "Rebuild",
  "timeout": 120000
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && ./tcc -B. -run /tmp/fpall.c; echo \"fpall exit=$?\"'",
  "description": "Run comprehensive FP test",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

All FP tests pass. Run regression once more:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && for f in h1 h2 h3 h4 fp1 fp2 fp3 fp4 fp5 fp6 fp7 fp8 fpall; do ./tcc -B. -run /tmp/$f.c; echo \"$f exit=$?\"; done'",
  "description": "Final regression sweep",
  "timeout": 120000
}


---

## 🤖 Claude (Subagent)

All 13 tests pass. Let me also test int<->fp comparisons in conditions (used in real code):

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fpcond.c <<EOF\nint main(void){\n  double x=2.5;\n  if (x<3.0) return 7;\n  return 99;\n}\nEOF\n./tcc -B. -run /tmp/fpcond.c; echo \"fpcond exit=$?\"\ncat > /tmp/fpsum.c <<EOF\ndouble sum(int n){double s=0.0;int i;for(i=1;i<=n;i++)s=s+(double)i;return s;}\nint main(void){return (int)sum(10);}\nEOF\n./tcc -B. -run /tmp/fpsum.c; echo \"fpsum exit=$?\"\ncat > /tmp/fpdiv.c <<EOF\nint main(void){double x=22.0/7.0;return (int)(x*100);}\nEOF\n./tcc -B. -run /tmp/fpdiv.c; echo \"fpdiv exit=$?\"'",
  "description": "More FP edge cases",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

fpcond and fpdiv pass (314 mod 256 = 58 — exit code is 8-bit). fpsum returns 0 (expected 55). The loop `i=1;i<=n;i++` with `s=s+(double)i` — uses int->fp conversion in a loop. Let me debug:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fpsum2.c <<EOF\nint main(void){double s=0.0;double a=(double)5;s=s+a;return (int)s;}\nEOF\n./tcc -B. -run /tmp/fpsum2.c; echo \"fpsum2 exit=$?\"\ncat > /tmp/fpsum3.c <<EOF\nint main(void){double s=0.0;int i=1;s=s+(double)i;return (int)s;}\nEOF\n./tcc -B. -run /tmp/fpsum3.c; echo \"fpsum3 exit=$?\"'",
  "description": "Test int-to-fp conversion in expression",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

Interesting — fpsum2 (compile-time int constant 5 cast to double) works, but fpsum3 (runtime int variable) fails. The runtime int->fp conversion must have a bug.

Let me trace: in fpsum3, `(double)i` — i is at a stack location, loaded as int, converted to double. Let me look at the disasm:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && ./tcc -B. -c -o /tmp/fpsum3.o /tmp/fpsum3.c && /usr/local/bin/otool -tV /tmp/fpsum3.o'",
  "description": "Disassemble fpsum3"
}


---

## 🤖 Claude (Subagent)

Look at the int->fp conversion at 0x6c-0x88:
- 0x6c xoris r0, r3, 0x8000 — flip sign bit, store in r0
- 0x70 lis r12, 0x4330 — high half magic
- 0x74 stw r12, 24(r1)
- 0x78 stw r0, 28(r1) 
- 0x7c lfd f1, 24(r1) — load combined as double
- 0x80 lis r12, ha16(...) — load magic constant
- 0x84 lfd f1, lo16(_L.1)(r12) — overwrites f1 with magic!
- 0x88 fsub f1, f1, f1 — subtract magic from itself = 0

The bug: my code allocates dst_slot first (gets f1), then loads magic into fpr_tmp. But in this gen flow, the magic load OVERWRITES f1 (the destination!) because both ended up as the same FPR slot.

Wait — let me re-read my code:

```c
dst_slot = get_reg(RC_FLOAT);  // returns slot 10 (f1)
fpr_dst = TREG_TO_FPR(dst_slot); // f1
tmp_slot = get_reg(RC_FLOAT);  // wants ANOTHER slot, but vtop[dst_slot] not set yet?
fpr_tmp = TREG_TO_FPR(tmp_slot); // could also be f1 if dst_slot wasn't reserved
```

The issue is that `get_reg` for tmp_slot doesn't see dst_slot as taken because we haven't pushed vtop yet. Let me look at how arm64 does this — it does vtop++ first then get_reg.

Let me fix the allocator order:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Materialize the int operand into a GPR. */\n    gv(RC_INT);\n    gpr_int = TREG_TO_GPR(vtop->r);\n\n    /* Allocate destination FPR. */\n    vtop--;\n    dst_slot = get_reg(RC_FLOAT);\n    fpr_dst = TREG_TO_FPR(dst_slot);\n    /* Allocate a second FPR for the magic constant. */\n    tmp_slot = get_reg(RC_FLOAT);\n    fpr_tmp = TREG_TO_FPR(tmp_slot);\n    vtop++;\n    vtop->r = dst_slot;\n    vtop->type.t = t;",
  "new_string": "    /* Materialize the int operand into a GPR. */\n    gv(RC_INT);\n    gpr_int = TREG_TO_GPR(vtop->r);\n\n    /* Allocate destination FPR. Push it on vtop first so the second\n     * get_reg won't return the same FPR. */\n    vtop--;\n    dst_slot = get_reg(RC_FLOAT);\n    fpr_dst = TREG_TO_FPR(dst_slot);\n    vtop++;\n    vtop->r = dst_slot;\n    vtop->type.t = t;\n    /* Allocate a second FPR for the magic constant -- now that\n     * dst_slot is on the vstack, get_reg will avoid it. */\n    tmp_slot = get_reg(RC_FLOAT);\n    fpr_tmp = TREG_TO_FPR(tmp_slot);"
}


---

## 🤖 Claude (Subagent)

Same fix for ftoi:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Edit
Input: {
  "replace_all": false,
  "file_path": "/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c",
  "old_string": "    /* Materialize FP operand. */\n    gv(RC_FLOAT);\n    fpr_src = TREG_TO_FPR(vtop->r);\n\n    /* Allocate result GPR. */\n    vtop--;\n    dst_slot = get_reg(RC_INT);\n    gpr_dst = TREG_TO_GPR(dst_slot);\n    /* Allocate scratch FPR for the converted value (don't clobber the\n     * source if it's still live downstream). */\n    tmp_slot = get_reg(RC_FLOAT);\n    fpr_tmp = TREG_TO_FPR(tmp_slot);\n    vtop++;\n    vtop->r = dst_slot;\n    vtop->type.t = t;",
  "new_string": "    /* Materialize FP operand. */\n    gv(RC_FLOAT);\n    fpr_src = TREG_TO_FPR(vtop->r);\n\n    /* Allocate result GPR; push it onto vtop so the next get_reg\n     * for the FP scratch doesn't return our source FPR. */\n    vtop--;\n    dst_slot = get_reg(RC_INT);\n    gpr_dst = TREG_TO_GPR(dst_slot);\n    vtop++;\n    vtop->r = dst_slot;\n    vtop->type.t = t;\n    /* Allocate scratch FPR for the converted value. */\n    tmp_slot = get_reg(RC_FLOAT);\n    fpr_tmp = TREG_TO_FPR(tmp_slot);"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "rsync -az /Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c imacg3:~/tcc-darwin8-ppc/tcc/ && ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && touch ppc-gen.c && /opt/make-4.3/bin/make 2>&1 | tail -3 && for f in h1 h2 h3 h4 fp1 fp2 fp3 fp4 fp5 fp6 fp7 fp8 fpall fpcond fpsum fpdiv fpsum2 fpsum3; do ./tcc -B. -run /tmp/$f.c; echo \"$f exit=$?\"; done'",
  "description": "Rebuild and full test sweep",
  "timeout": 180000
}


---

## 🤖 Claude (Subagent)

All tests pass — 18/18. fpsum=55 (sum of 1..10) confirms int->fp in a loop works. fpsum3=1 (single iteration sum 0+1=1). Excellent.

Let me also verify with a slightly more demanding program that uses FP args end-to-end:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ssh imacg3 'cd ~/tcc-darwin8-ppc/tcc && cat > /tmp/fpall2.c <<EOF\ndouble poly(double a,double b,double c,double x){return a*x*x+b*x+c;}\nint mix(int x,double y,int z){return x+(int)y+z;}\ndouble pi(void){return 3.14159265358979;}\nint main(void){\n  if ((int)(poly(1.0,2.0,3.0,4.0))!=27) return 1;  /* 16+8+3=27 */\n  if (mix(10,3.7,20)!=33) return 2;\n  if ((int)(pi()*100)!=314) return 3;\n  return 42;\n}\nEOF\n./tcc -B. -run /tmp/fpall2.c; echo \"fpall2 exit=$?\"'",
  "description": "More demanding FP test",
  "timeout": 60000
}


---

## 🤖 Claude (Subagent)

End-to-end success. Final summary:

## Summary

Added 32-bit single + 64-bit double FP codegen to `/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c`. Also fixed one host-endian bug in `/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c`.

**What works** (all 18 of 18 test programs pass end-to-end via `tcc -run`):
- FP load/store: `lfs`/`lfd`/`stfs`/`stfd` for VT_LOCAL, pointer-deref, and rodata-symbol forms.
- FP constants from rodata via `lis` + `lfs/lfd` with `R_PPC_ADDR16_HA` / `R_PPC_ADDR16_LO` reloc pairs.
- Binary FP ops `+ - * /` in both single (`fadds`, etc., opcode 59) and double (`fadd`, etc., opcode 63), with the fmul/fmuls C-field quirk (operand at bits 21..25 not 11..15).
- Unary `TOK_NEG` via `fneg`.
- FP comparisons via `fcmpu cr0, fA, fB` followed by `vset_VT_CMP(op)`; also added a `VT_CMP -> int register` materialization (`li r,1; bc skip; li r,0`) so comparison results land in GPRs for `int x = (a < b)` style code.
- `gen_cvt_itof`: classic magic-double trick (write 0x43300000 high half + xor'd low half to a scratch slot, lfd, fsub the magic, optionally frsp for float). Signed and unsigned 32-bit ints supported.
- `gen_cvt_ftoi`: `fctiwz` to a temp FPR, `stfd` to scratch, `lwz` low word.
- `gen_cvt_ftof`: `frsp` for double→float; nop for float→double (PPC FPRs are always 64-bit internally).
- FP argument passing per Apple PPC ABI: first 8 FP args go in f1..f8; each FP arg also "shadows" 1 (float) or 2 (double) GPR slots. Caller materializes via `gv(RC_F(slot))`.
- FP parameter unpacking: prolog now spills f1..f8 to the corresponding GPR-shadow address space when the function has FP params; body reads them as `lfs/lfd offset(r31)`. Reserved 8 extra slots in the prolog backfill area; unused slots become nops.

**What aborts (intentional v1 limits):**
- `gen_cvt_itof` with `VT_LLONG` source: errors with "long long to FP conversion not yet implemented".
- `gen_cvt_ftoi` with `VT_LLONG` target: errors similarly.
- Long double treated as alias for double (Apple's double-double not implemented).
- More than 8 FP args.

**Sample end-to-end test that runs:**
```c
double poly(double a,double b,double c,double x){return a*x*x+b*x+c;}
int mix(int x,double y,int z){return x+(int)y+z;}
double pi(void){return 3.14159265358979;}
int main(void){
  if ((int)(poly(1.0,2.0,3.0,4.0))!=27) return 1;
  if (mix(10,3.7,20)!=33) return 2;
  if ((int)(pi()*100)!=314) return 3;
  return 42;
}
```
Returns 42.

**One non-obvious fix in `/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c`:** `init_putv` was using `write32le` / `write64le` for FP literals and reading from `vtop->c.i` (the union's uint64 alias). Since the union member alignment differs by host endianness, on a BE host the low 32 bits of `c.i` are NOT the same bytes as `c.f`. Added a `#ifdef TCC_TARGET_PPC` branch that reads `vtop->c.f` / `vtop->c.d` directly through a typed union and writes BE-ordered bytes. Without this fix every FP literal in rodata was bit-reversed and `lfd` saw garbage.

**Existing regression tests still pass:**
```
return 42;                                     -> exit 42
sq(7)                                          -> exit 49
fib(10)                                        -> exit 55
add64(0x12345678LL, 0x10000000LL)              -> exit 120
```

**Files modified:**
- `/Users/cell/claude/tcc-darwin8-ppc/tcc/ppc-gen.c` (~360 lines added: FP load/store paths in `load`/`store`, `ppc_load_fp_const` helper, full `gen_opf`/`gen_cvt_itof`/`gen_cvt_ftoi`/`gen_cvt_ftof`, VT_CMP→int register path, VT_CONST|VT_SYM symbol-load path, FP arg/param tracking in `gfunc_call`/`gfunc_prolog`/`gfunc_epilog`).
- `/Users/cell/claude/tcc-darwin8-ppc/tcc/tccgen.c` (one PPC-conditional block in `init_putv` for VT_FLOAT/VT_DOUBLE byte ordering, ~30 lines).

---

