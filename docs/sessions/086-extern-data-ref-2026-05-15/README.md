# Session 086 — extern *data* reference dyld bind (2026-05-15)

**Started:** 2026-05-15
**Arrival HEAD:** `f4abf9e` (end of session 085).
**Goal:** Close the long-deferred extern-data-reference correctness gap
called out by session 044 and recorded on `docs/roadmap.md` item #10:

> *Extern data references (`extern int errno; int *p = &errno;`) still
> resolve to the `__nl_symbol_ptr` slot address rather than the dyld-
> bound target — needs dyld bind-info emission in the Mach-O exe writer,
> no in-tree program hits it.*

## The bug, precisely

For an `int *p = &optind;` style static init (where `optind` is an
`extern int` resolved by libSystem), tcc currently stores the address
of the `__nl_symbol_ptr[optind]` slot into `p` instead of `&optind`.

`exe_resolve_section_relocs` ([tcc/ppc-macho.c](../../../tcc/ppc-macho.c)
line 1697–1703) falls back to `nlptrs[...].slot_addr` for any
`SHN_UNDEF` symbol that `exe_sym_addr` can't resolve. That's the right
behavior for `R_PPC_ADDR16_HA/HI/LO` (where `lis r3, ha(slot); lwz r3,
lo(slot)(r3)` does its own indirection through the slot), but it's
wrong for `R_PPC_ADDR32`: there's no second-step indirection for a
plain 32-bit data word, so `p` ends up pointing at the slot, not at
`&optind`.

The fix gcc uses is to leave the in-place value at zero (or whatever
addend was emitted) and emit a classic Mach-O **external relocation
entry** in `__LINKEDIT` that tells dyld "at load time, write the
address of `_optind` into VA *X*." Tiger's dyld processes external
relocations through `doBindExternalRelocations`; for us today that
table is empty (`extreloff=0, nextrel=0` in LC_DYSYMTAB) — no externs
ever flow through the dyld binding path. Once we emit them, dyld
fills `p` with `&_optind` at startup.

## Confirmation of the bug, before any change

[`repro/extern_data.c`](repro/extern_data.c) is a 22-line program
using `<unistd.h>`'s `extern int optind`. `optind` is a real `int`
data symbol in libSystem (`a000056c D _optind`), so it makes a clean
test. On imacg3:

```
=== gcc baseline ===
&optind = 0xa000056c
p       = 0xa000056c
optind  = 7
*p      = 7
PASS

=== tcc current ===
&optind = 0xa000056c
p       = 0x2038
optind  = 7
*p      = -1610611348      # = 0xa000056c interpreted as int
FAIL
```

`*p = 0xa000056c` is the giveaway: tcc baked the address of
`__nl_symbol_ptr[optind]` (a slot at VA 0x2038 in this exe) into `p`,
and dyld filled that slot with `&optind` — so `*p` reads back the
slot's contents, which is `&optind` cast to int. Confirms the
diagnosis exactly.

LC_DYSYMTAB comparison:

```
                tcc                gcc
extreloff       0                  12996
nextrel         0                  1
```

gcc emits one external reloc:

```
00003014 False long   True   VANILLA False     _optind
```

at VA 0x3014 (the location of `p` inside `__data`), telling dyld:
"write `&_optind` here." tcc emits none.

## Audit method

Searches used:

```sh
grep -n "extreloff\|nextrel\|R_PPC_ADDR32\|exe_resolve\|nl_for_elfsym" \
    tcc/ppc-macho.c
```

Cross-checked the gcc baseline binary with `otool -l`, `otool -rv`,
`otool -dV`, `otool -Iv` to confirm the desired output shape, then
walked tcc's `exe_resolve_section_relocs` to identify the exact line
where the wrong value is baked in.

## Fix design

### Where the value goes wrong

[tcc/ppc-macho.c:1697](../../../tcc/ppc-macho.c) — the slot-address
fallback for any undef symbol that `exe_sym_addr` can't resolve. For
`R_PPC_ADDR32` against an undef *data* symbol, this fallback returns
`slot_addr` and the `R_PPC_ADDR32` case at line 1786 does
`inst += target_addr` — baking the slot address into the in-place
word.

### What to emit instead

For each `R_PPC_ADDR32` reloc whose target is `SHN_UNDEF` and **not**
`STT_FUNC` (function ADDR32s already get a stub via
`collect_extern_stubs` and resolve correctly):

1. Leave `target_addr = 0`, so `inst += target_addr` keeps the in-place
   value at whatever addend was already there (typically 0 for
   `int *p = &sym;`).
2. Push an external-relocation record `{ r_address, elfsym_idx,
   length=2 }`.

