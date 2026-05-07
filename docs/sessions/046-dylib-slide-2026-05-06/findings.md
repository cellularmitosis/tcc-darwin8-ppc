# Findings — session 046 (2026-05-06)

## How dyld processes Mach-O local relocations on Tiger PPC

For non-MH_OBJECT files (executables and dylibs), each
`relocation_info` entry in LC_DYSYMTAB.locrel encodes:

```
struct relocation_info {
    int32_t  r_address;          // bytes 0..3, BE
    uint32_t r_symbolnum:24,     // bytes 4..7, BE bitfield (high 24)
             r_pcrel:1,
             r_length:2,
             r_extern:1,
             r_type:4;           // (low 4 of byte 7)
};
```

For VANILLA (r_type=0) local (r_extern=0, r_pcrel=0, r_length=2):
- byte 7 = `(r_extern<<4) | r_type | (r_length<<5) | (r_pcrel<<7)`
  = `0 | 0 | 0x40 | 0` = **`0x40`**
- bitfield word = `(r_symbolnum << 8) | 0x40`

`r_address` is the offset from the **first segment's vmaddr**
(image base = __TEXT vmaddr). dyld computes the location at
runtime as `image_base_runtime + r_address` and adds the slide
amount to the 4-byte word at that address.

`r_symbolnum` is the 1-based ordinal of the section the value
points into (in load command order, across all segments).
dyld doesn't actually consult it for VANILLA local relocs (it
just adds the slide unconditionally), but otool / `ld -r` / merge
operations expect a meaningful value.

Verified by raw byte inspection of a gcc-built PPC dylib: the
locreloff bytes for a `static int *p = &arr[3]` in __data were
`00 00 10 0c 00 00 05 40` — r_address=0x100c, r_symbolnum=5
(__DATA,__data), bitfield byte 0x40 (length=2, all else 0).

## Why we can't just emit Mach-O HA16/LO16 paired relocs

dyld does support PPC_RELOC_HA16/LO16 with PPC_RELOC_PAIR for
slide-time fixup, but Tiger's stock dylibs (libSystem,
libmathCommon, etc.) don't use them at all. Apple's strategy
across the board is:

* code is PIC (uses bcl/mflr to anchor + addis/addi against the
  anchor; the (sym - anchor) displacement is slide-invariant);
* only data slots get VANILLA local relocs.

Verified: `otool -rv /usr/lib/libSystem.B.dylib | grep HA16` is
empty; out of 1792 reloc entries, zero are HA16/LO16/PAIR.

We follow the same convention. `ppc_need_pic_for_sym` returns 1
for locally-defined symbols in DLL mode so code goes through
the existing PIC sectdiff path; data initializers get VANILLA
locrels.

## PIC base setup must be eager, not lazy

`ppc_emit_pic_addr_load` lazily emits PIC base setup
(`mflr/bcl/mflr/mtlr` giving r30 as anchor) the first time it's
called within a function. That's fine when first use dominates
all subsequent uses — true for OBJ/EXE because most functions
either use externs early or never.

In DLL mode, with locally-defined syms also routed through PIC,
the first PIC use can be inside a conditional branch (e.g.
`tcc_parse_args`'s `@file` handler is the very first PIC ref;
the main loop body is later). Lazy emission then puts the
PIC base instructions inside that branch — when the branch
isn't taken, r30 is the **caller's r30** (because we saved it
in the prolog but never overwrote it), and any later PIC use
in the same function reads garbage from `r30 + offset`.

Failure mode found in libtcc2.dylib: tcc_parse_args's loop over
`tcc_options[]` used PIC sectdiff to compute the array base.
With lazy PIC setup landing inside the @file branch, fall-through
runs of tcc_parse_args computed `popt = caller_r30 + 0x136b8`
which was way out of any segment — first dereference SIGSEGV'd.

Fix: in `gfunc_prolog`, eagerly emit PIC base setup at function
entry whenever output_type is OBJ/EXE/DLL. 16-byte cost per
function (the four-instruction setup) but always correct.

## Scope of the slide fix

Works under slide:
* `int *p = &arr[3]` (and other ADDR32 in __data)
* `void (*fns[])() = {f1, f2}` (function pointers in __data)
* `__attribute__((constructor))` — the function pointer in
  __mod_init_func is locrel'd and dyld fixes it before calling
* code refs to local data (via the existing PIC sectdiff path)
* code calls to extern functions (PIC stubs, slide-invariant)
* code refs to extern data (via __nl_symbol_ptr, slide-invariant)

Does NOT work under slide (known limitation):
* `static const struct {const char *name; ...} tbl[]` placed in
  __const. tcc routes pointer-bearing const arrays to .rodata
  because of the `const` qualifier; dyld can't write to __const
  at runtime, so the inner `name` pointers stay unslid.
  Workaround: drop the outer const, or accept that no-slide is
  the common case (preferred vmaddr 0x40000000 is high enough
  to usually be free).

A future improvement would teach `tcc/tccgen.c` to route
pointer-bearing const initializers to .data instead of .rodata
when `output_type == TCC_OUTPUT_DLL`. Out of scope this session.

## Test methodology for forcing dyld to slide

`mmap((void*)0x40000000, 0x01000000, PROT_NONE,
      MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0)` reliably reserves
the dylib's preferred range, so the next `dlopen` is forced to
load the dylib elsewhere.

Two pitfalls hit during demo authoring:
* dyld caches dlopen handles. Calling `dlopen("foo.dylib")`
  twice in the same process returns the same handle — the
  second call doesn't re-load. So you can't run a "no slide"
  pass and a "with slide" pass back-to-back inside one process.
* `fork()` doesn't help: the child inherits the parent's
  address space (with the dylib already mapped). MAP_FIXED in
  the child clobbers the child's view but dyld's bookkeeping
  is half-inherited from the parent and gets confused.

Solution: two separate test programs. One dlopens normally;
the other reserves 0x40000000 first, then dlopens. They link
to the same dylib but exercise it at different load addresses.
This is what `demos/v0.2.29-dylib-slide.sh` does.
