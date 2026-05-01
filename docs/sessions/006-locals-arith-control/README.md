# 006 — Locals, arithmetic, branches, comparisons

The big middle slab of integer codegen, plus three critical fixes
that came up along the way.

## Arrival state

After 005, tcc could compile and JIT-run `int main(void) { return N; }`.
Anything beyond constant return aborted at a stub.

## What was added (in ppc-gen.c)

### Frame layout with FP register r31

Apple PPC ABI matches what gcc-4.0 emits: r31 (callee-saved) holds
the frame pointer = OLD_SP. Locals live at NEGATIVE offsets from r31
(matches tcc's `loc` convention from i386). Slot at r31-4 reserved
for saved r31; user locals start at r31-8 (`loc=-4` initially in
gfunc_prolog).

PROLOG_SIZE grew from 12 → 20 bytes (5 instructions):
```
mflr r0
stw  r0, 8(r1)
stwu r1, -frame_size(r1)
stw  r31, frame_size-4(r1)
addi r31, r1, frame_size
```

EPILOG also 20 bytes (5 instructions); restores r31 BEFORE addi r1
(PPC interrupts can clobber memory below r1).

### load() — extended

- VT_CONST integer (already done).
- **VT_LOCAL lvalue read**: `lwz/lhz/lha/lbz` based on `VT_BTYPE`.
  - Sign-extends short/byte when signed (`extsb` / `lha`).
- **VT_LOCAL non-lvalue (address-of)**: `addi rD, r31, offset`.
- **Register-to-register move**: when `sv->r` is already a register
  slot, emit `mr rA, rS` (= `or rA, rS, rS`). Critical for tcc
  forcing values into RC_IRET (r3) for return.

### store() — implemented

- VT_LOCAL of int/ptr: `stw rS, d(r31)`.
- VT_LOCAL of short: `sth`.
- VT_LOCAL of byte: `stb`.

### gen_opi() — full integer ABI

Two encoding shapes:
- XO-form arithmetic (rD at bits 21-25): `+`, `-` (subf with swap),
  `*` (mullw), `/` (divw), unsigned `/` (divwu).
- X-form logical/shift (rA dst at bits 16-20): `&`, `|`, `^`,
  `<<` (slw), `>>` signed (sraw), `>>` unsigned (srw).
- **Comparisons** (TOK_ULT..TOK_GT): emit `cmpw` (signed) or
  `cmplw` (unsigned), then `vset_VT_CMP(op)`. The conditional
  branch comes later via `gjmp_cond`.

### gjmp / gjmp_addr / gjmp_cond / gjmp_append / gsym_addr

Branch chain encoding using PPC `b` (24-bit displacement, opcode 18):
- Encode chain end (target=0) as `b $-PC`, decoding back to 0.
- `gjmp_cond` uses `bc <invert>, .+8 ; b <chain>` so all chain links
  are uniform `b` instructions. Cost: 1 extra bc per conditional;
  benefit: simple chain bookkeeping. Switch to direct `bc` chains if
  we ever hit the 32MB limit (very unlikely).

## Bugs found and fixed (3 of them)

### Bug #1: double `gsym(rsym)` call

`tccgen.c:8549` already calls `gsym(rsym)` before `gfunc_epilog`.
My gfunc_epilog was calling it again. Second walk decoded the
already-patched chain → followed it to a zero word → infinite loop.

**Fix:** removed the gsym(rsym) call from gfunc_epilog. The other
backends never call it either; tccgen owns it.

### Bug #2: Tiger has no `__clear_cache`

PPC has separate I-cache and D-cache. After tccrun.c writes JIT'd
code into a buffer, the I-cache must be invalidated before the CPU
can fetch the new instructions. tccrun.c calls `__clear_cache()`,
expecting it from libgcc. Tiger's gcc-4.0 / libgcc doesn't ship it
(`nm tcc` showed `U ___clear_cache`).

Result: stale I-cache executed garbage / previous functions.
Symptom: every test returned exit 132 (= SIGILL = 128+4).

**Fix:** provide `__clear_cache` in `ppc-macho-stubs.c` using PPC's
`dcbst / sync / icbi / sync / isync` sequence over 32-byte cache
lines. This is the canonical PPC cache-flush idiom; the constant 32
covers G3/G4/G5 and even modern PPCs.

### Bug #3: load() didn't handle "value already in register"

For chained ops like `a + b * 2`, after computing `b * 2` into r3,
tcc needs `a` in some other register. gv2 calls load() to materialize
`a` from VT_LOCAL into a free register, **then** later may call
load() again to move the result into REG_IRET (r3) for the return.
That second call has `sv->r` set to a register slot (not VT_LOCAL,
not VT_CONST). My load() had no case for "value-already-in-register"
and aborted.

Symptom: `7 + 12*2` returned 1 instead of 31.

**Fix:** added a `(sv->r & VT_VALMASK) < VT_CONST && !VT_LVAL` case
that emits `mr rD, rS` (= `or rA, rS, rS`) for ints, `fmr` for
floats.

## Test record

```
locals & assignment:          int s=0; s=s+1; return s;        → 1
multiple locals:              int a=10, b=3; return a+b;       → 13
arithmetic (all 9 ops):       +, -, *, /, &, |, ^, <<, >>      ✓
comparison + if:              if (a>3) return 17; return 99;   → 17 / 99
while loops:
   sum 1..5:                  ans 15                            ✓
   factorial 5:               ans 120                           ✓
   sum 1..10:                 ans 55                            ✓
nested while + if:            count of x>50 in stride 10        → 5
operator precedence:          7+12*2 → 31; 7*12+1 → 85          ✓
register chaining:            7+b*c with three locals → 43      ✓
```

All correct. The test suite is informal but covers the language
surface needed for ~95% of straight-line C code.

## Decisions

- **r31 as frame pointer** (gcc convention on PPC). One callee-saved
  register cost, much simpler addressing than r1+offset with a
  patched frame size.
- **Conditional branches use `bc; b <chain>` trick** instead of
  direct `bc` with chain in the 14-bit BD field. Single chain-link
  encoding regardless of conditional vs unconditional, at the cost
  of 1 extra instruction per conditional. Re-evaluate if function
  size ever pushes the 32MB `b` limit.
- **Cache-flush helper lives in `ppc-macho-stubs.c`** (not in
  ppc-gen.c) because it's an integration concern between tccrun.c
  and the runtime, not a codegen concern.

## What's still stubbed

- `gen_opi('%')` and `TOK_UMOD` — modulo. PPC has no modulo
  instruction; need to compute `a - (a/b)*b` using a temp register.
  Easy add, deferred.
- `gen_opf` — floating point. Not yet started.
- `gfunc_call` — function calls. **The next big piece.**
- `gen_va_start` / `gen_va_arg` — varargs.
- VLAs, struct returns, address-of-symbol, etc.

## Next session

Session 007 — function calls per the Apple PPC ABI:
- Args 1-8 in r3-r10.
- FP args in f1-f13.
- Stack params above the linkage area in caller's frame.
- Return value in r3 (int) or f1 (float).
- bl (branch-with-link) for the call itself.

Reference: PowerPC PEM (Programming Environments Manual) and Apple's
"Mac OS X ABI Function Call Guide" — both available locally per
[001-fork-comparison/findings.md] and the user's references.
