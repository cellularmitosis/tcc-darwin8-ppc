# Session 044 — unsupervised stretch (2026-05-05)

User asked for a long autonomous run: "make tcc on darwin/tiger
100% bug free… don't worry about what will fit in one session."

Picking up from session 043's HANDOFF: HEAD `05dd9a3`, latest
release v0.2.21-g3, tests2 111/111, tcctest diff 44 lines, 4
real-world programs working (lua, zlib, bzip2, cJSON), open
work item #1 = `relocation_test` static-init `&arr[N]` addend
bug (4 prior attempts in session 043 broke bootstrap).

## Arrival state

* `git log -1`: `05dd9a3 docs/sessions/043: HANDOFF for next session`.
* Bootstrap fixpoint holds; tests2 111/111.
* Open work item #1: relocation_test `*rel1=1 *rel2=1` divergence
  (deferred since session 041; 4 attempts in 043).

## Bug 1: `&arr[N]` addend dropped on PPC ADDR32 relocations

### Reproduction (in-memory single-TU)

```c
int reltab[3] = { 100, 200, 300 };
int *rel1 = &reltab[1];
int *rel2 = &reltab[2];
int main(void) {
    printf("*rel1=%d (want 200)\n", *rel1);
    printf("*rel2=%d (want 300)\n", *rel2);
}
```

Pre-fix output: `*rel1=100`, `*rel2=100`. Both pointers resolve
to `&reltab` (offset 0) — addends 4 and 8 were lost.

Same pattern is also broken via `.o` roundtrip and across TUs
with extern (`int *rel1 = &extern_arr[1];`).

### Root cause

PPC's `relocate()` for `R_PPC_ADDR32` (in `tcc/ppc-link.c:229`)
overwrote the in-place data with the symbol's value:

```c
ppc_link_write32be(ptr, (uint32_t)val);
```

But TCC uses ELF Rel-format (no `r_addend` field) — the addend
is implicit in the in-place value (set by `init_putv` in
tccgen.c when the user writes `int *p = &arr[N]`). The correct
operation is **add**, not write. i386's `R_386_32` does this
correctly (`add32le(ptr, val)`).

The Mach-O EXE writer's `exe_resolve_section_relocs()` had the
same overwrite bug (`inst = target_addr`).

The .o → exe roundtrip path had a third twist: the .o loader
(`macho_translate_relocs`) extracts the addend from the in-place
value into a synthetic "anchor" symbol with
`st_value = sec_to_off + addend`. After this absorption the
in-place value is no longer the addend — it's `section_base +
addend`. If we naively switched to ADD semantics in the
relocator, we'd double-count the section base. So the fix also
zeros the in-place value at load time after the anchor absorbs
the addend.

### The fix (3 small changes)

1. `tcc/ppc-link.c::relocate()` for `R_PPC_ADDR32`: ADD, not
   write. (Single-TU exe and JIT paths.)
2. `tcc/ppc-macho.c::exe_resolve_section_relocs()` for
   `R_PPC_ADDR32`: `inst += target_addr` (was `inst =
   target_addr`).
3. `tcc/ppc-macho.c::macho_translate_relocs()` after creating
   the local-section anchor for VANILLA ADDR32: zero out the
   4 bytes of in-place value, so ADD semantics doesn't
   double-count `section_base + addend`.

The .o emit's pre-resolution path now writes Mach-O-conformant
VANILLA values (`section_base + addend` instead of just
`section_base`) as a side effect — which fixes the
.o-roundtrip path even before the loader adjustment lands,
because the loader's existing anchor logic starts producing
correct merged offsets again.

### Verification

```sh
# All three paths (in-memory, .o-roundtrip, cross-TU extern)
==single-TU==
*rel1=200 (want 200)
*rel2=300 (want 300)
==.o roundtrip==
*rel1=200 (want 200)
*rel2=300 (want 300)
==cross-TU extern==
*rel1=200 (want 200)
*rel2=300 (want 300)
```

* tests2: 111/111 (unchanged)
* Bootstrap fixpoint: still holds.
* tcctest.c diff vs gcc reference: **44 → 33 lines**
  (relocation_test divergence gone). Remaining 33 lines are all
  in the previously-classified "won't fix" buckets (Apple ABI
  `aligntest3/4`, gcc-4.0 `_Bool` size, undefined-behavior
  `promote char/short funcret`).

* abitest-tcc best-of-15: **19 / 30 passing** (up from
  HANDOFF's 14). Worst case is still 5 — the JIT-context
  Bus-error heisenbug is unchanged but the new ceiling is
  higher.

## Demo

[`demos/v0.2.22-relocation-addends.c`](../../../demos/v0.2.22-relocation-addends.c) — three
flavors of `&arr[N]` static init (single-TU, .o roundtrip,
cross-TU extern), all printing the expected values.

## Subagent log

(none in this section yet; the bug investigation was tightly
coupled to single-file changes and didn't fit a subagentable
shape.)
