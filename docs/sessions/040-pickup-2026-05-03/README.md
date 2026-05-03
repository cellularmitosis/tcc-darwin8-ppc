# Session 040 — pickup 2026-05-03

## Arrival state

- HEAD: `527cb81` (docs/sessions/039 HANDOFF + final findings)
- Working tree clean, in sync with `origin/main`.
- Latest release: **v0.2.11-g3** — tests2 109/111 (98.2%) on the
  default `-o exe` path.
- Bootstrap fixpoint holds; `tcc -run hello.c` works.
- Full context for arrival state is in
  [../039-unsupervised-cleanup-2026-05-03/HANDOFF.md](../039-unsupervised-cleanup-2026-05-03/HANDOFF.md).

User instruction: "start with #5 [sqlite3 amalgamation], then proceed
with the rest in the order you deem appropriate. work unsupervised.
document everything, cut releases as appropriate, include a demo in
each release if it makes sense to do so. keep relevant docs updated.
keep working until we have robust end-to-end self-hosting tcc on
darwin8/ppc, or until I stop you."

## Plan

Roughly in order of impact / standalone size:

1. **sqlite3 amalgamation** — retry, fix any backend gaps surfaced.
2. **60_errors_and_warnings** test_scope_1 — enum-param-scope frontend.
3. **96_nodata_wanted** — BE bitfield layout.
4. **lua build attempt.**
5. **Roadmap #3** — Mach-O `tcc -ar` driver.
6. **Roadmap #7** — self-link diagnostics polish.
7. **Roadmap #4** — Mach-O alacarte loader.

## Work log

### sqlite3 amalgamation (3.46.1, 257K lines)

Downloaded amalgamation, pushed to ibookg37, attempted compile.

**Bug #1: struct-deref-by-value.**

```
sqlite3.c:176213: error: ppc-gen: deref of basic type 0x7 not yet supported
```

Line 176213 was `sqlite3AddColumn(pParse, yymsp[-1].minor.yy0, yymsp[0].minor.yy0)`
— passing struct values from pointer-deref by value. Minimal repro:

```c
struct Tok { const char *z; int n; };
void use(struct Tok a);
void f(struct Tok *p) { use(*p); }
```

Fix in [ppc-gen.c:660-674](../../tcc/ppc-gen.c) — added `case VT_STRUCT` to
the `(REG | VT_LVAL)` deref switch in load(). Mirrors the existing
VT_LOCAL+VT_LVAL+VT_STRUCT case: "loading" a struct lvalue whose
address is in a register is just a register move (`mr r, base`).

**Bug #2: cross-TU PIC reloc translation lost.**

After fix #1, sqlite3.c + shell.c both compiled cleanly, linked into
2.2 MB sqlite3 binary. But `./sqlite3 -version` hit SIGBUS in
`__sflush` from `setvbuf(stderr, ...)`. Disasm showed the addis/lwz
pair for `__sF` referenced an address inside `__TEXT/__const`
(0x210684 — random data) instead of the linked binary's
`__nl_symbol_ptr` (0x224830).

Root cause in [ppc-macho.c::macho_translate_relocs:3754](../../tcc/ppc-macho.c):
`if (scattered) continue;` unconditionally skipped all scattered
Mach-O relocs. But the SECTDIFF pairs we ourselves emit for extern
data refs are scattered. So when linking multiple .o files, the
second TU's PIC pairs went unreloced — instructions kept the
addis/lwz immediates the assembler had baked in for the .o file's
local layout, which were no longer valid in the linked binary.

Fix: translate scattered HA16_SECTDIFF / LO16_SECTDIFF relocs to
R_PPC_HA16_PIC / LO16_PIC, resolving the slot via the input indirect
symtab (`macho_resolve_stub_slot`) to the underlying extern. The
trailing PAIR carries the source-object anchor VA — translate it to
the merged-section offset and feed `ppc_pic_pair_record` so our
linker code's `ppc_pic_pairs_lookup` finds it later.

Promoted `ppc_pic_pair_record` from static to `ST_FUNC` so
ppc-macho.c can call it.

**Regression caught + fixed:** test 125_atomic_misc started failing
with "ppc-link: R_PPC_HA16_PIC/LO16_PIC encountered in non-OBJ
output". This test runs via `-run` (per-test T1 override), and -run
mode goes through ppc-link.c which doesn't handle R_PPC_HA16_PIC.
Made the new SECTDIFF translation conditional on output_type ==
OBJ/EXE — for -run, preserve the old skip behavior.

Both fixes committed in
[44c8647](https://github.com/cell-labs/tcc-darwin8-ppc/commit/44c8647).

**State after fixes:**
- tests2: 109/111, no regression.
- Bootstrap fixpoint: still holds.
- sqlite3 builds cleanly: `sqlite3.o` (2.1 MB), `shell.o`, linked
  to 2.2 MB exe.
- `./sqlite3 -version` works (3.46.1 (32-bit)).
- `./sqlite3 .help` works (lists commands).
- `./sqlite3 :memory: "select 1+1"` **crashes**: SIGSEGV at
  0xfffffffc inside an unidentified static helper between
  sqlite3_status64 and sqlite3_deserialize (PC 0x3a3ac in
  sqlite_test2 build).

**Bug #3 (open): NOT YET DIAGNOSED.**

Symptom: anything that calls `sqlite3_open` crashes during open.
`sqlite3_initialize`, `sqlite3_libversion`, `sqlite3_malloc/free`
all work standalone.

The crashing static helper takes a struct pointer arg, accesses
`*(arg + 20)`, treats result as int*, subtracts 4, and dereferences.
The function returns LL (caller and callee both do the r3↔r4 swap).
Loaded value at *(arg+20) is 0, then -4 = 0xfffffffc, then deref
crashes.

Hypothesis paths to explore next:
- Sign-extension miscompile for `int → i64` returns (a small repro
  with the same pattern works correctly, so probably more subtle).
- An earlier function returns NULL where it shouldn't (uninitialized
  field, real bug elsewhere).
- LL calling-convention swap miscompile in some particular call
  pattern.

Without DWARF, we can't easily map PC → source line. Next steps:
either bisect by stubbing out sqlite subsystems, or try lua /
something smaller to see if the bug is sqlite-specific or a
general codegen issue that surfaces at scale.

Decision (see "Cut release?" below): defer deeper sqlite3 debug for
later; ship the two real fixes now and keep moving on the rest of
the plan. The fixes are valuable independent of sqlite3 — Bug #2 in
particular affects ANY multi-TU program that uses extern data
symbols (very common).

### Cut release?

Yes — both fixes shipped have value beyond sqlite3:
- Bug #1: any program passing struct-from-deref by value.
- Bug #2: any program with multi-TU build referencing extern data
  (which is most non-trivial programs).

Plan: cut **v0.2.12-g3** with these two fixes + a sqlite3 demo
showing partial sqlite3 working (`-version` / `.help` succeed even
though `select 1+1` doesn't yet).

(populated as the session progresses)
