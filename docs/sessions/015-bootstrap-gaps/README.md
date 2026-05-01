# 015 — Closing the bootstrap gaps

We compile **all 11 tcc source files** with our tcc, and gcc-4.0
links them into a 463KB `tcc-self` binary. It doesn't run yet (one
remaining blocker — PPC PLT stubs — described below), but every
piece up to that point now works.

## What was added (5 distinct fixes)

### 1. `ppc-gen.c` `load()` — VT_LLOCAL case

`VT_LLOCAL` is tcc's "indirect local": the local at FP+c.i holds a
*pointer* to the actual value. To load: load the pointer first,
then deref. Mirrors `i386-gen.c:316`. Used by tcc for VLAs and some
struct-by-pointer paths.

### 2. `ppc-gen.c` `load()` — VT_JMP / VT_JMPI case

Materialize a jump-list result as 0 or 1 in a register. Pattern
(matches i386):

```
li   rD, t              ; t = v & 1
b    $+8                ; skip the chain target
patch_chain_to_here:
li   rD, t ^ 1
```

### 3. `ppc-gen.c` `store()` — VT_CONST + VT_SYM (global variables)

Store to a global / relocatable symbol:
```
lis  rTMP, sym@ha       ; R_PPC_ADDR16_HA reloc on the lis
stw  rS,   sym@lo(rTMP) ; R_PPC_ADDR16_LO on the stw
```
Same scheme works for stb/sth/stfs/stfd. Long-long handled with two
adjacent `stw`s.

### 4. `ppc-gen.c` `gen_cvt_ftoi` / `gen_cvt_itof` — long long via libcall

PPC32 has no hardware FP↔64-bit-int instructions (those are
PPC64-only). Same as ARM: dispatch to libgcc helpers
(`__fixsfdi`, `__fixdfdi`, `__floatdisf`, `__floatdidf`). The
unsigned variants are already dispatched in `tccgen.c` via
`gen_cvt_ftoi1` / `gen_cvt_itof1`. Added the necessary `TOK_*`
definitions in `tcctok.h` under a `TCC_TARGET_PPC` block.

### 5. `ppc-gen.c` `gfunc_call` — stack-passed args (slot >= 8)

Apple PPC ABI: args using GPR slots 0..7 go in r3..r10, slots 8+
spill to the outgoing parameter area at `r1+24+slot*4`. Without
this, `fprintf(stderr, fmt, ...)` with several args (which counts
each `double` as 2 GPR slots) immediately overflowed.

Bumped `PPC_PARAM_AREA` from 32 to 96 bytes (24 GPR slots) — enough
for any stdarg call we've seen. Long-long stack args use two
adjacent `stw`s.

### 6. `tccpp.c` — big-endian token-stream bug (CRITICAL)

`tok_str_add2` (tccpp.c around line 1083) wrote 32-bit constants
via `cv->tab[0]`. On big-endian PowerPC, `cv->tab[0]` aliases the
**high** 32 bits of the 64-bit `cv->i` union member. For a small
constant like `cv->i = 0xc2`, the high word is 0, so the saved
value was always 0.

The bug only surfaced when `gen_inline_functions` later replayed a
saved token stream containing `case TOK_CINT:` (etc.) — every case
label resolved to 0, triggering "duplicate case value" with two
fake line-0 labels.

**Fix:** split `TOK_CFLOAT` out of the int-constant case group and
write `(int)cv->i` for the integer types. The float path keeps using
`cv->tab[0]` since `cv->f` lives at byte offset 0 of the union (so
correct on either endianness). The 64-bit / long-double paths were
already symmetric write/read.

This is the kind of byte-order bug that wouldn't bite anyone until
a big-endian host tried to bootstrap. It's a real upstream bug that
should be reported.

### 7. `ppc-macho.c` — symtab + reloc table 4-byte alignment

`ld(1)` rejected our .o files with "symoff in load command 1 not
aligned on 4 byte boundary." Fix: pad `cur_off` to 4 bytes before
assigning `reloc_file_off` and `sym_file_off` in the layout pass.

### 8. `libtcc.c` — TCC_TARGET_PPC arch-include block

