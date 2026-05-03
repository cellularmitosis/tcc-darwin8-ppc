# PPC32 codegen pitfalls — postmortem for parallel PPC backends

These are bugs we hit while bringing tcc to a working state on
Apple PPC32 (Mac OS X 10.4 Tiger / G3-G4). They are **not
tcc-specific**: any PowerPC 32-bit code generator targeting Apple
ABI on Tiger/Leopard hardware will likely hit the same issues,
in the same order, surfaced by the same kinds of programs.

This file is intentionally written for someone bringing up a
**different compiler backend** for the same target — e.g. the
parallel golang/PPC fork. Each section names the bug, what
surfaces it, and the shape of the fix. Code references are to
tcc's source but should translate.

The bugs are listed in roughly the order they bite a new backend:
the simple ones (constants, simple loads) first; the structural
ones (struct layout, multi-arg calls) later. If you see test 1
in your test suite passing but test 5 failing in weird ways,
this list is a checklist.

## Apple PPC ABI vs SysV PPC ABI — the alignment trap

The PowerPC SVR4/Linux ABI is **not** the Apple PPC ABI. Apple
inherited rules from the m68k Mac ABI, and they're wrong in ways
you'll reproduce by mistake.

### double / long long alignment in structs

* **SysV PPC**: 8-byte alignment for `double` and `long long`
  inside structs.
* **Apple PPC**: **4-byte** alignment for `double` and `long long`
  inside structs.

If your backend defaults to SysV (because most PPC32 docs you'll
read are SysV-flavored), every struct with a `long long` or
`double` will have the wrong layout vs gcc-4.0 / Apple's headers.
You won't notice until you call into libSystem or link against a
gcc-4.0-compiled .o.

**Surfaced by**: any third-party C library. We hit it in sqlite —
`PCache.nRefSum` (i64) landed at offset 16 in our compiler but
offset 12 in gcc, so `sqlite3PcacheRefCount` read junk on the
first non-trivial code path inside `sqlite3_open`. Smaller test
case: any struct of `{void *, void *, void *, long long, int}` —
compare `sizeof()` and `offsetof(ll)` against gcc.

**Fix**: in `type_size` (or equivalent), gate the 8-byte alignment
on PPC64 only. PPC32 → 4-byte.

In tcc this was a one-line gate addition:
```c
} else if (bt == VT_DOUBLE || bt == VT_LLONG) {
#if (defined TCC_TARGET_I386 && !defined TCC_TARGET_PE) \
 || (defined TCC_TARGET_ARM && !defined TCC_ARM_EABI) \
 || (defined TCC_TARGET_PPC && !defined TCC_TARGET_PPC64)  /* ← */
    *a = 4;
#else
    *a = 8;
#endif
    return 8;
}
```
(`tccgen.c::type_size`, commit `bc1ff4d`.)

### long long argument passing

Apple PPC32 ABI: a `long long` arg occupies **two consecutive GPR
slots**, **HIGH in the lower-numbered register** (`r3+gslot`),
LOW in the higher (`r3+gslot+1`). For `func(int a, long long b)`:
* `r3 = a`
* `r4 = (int)(b >> 32)` (HIGH)
* `r5 = (int)(b & 0xFFFFFFFF)` (LOW)

This matches SVR4. If you also support SVR4 PPC, the conventions
align here.

A `long long` arg never starts in r3 unless it's the first arg
(no preceding ints/structs). It's always in slots `gslot` and
`gslot+1` where `gslot` is the running GPR-slot counter.

For variadic FP args past the FPR registers (`fslot >= 8`), the
shadow GPR slot still gets the value — write the FP value to the
outgoing param stack at the GPR shadow position. Otherwise
printf("%f") with many earlier args reads garbage. We hit this
on `tests2/73_arm64`.

### long long return from functions

Apple PPC ABI: returned in **r3=HIGH, r4=LOW**.

