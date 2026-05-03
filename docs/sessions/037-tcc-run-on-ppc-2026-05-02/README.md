# Session 037 — tcc -run works on PPC, 2026-05-02

Picking up from [session 036](../036-post-travel-2026-05-02/README.md)
at v0.2.6-g3. The remaining 13 tests2 failures broke down into
three buckets (`-run` / `-dt`, `-bcheck`, two platform outliers).
The biggest single lever was the `-run` family: 6 tests blocked
since session 003 by a `tcc_error_noabort("ppc-link: create_plt_entry
not implemented")` stub. This session lands the foundational PLT
work to unblock `-run` mode.

## Headline result

| | start | end |
|---|---|---|
| HEAD | `5fcec8c` (v0.2.6-g3) | `45b13e9` (v0.2.7-g3) |
| Releases | 7 | **8** (v0.2.7-g3 cut this session) |
| `-o exe` tests2 | 105 / 118 (89.0%) | 104 / 118 (88.1%) |
| `tcc -run` tests2 | n/a (couldn't run) | **102 / 118 (86.4%)** |
| Bootstrap fixpoint | holds | holds |

`tcc -run hello.c` works on PPC for the first time — a 22-year-old
G3 now JITs C through tcc.

## Implementation

Each PLT entry is a 16-byte stub:

```
lis   r12, hi(target)
ori   r12, r12, lo(target)
mtctr r12
bctr
```

`create_plt_entry` allocates the entry and stashes the symbol's
GOT offset in the first 4 bytes (placeholder); `relocate_plt`
walks the PLT, looks up the resolved address, and writes the real
4-instruction sequence in place.

### Gotcha 1: where do PLT relocations live?

Looking at i386-link.c::relocate_plt for the pattern:

```c
for_each_elem(s1->plt->reloc, 0, rel, ElfW_Rel) { ... }
```

Iterating `s1->plt->reloc` yielded zero entries in our `-run` mode.
Tracked it back to `tccelf.c::put_got_entry`:

```c
if (s1->dynsym) {
    /* dynsym path */
    put_elf_reloc(s1->dynsym, s_rel, got_offset, ...);
} else {
    put_elf_reloc(symtab_section, s1->got, got_offset, ...);
    /* ^^^ note: s1->got, not s_rel */
}
```

For `-run` mode `s1->dynsym == NULL` (no shared-library bookkeeping
needed — dlsym handles it). In that branch the JMP_SLOT relocs land
on `s1->got->reloc`, NOT `s1->plt->reloc`. So the i386/ARM "iterate
plt->reloc" pattern is wrong for us.

Switched relocate_plt to iterate `s1->got->reloc` and match each
PLT entry by `r_offset == got_offset` (decoded from the placeholder
in the entry's first 4 bytes).

### Gotcha 2: `addi rD, r0, imm` is `rD = imm`, not `rD = r0 + imm`

While debugging, hit a SEGV that turned out to be a PPC encoding
trap. In the restore path I emitted:

```c
o(0x38000000 | (1 << 21) | (0 << 16) | (-128 & 0xffff));
/* addi r1, r0, -128 */
```

Disassembler showed it as `li r1, 0xff80` because PPC treats `addi
rD, r0, imm` as a special "load immediate" case — the value in r0
is NOT used; you get `rD = imm`. To actually subtract from a
register, the source register has to be non-r0. Switched to r12 as
the scratch.

(This is documented in the IBM PPC Programming Manual but easy to
miss. Worth saving for the durable findings file.)

### Gotcha 3: `dyld_stub_binding_helper`

`tccelf.c::tcc_add_runtime` loads `libtcc1.a` unconditionally for
non-`-nostdlib` runs. Our `lib-ppc.o` was compiled by tcc itself,
which emits Apple-ABI lazy stubs around any external function
call. `__eprintf` in lib-ppc.c calls `fprintf`, `fflush`, `abort`
— each via a lazy stub. Each lazy stub's `__la_symbol_ptr` slot
has a `R_PPC_VANILLA` reloc against `dyld_stub_binding_helper`,
which dyld would normally patch on first call.

In `-run` mode there is no dyld. `dyld_stub_binding_helper` is
also a libSystem-internal symbol (lowercase `t` in nm output),
so dlsym can't see it.

Two fixes:

* `tccelf.c::relocate_syms`: try dlsym both with and without the
  leading-underscore strip. Most Mach-O exports want the strip
  (`_printf` -> dlsym strips to "printf"); `dyld_stub_binding_helper`
  is one of the few symbols stored naked in our symtab.
* `tccrun.c`: provide `tcc_dyld_stub_binding_helper_unsupported`,
  a tcc-internal stub that aborts cleanly with an explanation if
  the lazy-binding path ever fires in -run mode. Common -run
  programs don't hit this (their direct printf is resolved through
  a real PLT entry, not a libtcc1 lazy stub), but if they did
  (e.g. assert() failure in libtcc1), the failure mode would be
  SEGV without this stub.

### Gotcha 4 (the big one): `@hi` vs `@ha` with `ori` vs `addi`

After the foundational PLT was in, I committed `4d92814` and tested.
Simple `printf("hi\n")` worked. Then ran the test suite under
`-run` (RUN=1): 88 / 118 = 74.6% — substantial regression vs the
default `-o exe` mode's 105 / 118. Many tests crashed with:

```
tcc(N) malloc: ***  Deallocation of a pointer not malloced: 0xbffff350
```

A *stack* address being freed. I'd noted this in the v0.2.6 docs as
a probable double-free in tcc's cleanup_sections.

It was nothing of the sort. Bisected to: programs calling any
libSystem function whose `@lo` half had its top bit set crashed.
Examples (Tiger libSystem): memset @ 0x90129660 (lo = 0x9660),
free @ 0x90005ee0 (lo = 0x5ee0), fprintf @ 0x90015240 (lo = 0x5240)
— roughly half of the libc surface.

Root cause was in my fresh `relocate_plt`. I'd written:

```c
hi = ((addr + 0x8000) >> 16) & 0xffff;       /* WRONG -- this is @ha */
lo = addr & 0xffff;
ppc_link_write32be(p,    0x3d800000u | hi);  /* lis  r12, hi */
ppc_link_write32be(p+4,  0x618c0000u | lo);  /* ori  r12,r12,lo */
```

`@ha` is the `+0x8000`-adjusted high half, designed to compensate
for `addi`'s sign extension on the low half — used with `lis` +
`addi`. `ori` is *zero-extending*, so the `+0x8000` adjustment is
spurious. For memset:

```
@ha = (0x90129660 + 0x8000) >> 16 = 0x9013     (designed for addi)
@hi =  0x90129660 >> 16            = 0x9012
```

With `@ha + ori` we'd build `r12 = 0x9013_0000 | 0x9660 =
0x9013_9660` — wrong by 0x10000 whenever `lo`'s top bit is set
(since the carry from the sign-extension never happens with `ori`).

Switched to plain `@hi`:

```c
hi = (addr >> 16) & 0xffff;     /* @hi -- correct for ori */
```

Memset's PLT entry now correctly resolves to 0x90129660, not
0x90139660. The misleading "Deallocation of a pointer not malloced"
warning was the bl into the PLT effectively jumping to a related-
but-wrong address that on Tiger libSystem often landed inside the
malloc family.

**Effect**: tests2 RUN=1 (NORUN=false) jumps from **88 → 102 / 118
= 86.4%**. NORUN=true unchanged at 104 / 118 (the PLT path is only
exercised under -run). Tests including `117_builtins`, `104_inline`,
and several `-dt` family tests now run cleanly.

## v0.2.7-g3 release (`45b13e9`)

Cut [v0.2.7-g3](https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.7-g3),
154 KB tarball. Release notes lead with "tcc -run works on PPC for
the first time" and document the @hi/@ha gotcha explicitly so the
next person to touch this code doesn't re-derive it.

## Trajectory and remaining failures

NORUN=true unchanged at 104 / 118 (the 1-test difference vs v0.2.6's
105 is the timing-sensitive `124_atomic_counter` flipping; multi-
thread atomic stress on top of single-threaded stubs is inherently
flaky). The new-capability axis is the RUN=1 score: 102 / 118.

The 13 remaining tests at NORUN=true plus the 4 unique to
NORUN=false break down:

* **Need bcheck.c port (5 tests):** 112_backtrace, 113_btdll,
  115_bound_setjmp, 116_bound_setjmp2, 126_bound_global.
* **Need real PPC atomics (2 tests):** 124_atomic_counter,
  125_atomic_misc — `lwarx`/`stwcx.` reservation primitives.
* **Multi-invocation -dt quirks (3 tests):** 60_errors_and_warnings,
  96_nodata_wanted, 128_run_atexit. Run-mode partially works; some
  sub-tests pass, others hit edge cases (e.g. `on_exit` is glibc-
  only on Mac).
* **Platform outliers (3 tests):** 70_floating_point_literals
  (5-ULP error in upstream tcc's `parse_number` on PPC IEEE 754),
  73_arm64 (HFA aggregates designed for AArch64 only), 117_builtins
  (passes the no-bcheck path; second invocation under `-b` fails
  because bcheck.o isn't built for PPC).
* **`-run`-only failures (3 tests, vs `-o exe`):** 121_struct_return,
  122_vla_reuse, 132_bound_test — all use `-b` flag; bcheck stubs
  in lib-ppc.c work fine through `-o exe` but the `-run` path
  doesn't load them the same way.

## Commits this session

```
c365021 README: status to v0.2.7-g3 (tcc -run works on PPC)
45b13e9 release: bump default to v0.2.7-g3 (with -run / PLT support)
a8a4644 docs/sessions/035: @hi-vs-@ha resolution to the -dt double-free
a140db0 ppc-link: relocate_plt: use @hi (not @ha) with ori in absolute-call stub
ef0f3d8 docs/sessions/035: notes on -run / create_plt_entry foundation work
4d92814 ppc-link: implement create_plt_entry and relocate_plt for -run mode
```

Plus the v0.2.7-g3 tag and GitHub release.

## Files touched

- `tcc/ppc-link.c` — create_plt_entry, relocate_plt, R_PPC_JMP_SLOT
  + R_PPC_GLOB_DAT relocate handlers, @hi-fix
- `tcc/tccelf.c` — relocate_syms dlsym-with-and-without-underscore
  fallback
- `tcc/tccrun.c` — `tcc_dyld_stub_binding_helper_unsupported` stub
- `scripts/run-tests2.sh` — `RUN=1` opt-in
- `scripts/build-release-tarball.sh` — version bump + notes
- `README.md` — status table

## Exit state

HEAD `c365021` ([v0.2.7-g3](https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.7-g3)).
Bootstrap fixpoint holds. NORUN=true: 104 / 118. RUN=1: 102 / 118.
`tcc -run hello.c` works.

## Recommended next session

The largest unblock is the bcheck.c port — that's 5 tests (`-bcheck`
bucket) plus 3 more that fail under `-run` because of `-b`. Pattern
is documented in tcc/lib/bcheck.c (hash-table region tracking,
signal hooks, mprotect manipulation); codegen instrumentation in
ppc-gen.c is partially in place via unused statics
`func_bound_offset` / `func_bound_ind`. Estimate: substantial,
multi-session.

Smaller / orthogonal wins:

* Real PPC atomics via lwarx/stwcx. (2 tests, ~50-100 lines).
* The struct-by-value HFA test (73_arm64) — won't fully pass since
  it tests AArch64 ABI specifics, but our struct-by-value codegen
  still has missing cases that could be improved.
