# Session 039 findings — durable lessons

## Mach-O `N_WEAK_REF` is a low-byte n_desc bit, NOT a binding type

For weak undefined externals (`__attribute__((weak)) void f();`),
the linker output must set `N_WEAK_REF = 0x40` in n_desc's low
byte. Without it, dyld treats the UNDEF as hard and aborts at load
time when the symbol can't be resolved — even though the user code
expects the address to come back as 0 and tests `!!&f`.

The high byte of n_desc carries the two-level namespace library
ordinal (1 = first LC_LOAD_DYLIB). The low byte carries flags
including `N_WEAK_REF`. Tiger header: `<mach-o/nlist.h>`.

Detection on our side: `ELFW(ST_BIND)(esym->st_info) == STB_WEAK`
on the underlying ELF symbol entry.

## `cpp` macro args don't survive commas in PPC asm

Tried writing `atomic-ppc.S` using a cpp macro:
```
#define ATOMIC_FETCH_OP_4(name, op_insn) ...
ATOMIC_FETCH_OP_4(add, add r6, r0, r4)
```

Errors with "macro passed 4 arguments, but takes just 2" — cpp
splits on every comma in the call site. The PPC three-operand
instruction syntax (`add r6, r0, r4`) is inherently comma-rich.

Workarounds:
* Don't use cpp macros; write each function out (what I did).
* Wrap multi-comma args in parens — but parens become part of
  the substituted text, breaking the asm.
* Use C++-style variadic macros (`__VA_ARGS__`) — works in modern
  cpp but is ugly for repeated boilerplate.

For a small, fixed set of ops (~6 here), explicit duplication is
fine and readable.

## Per-file Makefile override pattern for the libtcc1.a build

To route a single source file through a different compiler (e.g.
`atomic-ppc.S` through gcc-4.0 instead of the generic
`$(XCC) = $(TCC)` rule):

```make
ifneq "$(filter ppc%,$T)" ""
$(X)atomic-ppc.o : atomic-ppc.S
	$S$(CC) -c $< -o $@
endif
```

The make ruleset's most-specific match wins, so this overrides
the generic `$(X)%.o : %.S` rule for `atomic-ppc.S` only.

`$(CC) = gcc-4.0` is set in `tcc/config.mak` from the configure
probe. `$(X)` is the cross-prefix (empty for native PPC builds).

## Variadic FP arg passing on Apple PPC has BOTH FPR and GPR-shadow paths

When calling a variadic function with FP args, the Apple PPC ABI
requires the FP value to live in TWO places:

1. The FP register `f1..f8` (so non-variadic callees can read it
   from there).
2. The corresponding GPR shadow slot — `r3..r10` if `gslot < 8`,
   or the outgoing parameter stack at `r1 + 24 + gslot*4` if
   `gslot >= 8` (so variadic callees walking `va_arg` by GPR
   slot see the right bytes).

Forgetting (2) when `gslot >= 8` produces "garbage in printf"
bugs that look like they should be parser or precision issues.
73_arm64's "0.0 0.0" output and 70_floating_point_literals'
"5-ULP error" were both this: printf reads from the GPR shadow
when fpr_used >= 8 OR when the va_arg tracking points there;
if the shadow wasn't filled, you get whatever was on the stack.

Specific cases for a `double` arg, where `gslot` is the starting
GPR-shadow slot (0-based):

* `gslot < 7`: both halves fit in r3..r10. Use `stfd; lwz; lwz`.
* `gslot == 7`: high half in r10, low half spills to stack at
  `r1 + 24 + 8*4 = r1+56`. **Don't forget the stack write.**
* `gslot >= 8`: both halves on stack at `r1 + 24 + gslot*4` /
  `r1 + 24 + (gslot+1)*4`. `stfd fS, (24+gslot*4)(r1)` writes
  both halves with one instruction.

For float args, only one slot needed. `stfs fS, (24+gslot*4)(r1)`
when `gslot >= 8`.

## Word-RMW byte/short atomic pattern on PPC32 BE

PPC32 has `lwarx`/`stwcx.` for word atomics but no `lbarx`/`sbarx`
for byte/short. Sub-word atomics work by lwarx-ing the containing
4-byte word, masking out the byte/short region, OR-ing in the new
value, then stwcx-ing the whole word. Reservation tracks the
word, so concurrent RMW of the same byte from multiple threads
serializes correctly via the reservation cancel.

Big-endian byte/short addressing within a word:
* byte at addr A: `shift = (3 - (A & 3)) * 8 = ((A ^ 3) & 3) << 3`
  (places byte at bits `[shift, shift+7]` of the word).
