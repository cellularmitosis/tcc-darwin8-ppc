# 017 — External-data PIC indirection

Closes the second of the two PIC blockers identified in 016.
External data references like `errno`, `stdin`, `__sF` now route
through `__nl_symbol_ptr` slots reached via a per-function PIC base
register. Programs combining `printf` + `extern int errno` link and
run end-to-end on the G3.

`tcc-self` (the bootstrap binary) links cleanly without the
`-read_only_relocs,suppress` workaround, but **`tcc-self -v` still
aborts with SIGBUS (exit 138)** — a separate codegen bug, not
PIC-related. Investigation deferred to session 018.

## What was added

### `tcc/ppc-gen.c` (+373 lines)

- **Per-function PIC base.** First time `load`/`store` needs PIC in
  a function, emit:
  ```
  mflr r0
  bcl  20, 31, .+4    ; subroutine call to next insn
  mflr r30            ; r30 = PIC anchor
  mtlr r0             ; restore caller's LR
  ```
  `r30` chosen as PIC base register: callee-saved per Apple PPC
  ABI, so it survives intervening libSystem calls. Tcc's allocator
  never picks r30 (only r3..r12 exposed). r30 is saved/restored in
  prolog/epilog at `frame_size-8` (slot below the existing r31
  save). `PROLOG_SIZE` bumped 52 → 56.
  
  Initial attempt used `r2` (also conventionally a PIC base), but
  libSystem clobbers r2; calls into printf/etc. would corrupt the
  PIC anchor.

- **Synthetic relocs `R_PPC_HA16_PIC` / `R_PPC_LO16_PIC`** (= 200/201).
  ppc-gen.c emits these for the addis/lwz pair that loads a
  __nl_symbol_ptr slot's address; ppc-macho.c translates them at
  output time to scattered `PPC_RELOC_HA16_SECTDIFF` /
  `LO16_SECTDIFF` + `PAIR` records.

- **`ppc_need_pic_for_sym(Sym *)`** — returns 1 only when
  `output_type == TCC_OUTPUT_OBJ` AND the symbol is an undef
  external. Internal symbols and `-run` mode keep the existing
  direct `lis ; lwz/stw sym@lo` path (no PIC needed).

- **`load()` / `store()` rewrite.** When need_pic, emit:
  ```
  addis rT, r30, ha(slot - anchor)
  lwz   rT, lo(slot - anchor)(rT)
  ; then the existing typed access:
  lwz/stw rD, 0(rT)
  ```

- **Side table `ppc_pic_pairs[]`** maps each PIC reloc's offset →
  its function's anchor offset, read by ppc-macho.c when emitting
  SECTDIFF relocs.

- **Bug fix in `gfunc_call` indirect path.** Materialization changed
  from `gv(RC_INT)` (which could pick r3 and clobber the first arg)
  to `gv(RC_R(8))` = r11. (r3 is the first-arg register; using it
  for the function pointer corrupts arg-passing.)

### `tcc/ppc-link.c` (+29 lines)

- `R_PPC_HA16_PIC` / `R_PPC_LO16_PIC` constants exposed to all TUs
  via `tcc.h` chain.
- `code_reloc()` / `gotplt_entry_type()` updated: PIC types return
  `0` / `NO_GOTPLT_ENTRY`.
- `relocate()` raises `tcc_error_noabort` if PIC types reach the
  runtime path — they shouldn't (PIC is only emitted for
  `TCC_OUTPUT_OBJ`).

### `tcc/ppc-macho.c` (+476 lines)

- **`S_NON_LAZY_SYMBOL_POINTERS = 0x6`** section type constant.

- **`collect_extern_nlptrs()`** dedups extern data symbols
  referenced via `R_PPC_HA16_PIC` / `R_PPC_LO16_PIC`.

- **Synthesized `__DATA,__nl_symbol_ptr` section.** 4 bytes per
  slot, between `__picsymbolstub1` and `__la_symbol_ptr` to match
  gcc-4.0 layout.

- **Indirect symbol table layout** updated: stubs first, then
  nl_ptrs, then la_ptrs. `reserved1` for each section is the
  starting index in that combined table.

- **`emit_section_relocs()`** translates PIC types to scattered
  `PPC_RELOC_HA16_SECTDIFF` / `PPC_RELOC_LO16_SECTDIFF` + `PAIR`
  records. `PAIR.r_address` holds the OTHER 16-bit half of the
  `(slot - anchor)` displacement; `PAIR.r_value` is the anchor VA.