libtcc.c's per-arch include chain was missing PPC. Added:
```c
#elif defined(TCC_TARGET_PPC)
#include "ppc-gen.c"
#include "ppc-link.c"
```
And a separate Mach-O block:
```c
#if defined(TCC_TARGET_MACHO) && defined(TCC_TARGET_PPC)
#include "ppc-macho.c"
#endif
```
(The original `#ifdef TCC_TARGET_MACHO #include "tccmacho.c"` is
now gated to `!TCC_TARGET_PPC` so we don't pull in the
x86_64/arm64-only file.)

## State now

```
$ for src in libtcc.c tccpp.c tccgen.c tccelf.c tccrun.c tccdbg.c \
             tccasm.c ppc-gen.c ppc-link.c ppc-macho.c tcc.c; do
    ./tcc -B. -DONE_SOURCE=0 -c $src -o /tmp/$src.o
  done
  [OK]    libtcc.c (45397 bytes)
  [OK]    tccpp.c (104987 bytes)
  [OK]    tccgen.c (212493 bytes)
  [OK]    tccelf.c (46198 bytes)
  [OK]    tccrun.c (26320 bytes)
  [OK]    tccdbg.c (63279 bytes)
  [OK]    tccasm.c (1093 bytes)
  [OK]    ppc-gen.c (37218 bytes)
  [OK]    ppc-link.c (3035 bytes)
  [OK]    ppc-macho.c (17252 bytes)
  [OK]    tcc.c (24020 bytes)

$ gcc-4.0 /tmp/tcc-bootstrap/*.o -o /tmp/tcc-self -lm -ldl -lpthread \
        -flat_namespace -Wl,-read_only_relocs,suppress \
        /Developer/SDKs/MacOSX10.4u.sdk/usr/lib/gcc/.../libgcc.a
$ file /tmp/tcc-self
/tmp/tcc-self: Mach-O executable ppc
$ ls -la /tmp/tcc-self
-rwxr-xr-x   1 macuser  wheel  463220 Apr 30 23:46 /tmp/tcc-self
$ /tmp/tcc-self -v
dyld: unknown external relocation type
```

The binary EXISTS, is the right shape (Mach-O ppc executable), and
links. It just can't run because we emit `bl <external>` directly
as an `R_PPC_REL24` against the external symbol. dyld can't patch
24-bit branches at load time — Mach-O needs PIC **stubs** for
external calls.

## The remaining blocker: PPC PLT stubs

For each external function call, classic PPC Mach-O emits a stub
in the `__picsymbolstub1` section (typically 32 bytes per stub):

```
mflr  r0
bcl   20, 31, $+4              ; subroutine call to next insn
mflr  r11                      ; r11 = pc
mtlr  r0
addis r11, r11, ha16(__la_symbol_ptr - .)
lwz   r12, lo16(__la_symbol_ptr - .)(r11)
mtctr r12
bctr
```

Plus a `__la_symbol_ptr` section holding one pointer per stub
(initially pointing at `dyld_stub_binder` or similar; dyld patches
them on first call).

Plus an indirect symbol table that links each `__la_symbol_ptr`
slot to its symbol-table entry.

Plus the BR24 reloc against the external function gets retargeted
at the local `__picsymbolstub1` entry (a defined symbol in the
output), so it's no longer external.

This is real work — probably a session of its own. The agent who
fixed the duplicate-case bug noted the same.

## Regression tests still pass

All previously-working programs continue to work:
- `int main(void){return 42;}` → exit 42
- recursive `fib(10)` → exit 55
- factorial, sum_array, FP polynomial, varargs, etc.

## Decisions

- **The big-endian token-stream bug** in tccpp.c is a real upstream
  TCC bug. Worth reporting back to the tinycc mailing list once we
  ship.
- **`tcc-self` is buildable but non-functional** — until PLT stubs
  land, we can't actually run it.
- **gcc-4.0 link line for tcc-self requires explicit libgcc** —
  `__floatundidf` (and friends) aren't in the default link path;
  we point at `/Developer/SDKs/MacOSX10.4u.sdk/usr/lib/gcc/...`.
  This is probably fine for the eventual tarball — we ship a
  Makefile rule that knows the right invocation.

## Next session

016 — implement PPC PLT stubs in ppc-macho.c. After that, `tcc-self`
should actually run, and we can test `tcc-self compile a hello world
that links with gcc-4.0` for the full self-host loop.
