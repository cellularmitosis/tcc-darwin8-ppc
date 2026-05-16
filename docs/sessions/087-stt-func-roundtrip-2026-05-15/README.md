# Session 087 — .o-roundtrip STT_FUNC reconstruction (2026-05-15)

**Started:** 2026-05-15
**Arrival HEAD:** `9a0ad5d` (end of session 086, after v0.2.63-g3).
**Tag:** `v0.2.64-g3`.

## TL;DR

`static const struct { name, fn } table[] = {{"alpha", isalpha}, ...}`
patterns now work across `.o` roundtrip. Pre-fix, calling
`table[i].fn(c)` SIGSEGV'd because tcc baked the
`__nl_symbol_ptr` slot address into the table entry. Post-fix, the
entry holds the symbol stub's VA — calling through it loads the
slot via the stub trampoline and branches to the dyld-resolved
function.

GNU sed 4.8's POSIX character classes (`[[:alpha:]]`,
`[[:digit:]]`, `[[:space:]]`) — the canonical sufferer flagged by
session 086 — now work end-to-end.

## The bug, precisely

Mach-O nlist has no `STT_FUNC` bit. So when tcc compiles `foo.c → foo.o`
and the resulting .o is read back at link time:

1. **Codegen → tcc symtab:** `foo_func` has `STT_FUNC`.
2. **Mach-O .o write (`classify_sym`, [ppc-macho.c:1076](../../../tcc/ppc-macho.c)):**
   undef external goes out as `N_EXT|N_UNDF`, no FUNC bit.
3. **Link-time .o read (`macho_translate_sym`, line 7064):** the
   reconstructed undef sym is `STT_NOTYPE`.
4. **`collect_extern_stubs` (line 522):** the
   `R_PPC_ADDR32 + STT_FUNC` clause fails to match → no stub
   allocated for `foo_func`.
5. **`exe_resolve_section_relocs` ADDR32 path (line 1816):**
   `wants_extrel` triggers (it looks like an undef-data ADDR32). For
   writable sections (`__data` / `__init_array` / `__fini_array`),
   v0.2.63 emits an extrel — which actually works for functions too
   in writable sections (dyld writes the function's real VA into the
   data slot, and `mtctr; bctrl` jumps to it). So `__data`-resident
   function-pointer tables are silently correct.