- **Pre-relocation pass** pre-fills the addis/lwz instruction
  immediates with `(slot_va - anchor_va)` so the SECTDIFF relocs
  apply a section-shift delta only.

- **Forward-reference handling.** When ppc-gen.c emitted PIC for a
  symbol that turns out to be defined locally later in the same TU
  (common in tcc.c sources where `extern int loc;` precedes
  `int loc;`), the macho writer rewrites the
  `lwz rT,lo(...)(rT)` → `addi rT,rT,lo(...)` (same field layout,
  opcode `0x80` → `0x38`) and emits SECTDIFF directly to the
  symbol VA (no slot needed). Matches gcc's pattern for
  defined-locally PIC refs.

- **Bug fix in pre-existing PAIR emission.** HA16/LO16 PAIRs now
  carry the LO/HA half of the symbol's resolved value (was always
  0 before, which made ld compute the wrong HA when bit 15 of LO
  is set — broke `lis ; lwz` against any symbol >= 0x8000 from
  section base).

## Test record

| Test | Result |
|---|---|
| `int main(){return 42;}` via -run | exit 42 ✓ |
| `extern int errno; int main(){return errno;}` compile + gcc-link + run | exit 0 ✓ |
| `printf("hi\n")` compile + gcc-link + run | prints `hi`, exit 0 ✓ |
| `printf("hi"); return errno;` (combined extern data + extern fn) | works ✓ |
| Multi-stub demo (strdup + printf + strlen + free) | works, exit 0 ✓ |
| All 11 tcc sources compile via tcc | 11/11 OK ✓ |
| `tcc-self` link (no `-read_only_relocs` workaround) | succeeds ✓ |
| `tcc-self -v` | **SIGBUS, exit 138** ❌ |

## What's still blocking actual self-host

`tcc-self -v` aborts with SIGBUS in the error-reporting path. Crash
chain (per follow-up investigation):
```
strcpy(NULL)
  ← get_tok_str(0)
  ← pp_error
  ← error1
  ← tcc_error_noabort
  ← main
```

It's NOT a PIC issue (`DYLD_PRINT_BINDINGS` confirms all
__nl_symbol_ptr slots resolve correctly). `tcc-self` enters
`error1` with no live token, which suggests the control flow in
`main` is miscompiled (the `if (n == 0)` / `if (opt)` argument
parsing path).

Likely cause: another instance of the arg-register clobbering bug
that was just fixed in `gfunc_call`'s indirect-call path. Possibly
the `gv(RC_INT)` in `gfunc_call`'s stack-arg-spill path
(`ppc-gen.c:1158`) has the same issue.

## Decisions

- **r30 as PIC base.** Callee-saved means it survives libSystem
  calls. Costs 8 extra bytes per frame (save + restore) and 16
  bytes per function for the bcl-mflr setup; acceptable.

- **PIC indirection ONLY for `TCC_OUTPUT_OBJ`.** -run mode resolves
  symbols in-process via tcc's symbol table; no need for the
  __nl_symbol_ptr slot machinery. Direct `lis sym@ha ; lwz
  sym@lo(rA)` works because the relocator runs in the JIT's
  address space.

- **Forward-reference rewrite (lwz → addi).** Mirrors what gcc
  does. We could conservatively always go through a slot, but
  matching gcc's pattern keeps the .o files closer to the
  reference and avoids unnecessary slot proliferation.

## Known limitations / TODOs

1. `tcc-self -v` SIGBUS (next session's focus).
2. `__floatundidf` helper missing from PPC `libtcc1.a`. Worked
   around by gcc-compiling a tiny stub object alongside the link.
   Should be added to `tcc/lib/` properly.
3. `gv(RC_INT)` in stack-arg-spill path (`ppc-gen.c:1158`) has the
   same potential clobbering issue as the now-fixed indirect-call
   path. Hasn't bitten any test yet, but worth fixing
   prophylactically.
4. No PIC for tentative-definition COMMON symbols. Currently
   treated as defined-locally. Probably correct (linker resolves
   them in our object), but not validated end-to-end.

## Exit state

End-user tcc on the G3 can now compile programs that mix `printf`,
`malloc`, `extern int errno`, and other external data/function
references. The infrastructure for self-host is structurally
complete. One specific codegen bug remains before `tcc-self` itself
runs.

## Next session

018 — debug and fix the `tcc-self -v` SIGBUS. Crash chain points at
`main`'s argument-parsing code; root cause is most likely an
arg-register clobbering bug similar to the one fixed in 017's
`gfunc_call` indirect path.