* half at addr A (assumed 2-aligned): `shift = (1 - ((A>>1) & 1)) * 16
  = ((A ^ 2) & 2) << 3` (bits `[shift, shift+15]`).

Implementation pattern in PPC asm (byte fetch_add):
```
clrrwi  r9,  r3, 2             # word ptr
xori    r10, r3, 3
rlwinm  r10, r10, 3, 27, 28    # shift
li      r11, 0xff
slw     r11, r11, r10          # mask = 0xff << shift
sync
1:
lwarx   r0,  0, r9             # load word with reservation
srw     r7,  r0, r10           # extract old byte (low 8 bits + garbage)
add     r7,  r7, r4            # apply OP
slw     r7,  r7, r10           # shift back to position
and     r7,  r7, r11           # mask to byte position
andc    r6,  r0, r11           # preserved bits (other bytes)
or      r6,  r6, r7            # combined word
stwcx.  r6,  0, r9             # store-conditional
bne-    1b                     # retry on reservation loss
isync
srw     r3,  r0, r10           # return old byte (zero-ext)
rlwinm  r3,  r3, 0, 24, 31
blr
```

For 8-byte ops there's no equivalent PPC32 (ldarx/stdcx are PPC64
only); fall back to a pthread_mutex.

## Bitfield struct layout: tcc differs from gcc-4.0 on mixed-type fields (NOT YET FIXED)

Hits 96_nodata_wanted test_data_suppression_off. Test struct:
```c
struct {
    unsigned x : 12;
    unsigned char y : 7;
    unsigned z : 28, a: 4, b: 5;
} s = { 0x333, 0x44, 0x555555, 6, 7 };
```

GCC-4.0 byte dump (PPC, BE): `33 30 88 00 05 55 55 56 38 00 00 00`
* word 0 (BE) = `0x33308800`: bits 31..20 = 0x333 (x), bits 19..13
  = 0x44 (y), bits 12..0 = padding. **First-declared-field at
  HIGHEST bits**.

Tcc byte dump: `00 00 44 33 60 55 55 55 00 00 00 07`
* word 0 (BE) = `0x00004433`: bits 0..11 = 0x433 (x — corrupted!),
  bits 8..14 = 0x44 (y, OVERLAPPING x's high 4 bits). **First-
  declared-field at LOWEST bits**, AND y's offset is computed to
  start at bit 8 (the first byte after x's first 8 bits) instead
  of at bit 12 (just past x).

Two separate bugs in tcc's bitfield layout for PPC:
1. Wrong endianness ordering — fields should be packed from MSB
   (BE) for Apple PPC ABI, not LSB.
2. Mixed-type packing places `unsigned char y : 7` at the next
   BYTE boundary after x's first byte, not after x ends.

Fix is in `tccgen.c::struct_layout` (the `pcc` mode bitfield
section around line 4275+). Probably needs PPC-specific byte-
order awareness or (more likely) tcc's bitfield code has had a
long-standing BE bug that's only now exposed. Worth comparing
against tcc's other BE targets (mips, sparc) before assuming the
fix is PPC-specific.

## Enum-in-parameter-list scope extension (NOT YET FIXED)

C standard quirk: when a function is *defined* (not just declared)
with an enum declared inside its parameter list, the enum
constants are visible inside the function body. This is the
"parameter scope extends into function body" rule from the
standard.

Tcc currently *does not* extend parameter scope into the body for
enum constants. So this code:

```c
int bar(enum ee { a = 12, b = 34 } i) {
    return a + b;       /* should be 12 + 34 = 46 */
}
```

…compiles to `return 0 + 0` (or 1+1, in our case — both `a` and
`b` resolve to `1`, suggesting they're being parsed as default-
ordinal-1 enum constants somewhere). Hits 60_errors_and_warnings
test_scope_1.

Workaround: declare the enum at file scope or inside the function
body. Both work.

The fix is in tccgen.c around the parameter parsing — needs to
keep enum constants from parameter scope alive through the body
parse. Likely involves not popping the parameter scope when
transitioning from declarator to body.

## Apple PPC nlist's high byte carries the LIBRARY ORDINAL

Two-level namespace lookup: each undef external has `n_desc >> 8`
as the index of the LC_LOAD_DYLIB it should resolve from
(1-based; 0 = "any library", which generally fails). Our PPC
backend hardcodes 1 because we only ever load libSystem (well,
plus libpthread.dylib which is a shim that forwards back to
libSystem on Tiger).

If we ever want to load multiple dylibs, this needs to track per-
symbol which dylib provides it.