After the nlist is built and `data_sym_idx[]` populated (which maps
ELF symtab indices to output Mach-O `nlist` indices), resolve each
`elfsym_idx → machsym_idx` and emit the classic Mach-O
`struct relocation_info` (8 bytes each, BE) into a fresh `obuf`. The
encoding:

```
word1 = r_address                   ; VA in our laid-out image
word2 = (symnum << 8)
      | (pcrel  << 7)               ; 0
      | (length << 5)               ; 2 (= long, 4 bytes)
      | (extern << 4)               ; 1
      | (type   << 0)               ; 0 = GENERIC_RELOC_VANILLA
```

### Layout in __LINKEDIT

Add the external-relocation table between `nlist` and `indirect` in
__LINKEDIT (the order that matches gcc-4.0's exe layout). Update
`extreloff` / `nextrel` in `LC_DYSYMTAB`.

### What does NOT change

* `collect_extern_nlptrs` still allocates an nlptr slot for
  ADDR32-against-undef-data (so existing slot-based code paths still
  work). Having both a slot and an external reloc is wasteful but
  harmless — the slot is unreferenced by anything when ADDR32 is the
  only use of the symbol; if PIC code also references the same
  symbol, the slot remains required.
* The `R_PPC_ADDR16_HA / HI / LO` fallback at line 1697 (used by
  crt1.o's `lis r3, ha(slot); lwz r3, lo(slot)(r3)`) stays — those
  legitimately want the slot address as the target.
* `STT_FUNC` ADDR32 path stays — function pointers in static init
  data continue to use the stub-address shim that v0.2.23 added.

### Edge cases

* **Nonzero addend** (`int *p = &optind + 5;`): classic Mach-O
  VANILLA external relocs are treated by Tiger's dyld as ADD ops
  (`*location = *location + symbolAddress`), not OVERWRITE — so
  leaving the addend in the in-place word should produce
  `addend + &optind` at runtime. Need to verify empirically.
* **Cross-TU `extern X` that turns out to be a locally-defined data
  symbol in another TU**: not undef at link time, so doesn't hit
  this path.
* **Weak undef extern data**: tcc still marks the nlist entry weak
  (`N_WEAK_REF`), and dyld will leave the slot at zero if unresolved.
  For the external reloc, dyld also leaves the location at zero (no
  binding occurs), so the addend stays — that matches gcc's behavior
  for `__attribute__((weak)) extern int x;` style.

## Implementation

All changes in [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c), self-contained
to the EXE writer:

1. **New struct `exe_extrel`** + three new fields on
   `exe_reloc_ctx` (`extrels_out`, `n_extrels_out`,
   `extrels_cap_out`) — bookkeeping for one pending external
   relocation. `elfsym_idx` is filled at collect time, replaced
   with the machsym index at nlist-build time.

2. **`exe_resolve_section_relocs` ADDR32 path** — when the symbol is
   `SHN_UNDEF && !STT_FUNC && !has-stub`, push an extrel and set
   `target_addr = 0` so the in-place addend stays (`inst += 0`).
   Tiger's dyld treats VANILLA externs as ADD, so this gives
   `addend + symbolAddress` at runtime, matching gcc's expectation.

3. **`macho_output_exe`** — new locals `extrels`, `n_extrels`,
   `extrels_cap`, `extrel_file_off`. Enable extrel collection only
   for the writable sections (data / init_array / fini_array);
   text / rodata / eh_frame / dwarf keep the legacy slot-address
   fallback because their containing segments are read-only at
   load time and would SIGBUS if dyld tried to write into them.

4. **Post-nlist resolution** — extend the existing
   `elfsym_to_undef[]` pass to walk extrels too: defensively add an
   nlist UNDEF entry for any extrel sym not already in the
   deduped pool (shouldn't happen in practice — `collect_extern_nlptrs`
   already picks up undef+ADDR32 — but keeps the code robust).
   Rewrite each extrel's `elfsym_idx` field to the machsym index.

5. **__LINKEDIT layout** — insert `n_extrels * 8` bytes between
   nlist and the indirect symbol table (matches gcc-4.0's layout
   order). Set `extrel_file_off`.

6. **LC_DYSYMTAB write** — replace the two `put32be(&out, 0)` placeholders
   for `extreloff` / `nextrel` with the real values.

7. **File payload** — between writing nlist and indirect, write
   `n_extrels` `relocation_info` records (each via two `put32be`:
   `r_address` then `pack_reloc_word(symnum, pcrel=0, length=2,
   extern=1, type=PPC_RELOC_VANILLA)`).

8. **Cleanup** — `tcc_free(extrels)` at the `cleanup:` label.

9. **Dylib writer compatibility** — `macho_output_dylib`'s
   `exe_reloc_ctx` setup explicitly NULLs the three new fields, so
   the shared `exe_resolve_section_relocs` helper skips extrel
   collection (the dylib bind-info emission would need additional
   load-command plumbing; deferred).

The diff is ~80 lines net across one file.

## Verification

On imacg3:

```
=== primary repro (extern_data.c) ===
&optind = 0xa000056c
p       = 0xa000056c                     # was 0x2038 pre-fix
optind  = 7
*p      = 7                              # was -1610611348 pre-fix
PASS

=== nonzero-addend repro (extern_data_addend.c) ===
&optind     = 0xa000056c
&optind + 3 = 0xa0000578 (expected)
p_plus3     = 0xa0000578 (got)
PASS (dyld ADDs)

=== .o-roundtrip repro ===
&optind = 0xa000056c
p       = 0xa000056c
*p      = 7
PASS                                     # extrel still emitted, dyld still binds

=== otool -rv on the primary repro ===
External relocation information 1 entries
address  pcrel length extern type    scattered symbolnum/value
00002000 False long   True   VANILLA False     _optind
```

Regression sanity:

```
tests2:        111 / 111
bootstrap:     FIXPOINT HOLDS
lua:           PASS (fib(20) = 6765)
bzip2:         PASS (MD5 round-trip)
cjson:         PASS (name=alice age=30; sum=15)
sed:           PASS (was the first regression — sed --version SIGBUS'd
                    when extrels were emitted into __TEXT,__const;
                    fixed by gating extrel emission on writable
                    sections only)
gzip:          PASS (1KB through 500KB round-trips)
dylib-slide:   PASS
alacarte:      PASS
gdebug-extern: PASS
gdebug-link:   PASS
```

Demo: [`demos/v0.2.63-extern-data-ref.sh`](../../../demos/v0.2.63-extern-data-ref.sh)
— compiles a program with `int *p = &optind; int *q = &optind + 3;`,
verifies the linked exe has `nextrel = 2` in `LC_DYSYMTAB`, runs it,
asserts `p == &optind && *p == 7 && q == &optind + 12`.

## What did NOT need updating

* `collect_extern_stubs` (line 522) — the existing `STT_FUNC ADDR32`
  case is the v0.2.23 stub-allocation logic; for STT_FUNC symbols
  `exe_sym_addr` returns the stub address, so `wants_extrel` is
  false and we never emit an extrel. Unchanged.
* `collect_extern_nlptrs` (line 617) — keeps allocating an
  `__nl_symbol_ptr` slot for every undef ADDR32 ref, even when
  we'll also emit an extrel. The slot is wasted in that case
  (nothing reads it) but harmless; if the same sym is also used
  from PIC code (`R_PPC_HA16_PIC / R_PPC_LO16_PIC`), the slot is
  required.
* The post-write linter (line 2147+) already validates
  `extreloff` / `nextrel` against file size — no change needed.

## Subagent log

None this session — direct edits to a known file with verified
results on each step, no exploration delegated.

## Closing notes

The fix shape matches gcc-4.0's exactly: a `VANILLA` external
relocation in `__LINKEDIT`. Tiger's `doBindExternalRelocations`
is the dyld entry point that processes the table. The two-step
process (collect during resolution, resolve after nlist build) was
necessary because the `elfsym_idx → machsym_idx` mapping isn't
available until the nlist undef-extdef block has populated
`elfsym_to_undef[]`.

The writable-section gate caught what would have been a real
regression on sed. Worth understanding the failure mode: **Mach-O
nlist has no STT_FUNC bit**, so when tcc compiles `dfa.c → dfa.o`,
the `_isalpha` undef sym loses its function-ness on disk. At link
time, when tcc reads dfa.o back, the recovered ELF sym is
STT_NOTYPE. The pre-existing `collect_extern_stubs` STT_FUNC check
fails, so no stub is allocated for the `R_PPC_ADDR32` in the
`prednames[]` table. Without the writable-section gate, my new
code would have emitted an external relocation into the
`__TEXT,__const` location of `prednames[X].func`; dyld at load
time would have tried to write there, hit the read-only mapping,
and SIGBUS'd before `main` ran.

A future session could fix the underlying STT_FUNC-loss problem
by recovering the function-ness from cross-TU REL24 references
at link time (any sym ever called via REL24 in the link is a
function; otherwise treat as data). That would let extrels be
safely emitted into `__const` too, but it's a separate workstream.

The dylib writer hasn't been updated — it still falls back to
the legacy slot-address behavior for undef-data ADDR32 refs.
That's harder: dylibs are slid by dyld, so r_address would need
to be relative to the dylib's load address; also dylib syms
themselves are exported via different mechanisms (LC_ID_DYLIB,
not LC_UNIXTHREAD's entry). Tackling that is a follow-up.