6. **`__TEXT,__const` is where it breaks:** the writable-section
   gate v0.2.63 added (because dyld can't write to `__TEXT`) means
   the ADDR32 falls back to the slot-address path. The table entry
   gets `&__nl_symbol_ptr[foo_func]` — a data slot. Calling through
   it jumps to data memory and SIGSEGVs (or executes garbage
   instructions).

Canonical sufferer: sed's
[`dfa.c::prednames[]`](https://github.com/gnu-mirror/sed/blob/master/lib/dfa.c)
table of ctype function pointers (`{"alpha", isalpha}`, etc.) in
`__TEXT,__const`. Sed's `--version` runs fine because the table is
only walked when a regex contains `[[:class:]]`; any such regex
SIGSEGVs.

## The handoff's prescription, and where it falls short

Session 086 handoff suggested:

> Walk all REL24 relocs across all loaded sections; for each undef
> sym that appears as a REL24 target, mark `ELFW(ST_INFO)` to include
> `STT_FUNC`. Then `collect_extern_stubs` allocates a stub for those
> even when their `__const`-resident ADDR32 ref is the only direct
> evidence.

This assumes the function is also called directly somewhere in the
link. For most code that's true. But on Tiger, `<ctype.h>` defines
`isalpha` etc. as macros that inline to `__istype(c, _CTYPE_A)`:

```c
#define isalpha(c)  __istype((c), _CTYPE_A)
```

So when sed (or any Tiger program) writes `isalpha(c)`, the call
expands to `bl ___istype`, not `bl _isalpha`. The only references
to `_isalpha` in the entire link are the table entries' ADDR32s —
the REL24 heuristic finds none.

`&isalpha` (address-of) is *not* macro-expanded — the C macro
`#define isalpha(c) __istype((c),...)` only matches `isalpha(<expr>)`,
not `isalpha` by itself. So the static-init expression `{ "alpha",
isalpha }` does decay to an `_isalpha` symbol reference. But it's
an ADDR32 in `.rodata`, the very pattern the heuristic was meant to
classify based on *other* evidence.

## Fix design

Two heuristics applied in a single pre-pass at the top of
`collect_extern_stubs`, before the existing
`ST_PPC_NEEDS_STUB`-hint and main loops:

**(a) REL24-target ⇒ function.** Any undef sym referenced via
`R_PPC_REL24` is a function — REL24 is `bl <target>`, and `bl` is
only emitted for function calls. Catches the common case where the
function is called directly somewhere in the link.

**(b) `.rodata`-ADDR32-target ⇒ function (heuristic).** Any undef
sym referenced via `R_PPC_ADDR32` from a section whose name maps to
`__TEXT,__const` (`.rodata`, `.rodata.foo`, `.data.ro`) is *treated*
as a function. The rationale is asymmetric:

| Pattern | Frequency | Pre-fix | Post-fix (heuristic b applied) |
|---|---|---|---|
| `static const fn_ptr arr[] = { ... }` | common | SIGSEGV (slot addr in table) | works (stub VA in table) |
| `const T *const p = &extern_data;` | rare/exotic | wrong but doesn't crash (slot addr) | wrong but doesn't crash (stub VA) |

The exotic case was already broken pre-fix; post-fix it's still
broken, just differently. No real regression. The common case goes
from SIGSEGV to working.

### Why we don't relax the `__TEXT,__const` extrel gate

The v0.2.63 gate excludes `__TEXT,__const` from extrel emission
because dyld can't write into a read-only segment. With the
STT_FUNC heuristics above, function pointers in `__const` don't
need extrels — they get a stub VA written at link time (a fixed,
non-relocated address). The gate stays.

The deeper "right" fix — moving `.rodata` sections that contain
relocations to undef syms into `__DATA,__const` (as gcc-4.0 does on
Tiger) — would give full pointer-equality semantics
(`&isalpha == prednames[0].fn`). It's a larger refactor; see
"Open follow-ups" below.

## Implementation

All change in [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c)
`collect_extern_stubs`, ~30 line addition immediately after
`stub_for[i] = -1` initialization:

```c
for (i = 1; i < s1->nb_sections; i++) {
    Section *s = s1->sections[i];
    Section *sr;
    int nrel;
    int is_rodata;
    if (!s) continue;
    sr = s->reloc;
    if (!sr) continue;
    is_rodata = (!strcmp(s->name, ".rodata")
                 || !strcmp(s->name, ".data.ro")
                 || !strncmp(s->name, ".rodata.", 8));
    nrel = sr->data_offset / sizeof(ElfW_Rel);
    for (k = 0; k < nrel; k++) {
        ElfW_Rel *rel = (ElfW_Rel *)sr->data + k;
        int type = ELFW(R_TYPE)(rel->r_info);
        int symidx = ELFW(R_SYM)(rel->r_info);
        ElfW(Sym) *esym;
        int matches = (type == R_PPC_REL24)
                   || (is_rodata && type == R_PPC_ADDR32);
        if (!matches) continue;
        if (symidx <= 0 || symidx >= nsyms) continue;
        esym = (ElfW(Sym) *)s1->symtab->data + symidx;
        if (esym->st_shndx != SHN_UNDEF) continue;
        if (ELFW(ST_TYPE)(esym->st_info) == STT_FUNC) continue;
        esym->st_info = ELFW(ST_INFO)(ELFW(ST_BIND)(esym->st_info),
                                       STT_FUNC);
    }
}
```

The pre-pass mutates undef syms' `st_info` in place. Downstream
code (`classify_sym`, defined-extern nlist emission, section
validation) keys off `st_shndx` rather than `st_info` for undef
classification, so the type change has no side effects beyond
making `collect_extern_stubs`'s existing
`R_PPC_ADDR32 + STT_FUNC` clause match. The .o write of a relinked
exe would re-emit the sym as `N_EXT|N_UNDF` regardless — the type
change is link-time-only.

Because the pre-pass lives inside `collect_extern_stubs`, all three
callers benefit (exe, dylib, `-run`).

## Verification

On imacg3, post-fix:

### Repro

[`repro/funcptr_const.c`](repro/funcptr_const.c) — `.o` roundtrip
of a `static const` table of ctype function pointers, plus direct
`isalpha`/`isdigit`/`isspace` calls (with `#undef` so the calls
don't expand to `__istype`):

```
=== link ===
=== run ===
prednames[0=alpha](A) = 1 (expected 1)
prednames[1=digit](7) = 1 (expected 1)
prednames[2=space]( ) = 1 (expected 1)
all match
exit=0
```

### Sed (the canonical sufferer)

[`demos/v0.2.40-sed.sh`](../../../demos/v0.2.40-sed.sh) extended
with character-class tests:

```
  [[:alpha:]] works (was SIGSEGV pre-v0.2.64)
  [[:digit:]] works (was SIGSEGV pre-v0.2.64)
  [[:space:]] works (was SIGSEGV pre-v0.2.64)

PASS — GNU sed 4.8 builds + works end-to-end with tcc on Tiger PPC
```

Pre-fix:

```
$ echo abc | ./sed/sed "s/[[:alpha:]]/X/g"
Segmentation fault (gdb backtrace: 0x000692f8 in __nl_symbol_ptr;
caller = dfaparse → dfacomp → dfawarn → compile_regex)
```

Post-fix: `XXX`.

### Regression suite

```
tests2:        111 / 111
bootstrap:     FIXPOINT HOLDS
lua:           PASS (fib(20) = 6765)
bzip2:         PASS (MD5 round-trip)
cjson:         PASS (name=alice age=30; sum=15)
sed:           PASS (now with [[:class:]] tests)
gzip:          PASS (1KB through 500KB round-trips)
dylib-slide:   PASS
alacarte:      PASS
gdebug-extern: PASS
gdebug-link:   PASS
v0.2.63 extern-data-ref: PASS (no regression)
v0.2.64 funcptr-const:   PASS (new demo)
```

### New demo

[`demos/v0.2.64-funcptr-const.{c,sh}`](../../../demos/v0.2.64-funcptr-const.sh)
— compiles a program with a `static const` table of extern ctype
function pointers via the .o-roundtrip path; inspects the linked exe
to verify table entries point into `__TEXT,__symbol_stub` (not
`__DATA,__nl_symbol_ptr`); runs it; asserts function calls return
expected values.

## What did NOT need updating

* The `exe_resolve_section_relocs` ADDR32 path — unchanged from
  v0.2.63. With STT_FUNC restored, `wants_extrel` correctly
  evaluates to false for these syms (the STT_FUNC predicate is
  already there from v0.2.63), and `exe_sym_addr` returns the stub
  address.
* `classify_sym` — undef syms continue to emit as `N_EXT|N_UNDF`
  regardless of `st_info` type. The type mutation is link-time-only;
  re-emitted .o files don't accumulate spurious FUNC bits.
* The writable-section gate — stays in place for the rare
  `const T *const p = &extern_data` case (still falls back to slot
  address; no SIGBUS).

## Open follow-ups

### Pointer-equality semantics for `&fn` in `__TEXT,__const`

The fix gives functional correctness (calling through the pointer
works) but not pointer equality:

```c
const fn_t *p = prednames[0].fn;   /* = stub VA, e.g. 0x1ee0 */
fn_t      *q = isalpha;            /* = libSystem VA, 0x900b13dc */
assert(p == q);                    /* FAILS — different addresses */
```

gcc-4.0 on Tiger gives `p == q` because it places `__const` with
relocs in `__DATA,__const` (writable at load) and emits extrels,
so dyld writes the same libSystem VA into both. Closing this gap
in tcc requires reclassifying `.rodata` sections that contain
ADDR32 relocs to undef syms into `__DATA,__const` at link time —
a larger refactor that touches segment grouping, vmaddr
assignment, and segment-protection bits. Deferred.

### `&extern_data` in `__TEXT,__const`

`static const int *const p = &errno;` still resolves to the slot
address (wrong, but doesn't crash). Same `__DATA,__const`
reclassification would fix it.

### `.o`-roundtrip STT_OBJECT? (probably no)

Mach-O also drops STT_OBJECT. Less obviously consequential since
data references rarely depend on a "this is data" classification —
but worth checking if a real program trips it.

## Subagent log

None this session — direct edits with iterative verification on
imacg3.

## Closing notes

The fix shape mirrors v0.2.23's `STT_FUNC` ADDR32 stub allocation
exactly — same code path, same `collect_extern_stubs` clause. The
only addition is making sure the predicate that gates that clause
(`STT_FUNC`) is actually true at link time even after a .o
roundtrip stripped it.

The `.rodata`-ADDR32 heuristic deserves a callout: it's not
perfectly sound (it would mis-classify a `const T *const p =
&extern_data` ref as a function). The shape of the trade-off is:

```
                 |  function-ptr-in-const  |  data-ptr-in-const   |
                 |  (common, e.g. sed)     |  (exotic)            |
-----------------+-------------------------+----------------------|
heuristic absent | SIGSEGV (slot addr)     | wrong but no crash   |
heuristic present| works                   | wrong (different way)|
```

No real regression — the exotic case was already broken. The
common case goes from crashing to working.

The "right" long-term fix is the section reclassification described
in the follow-ups. Until that lands, the heuristic stands.
