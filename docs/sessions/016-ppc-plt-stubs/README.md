# 016 — PowerPC PIC stubs (the `printf` payoff)

🎉 **First-ever `printf` from a tcc-built program on the G3.**

Every prior demo either returned an exit code directly or ran a
self-contained computation. This session closes the loop to
`libSystem`: tcc-emitted code can now call into dynamically-linked
external functions.

## What was added (in `tcc/ppc-macho.c`, +448 / -31)

PowerPC Mach-O requires a specific recipe for late-binding external
function calls: stubs in `__picsymbolstub1`, lazy pointers in
`__la_symbol_ptr`, and indirect symbol table entries linking them.
Implemented as:

1. **`collect_extern_stubs()`** — walks every section's relocs,
   finds every `R_PPC_REL24` against an undef external, dedupes,
   builds a per-output-file stub table.

2. **Synthesized `__TEXT,__picsymbolstub1` section.** 32 bytes per
   stub. Section type `S_SYMBOL_STUBS`, attribute
   `S_ATTR_PURE_INSTRUCTIONS|SOME_INSTRUCTIONS`, `reserved2 = 32`.
   Canonical 8-instruction PowerPC PIC stub:
   ```
   mflr  r0                              ; save caller's LR
   bcl   20, 31, .+4                     ; call to next insn (PC-rel)
   mflr  r11                             ; r11 = address of next insn
   addis r11, r11, ha(la_ptr - .)
   mtlr  r0                              ; restore caller's LR
   lwzu  r12, lo(la_ptr - .)(r11)        ; r12 = *la_ptr
   mtctr r12
   bctr
   ```
   `lwzu` (not plain `lwz`) is required because
   `dyld_stub_binding_helper` expects `r11` to point at the
   `la_ptr` slot when it's invoked.

3. **Synthesized `__DATA,__la_symbol_ptr` section.** 4 bytes per
   stub. `S_LAZY_SYMBOL_POINTERS`, `reserved1 = nstubs`. Initial
   slot values are zero; `ld` fills them with the address of
   `dyld_stub_binding_helper`, which `dyld` swaps for the real
   function pointer on first call.

4. **Indirect symbol table.** `nstubs` entries for stubs followed
   by `nstubs` entries for la_ptrs, each a 4-byte word that's the
   nlist index of the corresponding undef external.

5. **Scattered relocs.** `PPC_RELOC_HA16_SECTDIFF` + `PPC_RELOC_PAIR`
   on the `addis` and `PPC_RELOC_LO16_SECTDIFF` + `PPC_RELOC_PAIR`
   on the `lwzu` so `ld` fixes up the la_ptr displacement at link
   time.

6. **`n_desc = REFERENCE_FLAG_UNDEFINED_LAZY`** on every stub-bound
   external symbol so `dyld` treats them as lazy-bound (matches
   what gcc-4.0 emits).

7. **BR24 retargeting.** Relocs against external functions are
   rewritten as section-relative `R_PPC_REL24` against the local
   `__picsymbolstub1` section, with the displacement pre-computed
   to the stub's offset.

8. **`dyld_stub_binding_helper`** is auto-injected into the symbol
   table so it appears as a regular undef external in nlist (gcc
   does the same).

## Test record

| Test | Result |
|---|---|
| `printf("hi\n")` compile + gcc-4.0 link + run | prints `hi`, exit 0 ✓ |
| Multi-stub demo (strdup, printf, strlen, free) | works, exit 0 ✓ |
| Compile all 11 tcc sources with our tcc | 11/11 OK ✓ |
| Link `/tmp/tcc-self` from those .o + libSystem | succeeds ✓ |
| All previous regression tests | still pass ✓ |

## Demo

[`demos/s016-hello-printf.sh`](../../../demos/s016-hello-printf.sh) —
`printf("hello from tcc-built program\n")`. Verified on imacg3.

## Structural comparison with gcc-4.0 reference

`otool -hlv` on a tcc `.o` calling printf matches gcc-4.0's `.o`
structurally:
- `__TEXT,__text` with section-relative BR24 to `__picsymbolstub1`
- `__TEXT,__picsymbolstub1` `S_SYMBOL_STUBS`, `reserved2 = 32`,
  scattered HA16/LO16 SECTDIFF + PAIR relocs
- `__DATA,__la_symbol_ptr` `S_LAZY_SYMBOL_POINTERS`,
  `reserved1 = 1`, VANILLA reloc against `dyld_stub_binding_helper`
- `LC_DYSYMTAB.nindirectsyms = 2` with both entries pointing at
  `_printf` in nlist
- nlist has `_printf` with `n_desc = 1`
  (`REFERENCE_FLAG_UNDEFINED_LAZY`)

For `libtcc.o`: 56 stubs, 56 la_ptrs, 112 indirect entries.

## What's STILL blocking tcc-self bootstrap

`/tmp/tcc-self -v` still aborts at startup. Different cause now —
**external DATA symbols** like `___sF`, `_tok_flags`, `_isspace[]`.
tcc's PPC codegen emits `addis ; lwz` with direct
`R_PPC_ADDR16_HA` / `R_PPC_ADDR16_LO` relocations against these
symbols, but `dyld` cannot patch a 16-bit instruction immediate at
load time on PPC. The only externally-bindable reloc dyld supports
is `PPC_RELOC_VANILLA` (32-bit ADDR32 in writable data).

Breakdown across libtcc/tccpp/tccgen/tccdbg/tccelf:
| Pattern | Count | Fix needed |
|---|---|---|
| `addi` (taking address) | ~28 | retarget to `__nl_symbol_ptr` + `addi → lwz` rewrite |
| `lwz` (loading value) | ~380 | insert `lwz <ptr>` indirection step |
| `stw` (storing value) | ~17 | insert `lwz <ptr>` indirection step |

Fix requires changes in `ppc-gen.c`, not `ppc-macho.c`. The codegen
needs to:

1. Set up a per-function PIC base register (typically via
   `bcl 20, 31, .+4 ; mflr rN`).
2. For each external data symbol reference, emit:
   ```
   addis rT, rN, ha(__nl_symbol_ptr_for_sym - L0$pb)
   lwz   rT, lo(__nl_symbol_ptr_for_sym - L0$pb)(rT)
   <use rT as the actual address; lwz/stw through it as needed>
   ```
3. Reserve a `__nl_symbol_ptr` slot per external data symbol.

This is the next session.

## Decisions

- **PIC stubs are emitted in `ppc-macho.c`, not `ppc-gen.c`.** The
  Mach-O writer is the right place because it has the full picture
  of which externals are referenced; codegen doesn't need to know
  about stubs.
- **`-Wl,-read_only_relocs,suppress`** is still in the link line.
  It silences the data-symbol issue (#9 above) but doesn't fix it;
  with that suppression the linker leaves the bad reloc in the
  binary and dyld fails at load. The flag will go away once
  external-data PIC indirection lands.
- **`dyld_stub_binding_helper`** is sufficient for our use; we
  don't need to implement custom lazy-bind helpers.

## Exit state

User-mode tcc on the G3 can now compile programs that call
libSystem. `printf`, `malloc`, `fopen`, etc. all work end-to-end.
Self-host (compiling tcc with our tcc and running the result) is
one more session away.

## Next session

017 — external-data PIC indirection in `ppc-gen.c`.