Most backends' internal convention is the opposite (`r3=LOW,
r4=HIGH`). If yours is, swap r3/r4 right before `blr` in the
callee, and right after the call in the caller. tcc emits the
3-instruction swap (`mr r0, r3; mr r3, r4; mr r4, r0`) every
LL-returning function — every time, both sides.

We initially scoped the swap to "FP-to-LL helpers only" and
broke many things. The swap is needed for **every** LL return,
not just specific helpers.

### Big-endian sub-word param offset

For `void f(char c)` or `void f(short s)`, the caller passes the
value in r3. The callee's prolog spills r3 to the linkage area
(`24(r1)`). To READ the byte/short, you read at:
* `+27(r1)` for byte (= +3 from the word)
* `+26(r1)` for short (= +2 from the word)

We hit this when reading single-byte function args. Forgetting
the BE adjustment reads zero (the high bytes of the spilled
word).

## Big-endian bitfields

This is a structural decision, not a bug — but every BE backend
trips on it.

### The problem

C compilers historically choose between "bit numbering follows
byte numbering" (LSB first within byte 0, then byte 1, etc.) and
"bit numbering follows logical order" (MSB first within the
container word, regardless of byte boundaries). On LE these two
schemes produce identical memory layouts. On BE they don't.

* **LSB-first (tcc, our pick)**: byte-wise initializer writes
  bits at byte boundaries, low bit first. A static
  `s = {0x333}` for `unsigned x : 12` writes
  `byte0 = 0x33; byte1[0..3] = 0x3`.

* **MSB-first (gcc, AIX, standard BE convention)**: the first
  declared field goes at the MSB end of the container. Same
  init writes `byte0 = 0x33; byte1[4..7] = 0x3`.

Neither is wrong, but they're not interchangeable. **Pick one
and be consistent**, and document it. Code that makes assumptions
about the in-memory layout (e.g. parsing on-disk formats) will
care.

### The trap

If your compiler stores bitfields LSB-first byte-wise (matching
the static initializer) but reads them via "load the whole
container as a BE int, shift+mask", you'll read garbage on BE.
The static init's bits don't land in the same int positions
that your shift+mask expects.

We hit this on the `96_nodata_wanted` test — a static-initialized
bitfield struct read back wrong values for `x`, `y`, `z` because
the init was byte-wise but the read was wide-container.

### The fix we shipped

Force every bitfield on PPC32 to use byte-wise load/store
(matching the byte-wise init). Slower (~2-4 byte loads + shifts
per access vs 1 int load + 2 shifts), but correct.

In tcc: `f->auxtype = VT_STRUCT` in `struct_layout`'s FIX pass,
gated on PPC32. (`tccgen.c::struct_layout`, commit `e929b86`.)

The "right" fix is to switch to MSB-first numbering on BE so
both the byte-wise init AND the wide-container read agree on the
same in-memory layout. That's a wider change touching every
bitfield path.

## PPC integer codegen quirks

### slw / srw / sraw on shift count ≥ 32

PPC's `slw` / `srw` / `sraw` zero the result when the shift count
is ≥ 32 (per the PowerPC ISA — bit 26 of rB acts as a "shift
beyond width" flag). This is **better** than C's "undefined
behavior" — you get a deterministic 0.

If your codegen for `(uint32_t)x << s` emits `slw`, `(uint32_t)x
<< 32` returns 0 even at -O0. gcc at -O0 may return `x` (the
original value) because it omits the shift instruction entirely
when it sees s as 32. So tcc/PPC is more "correct" than gcc here.

For `long long` shifts (LL << s where s can be 0-63), don't try
to use a single PPC instruction. Decompose into HIGH/LOW pair
operations:
```
if (s == 0) { hi = oldhi; lo = oldlo; }
else if (s < 32) { hi = (oldhi << s) | (oldlo >> (32-s)); lo = oldlo << s; }
else { hi = oldlo << (s-32); lo = 0; }
```
The decomposition relies on `slw`/`srw`'s zero-on-overshoot
behavior to be robust at the boundary. If your backend uses a
different instruction that doesn't have this behavior (e.g. some
embedded variant), you need explicit checks.

### lwz with rA = r0 means literal 0

PPC encoding rule: `lwz rD, d(rA)` with `rA = 0` (that's the
literal register encoding 0, not r0's contents) reads from
absolute address `d`. This is the PPC way to do absolute loads.

If your codegen accidentally emits `lwz rD, 0(r5)` thinking r5
holds an address, but r5 was set to 0 by some earlier path (bug
or intent), the load goes to address 0 — NULL deref. Easy to
miss in disassembly because the instruction looks normal.

We hit this in two distinct places:
1. Static initializers for `(int *)0; *p` — emit `li r3, 0; lwz
   rD, 0(r3)`. Crash. Maybe intentional in the source.
2. **Argument shuffle clobbering an earlier-prepared address
   register** — see "Multi-arg call setup with mixed types" below.

### Sign-extending vs zero-extending the upper half of literals

`lis` puts a 16-bit value into the high half, sign-extended.
`addi rD, rA, simm` adds a 16-bit signed immediate.
`ori  rA, rS, uimm` ORs in a 16-bit zero-extended immediate.

For loading a 32-bit literal, the idiom is:
* `lis r, hi(value); ori r, r, lo(value)` if you want zero-ext lo.
* `lis r, ha(value); addi r, r, lo(value)` if you want sign-ext lo.

`@hi` and `@ha` differ for relocations: `@ha` (high adjusted)
adds 0x8000 to the high half if the low half has its sign bit
set, so that `lis+addi` produces the right address. `@hi` is
the raw upper 16 bits, paired with `ori`.

**Mismatched pairing** (e.g. `lis @hi; addi`) silently produces
addresses off by 0x10000 when the low half's bit 15 is set.
We had this bug; surfaced as random crashes during PIC PLT setup.

## Multi-arg call setup with mixed types

This is the **biggest single bug we hit this session** and the
one a parallel backend is most likely to reproduce.

### The setup

Consider:
```c
LSEEK(state->fd, 0LL, SEEK_CUR)
```
where `LSEEK` is `lseek64(int, off64_t, int)`. Apple PPC ABI:
* `r3 = state->fd` (int, arg1)
* `r4 = HIGH(0LL) = 0` (arg2, occupies r4+r5)
* `r5 = LOW(0LL) = 0`
* `r6 = SEEK_CUR = 1` (int, arg3)

A naive backend might:
1. Pre-compute arg1's address: `r4 = state + 20` (state->fd's
   address, ready to deref later).
2. Process args in some order, materializing each into its ABI
   slot.

For arg2 (the LL), you'd typically allocate two scratch GPRs
holding 0/0, then `mr r4, scratch_hi; mr r5, scratch_lo` to put
them in the ABI slots.

The `mr r4, ...` step **clobbers r4**, which still held arg1's
address. arg1's later `lwz r3, 0(r4)` then loads from r4=0 →
NULL deref → SEGV.

### Why this bites

The conflict only fires when:
* You have ≥ 2 args.
* One arg is a long long (or anything that uses 2 GPR slots).
* Another arg's address calculation lands in the slots the LL
  will fill (which is most of them — slots 1-2, 2-3, 3-4, etc.).

That covers approximately every call with mixed `{address, LL}`
args. We hit this in:
* `LSEEK(fd, off64_t, whence)` — first 64-bit syscall in zlib.
* `sqlite3PCacheRefCount(pPager->pPCache)` — reading an i64 field
  via a function call.
* Probably ~40% of sqlite's internal helpers, which is why
  sqlite was so hard to bring up.

### The fix

Before doing the LL move shuffle, **save_reg the target ABI
slots**. That spills any vstack entry currently using those regs
(arg1's address pointer) to a stack local. Subsequent arg setup
re-loads from the local instead of the clobbered register.

In tcc:
```c
int target_hi = gslot + 3;       /* PPC reg numbers */
int target_lo = gslot + 1 + 3;
save_reg(target_hi - 3);   /* convert PPC reg back to TREG idx */
save_reg(target_lo - 3);
gv(RC_INT);                /* now safe to materialize the LL */
/* ... mr's to target_hi / target_lo ... */
```
(`ppc-gen.c::gfunc_call`, commit `bc50586`.)

If you don't have a `save_reg` equivalent (because your backend
doesn't track vstack), the equivalent is: detect that another
arg's address sits in target_hi/lo, spill it to a stack slot,
mark the spill, and have arg1's setup re-load from the spill.

## Long-long field loads (`x->ll_field`)

The PPC32 BE LL split-load pattern is:
```
addr = ptr + offsetof(field)     ; address of the LL
hi = *(int *)(addr + 0)          ; HIGH at offset 0 on BE
lo = *(int *)(addr + 4)          ; LOW at offset 4 on BE
```

The naive way:
```
incr_offset(+4)        ; address += 4
load LOW into r        ; r = *(addr+4) — but writes through `addr`'s register
incr_offset(-4)        ; address -= 4
load HIGH into r2      ; r2 = *(addr+0)
```

Bug: the first `load(r, ...)` may **clobber the register holding
`addr`** (e.g. `lwz r3, 0(r3)`). The subsequent `incr_offset(-4)`
then operates on the **loaded LOW value** instead of the address,
producing a garbage address for the HIGH load.

This bites when the lvalue is a register-pointer — typical
output of `p->field` after the field-offset add. tcc internally
uses an LLOCAL (indirect-local) marker that means "the local
holds a pointer to the value"; the address can be re-derived
from the local on demand.

### The fix

Before the LOW load, snapshot the lvalue's state. After the LOW
load, restore the snapshot so the HIGH load re-derives the
address fresh from the saved local (or wherever the address
originally came from).

(`tccgen.c::gv` in the LL split-load branch, commit `bc1ff4d`.)

If your backend doesn't have an LLOCAL-style marker, you need to
explicitly spill the address to a temp local before the LOW load
and re-load it before the HIGH load. Same effect.

## Mach-O linker quirks

Less likely to bite you if you're emitting via an external
linker (ld), but if you're writing your own Mach-O writer:

### Cross-TU PIC reloc translation

When linking multiple `.o` files, references to `extern` data
symbols from one TU into another go through `__nl_symbol_ptr`
indirection on Tiger Mach-O. The address materialization is a
HA16/LO16 pair (lis + lwz, or lis + addi).

The Mach-O reloc representation for these is **scattered**:
`HA16_SECTDIFF` and `LO16_SECTDIFF`. If your linker reads input
`.o` files and skips scattered relocs (assuming they're some
local thing), you'll silently drop the relocations on cross-TU
data refs. The link succeeds but the runtime references go to
random `__TEXT` addresses.

Translate `HA16_SECTDIFF` / `LO16_SECTDIFF` into your equivalent
of `R_PPC_HA16_PIC` / `R_PPC_LO16_PIC`, resolve the indirect-
symtab slot to the underlying extern, and record the per-reloc
PIC anchor. We hit this when first compiling lua to multiple TUs.

### `__.SYMDEF SORTED` for archive members

The BSD-style archive format used by Apple `ar` puts a symbol
table named `__.SYMDEF SORTED` (note: literal space + "SORTED")
at archive index 0. The format uses `#1/N` "long names" where
the actual filename follows the header inline.

If your `ar` driver only knows ELF, it won't be able to write
Tiger archives. You can lean on the system `ar` for a while
(tcc still does), but eventually you'll want native support.

## What we DIDN'T hit (worth flagging)

Things that other backends bring up have caused us, but we don't
think we hit them in tcc:

* AltiVec. No tests use it; we emit scalar code for everything.
* 64-bit PPC (G5 in 64-bit mode). Out of scope.
* `lwarx`/`stwcx` atomics for 1- and 2-byte operations. PPC32 has
  no `lbarx` / `sbarx`, so we use word-sized RMW with masking.
  See `tcc/lib/atomic-ppc.S` if you want the implementation.
* Long-double on Apple PPC32 is **128-bit IBM extended** (a pair
  of doubles), not IEEE 754 binary128. We treat it as plain
  double in the backend; we haven't hit a test that needs the
  full 128-bit precision.

## File / commit references

All commits are in [https://github.com/cellularmitosis/tcc-darwin8-ppc](https://github.com/cellularmitosis/tcc-darwin8-ppc).
The four bugs documented above were fixed in session 041
(2026-05-03):

| Bug | Commit | tcc file |
|---|---|---|
| BE bitfield read-back | `e929b86` | `tcc/tccgen.c::struct_layout` FIX pass |
| Apple PPC ABI long-long alignment | `bc1ff4d` | `tcc/tccgen.c::type_size` |
| LL field-load address-clobber | `bc1ff4d` | `tcc/tccgen.c::gv` LL split-load branch |
| LL-arg shuffle clobber | `bc50586` | `tcc/ppc-gen.c::gfunc_call` LL move block |

The session writeup with full repros, traces, and diagnosis steps:
[docs/sessions/041-pickup-2026-05-03/](../sessions/041-pickup-2026-05-03/).

## Summary checklist

If you're bringing up a new PPC32 / Apple PPC backend and your
test suite is failing on real-world programs:

- [ ] Are your `double` and `long long` 4-byte aligned in
      structs (Apple ABI), not 8-byte (SysV)?
- [ ] Do bitfields on BE actually read back what you write
      statically? Test with
      `struct {unsigned x:12; unsigned y:20;} = {0x333, 0x55555};`
      and compare with gcc.
- [ ] Does `lwz rD, 0(rA)` with `rA = 0` deref absolute address
      0 in your backend, or do you treat r0 as a register? (PPC
      semantics: `rA = 0` means the literal value 0, not r0.)
- [ ] When generating `func(addr_of_field, 0LL, int_const)`, do
      you spill the address-of-field if it lands in the LL's
      target ABI slot? Easy to test:
      `lseek(fd, 0LL, SEEK_CUR)` from a struct.
- [ ] When loading a `long long` field via a pointer (`p->ll`),
      does the second-half load survive the address-register
      clobber from the first-half load?
- [ ] Does long-long return swap r3<->r4 in **every** LL-returning
      function (both callee return and caller post-call)?
- [ ] Do char/short function args read from `+3` / `+2` of the
      spilled word on BE?

If you can answer "yes" to all, you're past where we were stuck
for several sessions.
