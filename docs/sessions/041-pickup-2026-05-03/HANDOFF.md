# Handoff — end of session 041 (2026-05-03)

Captured for the next session. Resume cold from this doc.

## TL;DR

- HEAD: `738ec79` (sqlite-debug pinpoint).
- Latest release: **v0.2.16-g3** (three releases shipped this
  session: v0.2.14, v0.2.15, v0.2.16).
- tests2: **111 / 111 (100.0%)** — first time at 100%.
- Bootstrap fixpoint holds; `tcc -run hello.c` works.
- **zlib 1.3.1 full test suite passes** — second non-trivial
  third-party program that works end-to-end.
- **sqlite in-memory SELECT works** — `./sqlite3 :memory: "select
  1+1"` returns 2; `select hex(1234)` returns "31323334".
- Two known sqlite issues remain: `sqlite3_open("/tmp/foo.db",
  ...)` SEGVs, and `:memory: "create table t(x)"` returns
  SQLITE_CORRUPT. Both deferred — see "Open work".
- All work committed and tagged on `origin/main`.

## What landed since session 040 (v0.2.13-g3)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.14-g3 | `e929b86`..`66874ff` (2) | **111/111 (100%)** | BE bitfield read-back fix — force byte-wise paths on PPC32 BE |
| v0.2.15-g3 | `bc1ff4d`..`560ae06` (2) | 111/111 | Apple PPC ABI long-long alignment + LL field-load address-clobber. **sqlite3_open works.** |
| v0.2.16-g3 | `bc50586`..`49132a4` (2) | 111/111 | LL-arg shuffle clobber in gfunc_call. **sqlite SELECT works; zlib full test suite passes.** |

Roadmap items closed: none (open structural items remain). The
"concrete remaining test failures" section was rewritten to "(0)
— tests2 is at 100% since v0.2.14."

## Bugs fixed in detail

All four bugs were latent codegen issues that escaped earlier
attention because tcc's own use of long long, bitfields, and
struct fields happens to dodge the bad paths. They surface in
sqlite, zlib, and any third-party code that exercises them.

### 1. BE bitfield read-back (`tccgen.c::struct_layout` FIX pass)

tcc has two bitfield write paths and two read paths that must
agree on memory layout:

* Static initializer (`init_putv`, line 7945+) → byte-wise,
  LSB-first within each byte.
* `store_packed_bf` → byte-wise, LSB-first within each byte.
* Wide-container store/load (when `auxtype != VT_STRUCT`) →
  integer-wise with shift+mask, LSB-first within the loaded
  *integer*.

On LE these agree because byte order matches bit numbering. On BE
they don't: "LSB-first within byte 1" and "LSB-first within a 4-
byte int spanning bytes 0..3" point at different memory bits. A
static `s = {0x333}` for `unsigned x : 12` writes
`byte0 = 0x33; byte1[0..3] = 0x3` and the wide-int read can't
recover those bits.

Fix: in `struct_layout`'s FIX pass, set `f->auxtype = VT_STRUCT`
for every bitfield on PPC32 (gated on
`defined(TCC_TARGET_PPC) && !defined(TCC_TARGET_PPC64)`). That
funnels every access through `load_packed_bf` /
`store_packed_bf`, which agree with the static initializer.

Cost: ~2-4 byte loads per access vs 1 int load + shifts. Memory
layout still differs from gcc on BE (gcc places the first declared
field at the MSB end of the container); we keep tcc's LSB-first
byte-wise layout. That deeper fix would need MSB-first numbering
in `struct_layout`, but no test we ship requires it.

Test that flipped: `tests/tests2/96_nodata_wanted
test_data_suppression_off`.

Repro outside the test suite:
```c
struct A { unsigned x : 12; unsigned y : 20; } a = { 0x333, 0x55555 };
/* tcc before fix: a.x reads 0x555, a.y reads 0x33535. With fix: ✓ */
```

Commit: `e929b86`.

### 2. Apple PPC ABI long-long alignment (`tccgen.c::type_size`)

tcc was using **8-byte** alignment for `double` and `long long`
inside structs (the SysV/Linux PPC convention). Apple PPC32 ABI
uses **4-byte** — matches the original m68k Mac ABI; gcc-4.0 on
Tiger uses it; sqlite, lua, zlib all expect it.

Concretely: sqlite's `PCache.nRefSum` (i64) landed at offset 16
in tcc but offset 12 in gcc, so `sqlite3PcacheRefCount` read junk.
That field is then used inside `sqlite3PagerSetPagesize` very
early in `sqlite3_open`, so the bug fired immediately on any
real-world program.

Fix: gate `*a = 4` in `tccgen.c::type_size` on
`defined(TCC_TARGET_PPC) && !defined(TCC_TARGET_PPC64)` in
addition to the existing i386 / ARM-non-EABI cases.

After the fix, tcc-built `PCache` matches gcc-built `PCache`
exactly.

Repro:
```c
typedef struct {
    void *p1; void *p2; void *p3;
    long long ll;
    int x;
} S;
/* sizeof: tcc was 32, gcc was 24, now both 24.
   offsetof(ll): tcc was 16, gcc was 12, now both 12. */
```

Commit: `bc1ff4d`.

### 3. LL field-load address-clobber for VT_LLOCAL (`tccgen.c::gv`)

tcc's PPC32 BE LL split-load path:
```
incr_offset(+4); load LOW; incr_offset(-4); load HIGH;
```

For VT_LLOCAL lvalues (typical for register-pointer field accesses
that get spilled by an earlier `save_reg_upstack`), the first
`load()` clobbers the address register: `lwz r3, 0(r3)`. The
subsequent `incr_offset(-4)` then operates on the **loaded value**
rather than the address, producing a garbage HIGH-half address.

Concrete failure:
```c
i64 sqlite3PcacheRefCount(PCache *p) { return p->nRefSum; }
```
compiled to:
```
lwz r3, 24(r31)         ; r3 = arg
addi r3, r3, 12          ; r3 = arg + 12
stw r3, -12(r31); lwz r3, -12(r31)   ; spill+reload (save_reg)
addi r3, r3, 4           ; r3 = arg + 16 (LOW addr on BE)
lwz r3, 0(r3)            ; r3 = LOW value
addi r3, r3, -4          ; r3 = LOW_VALUE - 4   ← WRONG (was supposed to be addr-4)
lwz r4, 0(r3)            ; SEGV
```

Fix: in the LL split-load `else if (vtop->r & VT_LVAL)` branch,
snapshot vtop's `r/c.i/r2` before any modification. After the LOW
load + vdup, if the original vtop was VT_LLOCAL, re-anchor vtop to
the original VT_LLOCAL state so the HIGH load reloads the pointer
fresh from the saved local.

Note: the broader fix (also handling true register-pointer
lvalues, not just VT_LLOCAL) breaks bootstrap. The minimal
VT_LLOCAL-only fix is what shipped. Register-pointer LL loads
remain potentially buggy in narrow paths — bug 4 below caught
many of those incidentally.

Commit: `bc1ff4d`.

### 4. LL-arg shuffle clobber in `gfunc_call` (`ppc-gen.c`)

Surfaced by zlib (`./example` SEGV inside `gz_open`'s
`LSEEK(state->fd, 0LL, SEEK_CUR)`) and sqlite (`./sqlite3 :memory:
"select 1+1"` SEGV inside `sqlite3RunParser`). Both call helpers
with mixed `{int address pointer, long long value, int}` args.

Before this fix, tcc emitted:
```
lwz  r4, -12(r31)        ; r4 = state
addi r4, r4, 20          ; r4 = state + 20  (address of state->fd)
li   r5, 0               ; LL HIGH = 0
li   r3, 0               ; LL LOW  = 0
mr   r4, r5              ; r4 := 0   ← CLOBBERS state+20 !
mr   r5, r3              ; r5 := 0
lwz  r3, 0(r4)           ; reload state->fd via r4 — but r4 is 0  → SEGV
```

The second pass of `gfunc_call` processes args from vtop downward.
arg2 (a long long) materializes its halves into temporary regs,
then `mr` shuffles them into the ABI slots r4/r5. The shuffle
clobbers r4 — which arg1's later setup expected to still hold the
address of `state->fd`.

Fix: in the LL move block of `gfunc_call`, call
`save_reg(target_hi - 3)` and `save_reg(target_lo - 3)` BEFORE
materializing the LL value. That spills any vstack entry using
those regs to a stack local; subsequent arg1 setup reloads from
the local instead of the clobbered register.

This was the **biggest single win of the session** — it unblocked
both sqlite and zlib in one fix.

Commit: `bc50586`.

## What's verified at HEAD

* `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` → fixpoint holds.
* `./scripts/run-tests2.sh` → 111/111.
* `./demos/v0.2.12-lua.sh` → lua 5.4.7 builds + runs.
* `./scripts/bench.sh` → tcc 2s vs gcc-O0 17s vs gcc-Os 41s on lua.
* zlib 1.3.1 in `/tmp/zlib-1.3.1`: `./example` full test suite
  passes; `./minigzip` round-trips correctly.
* sqlite3 amalgamation in `/tmp/sqlite-amalgamation-3460000`:
  `./sqlite3 -version` works, `./sqlite3 :memory: "select 1+1"`
  returns 2.

## Open work (in roughly decreasing impact)

### Sqlite remaining (next session priority)

1. **`sqlite :memory: "create table t(x)"` returns SQLITE_CORRUPT
   (11)** — pinpointed via instrumentation to
   `sqlite3.c:100154`:
   ```c
   if( rc==SQLITE_OK && initData.nInitRow==0 ){
       /* OP_ParseSchema with non-NULL P4 should parse ≥1 row */
       rc = SQLITE_CORRUPT_BKPT;
   }
   ```
   The schema reload after CREATE TABLE finds 0 rows in
   `sqlite_schema`. Either the CREATE didn't insert (B-tree write
   broken) or the SELECT iteration is broken (B-tree read or VDBE
   cursor broken). VDBE-level bug.

2. **`sqlite3_open("/tmp/foo.db", ...)` SEGV** — wild jump via bad
   function pointer. r12 (often the ctr-load source for indirect
   calls) holds junk. Likely a struct-with-function-pointers
   layout issue or the same kind of LL-arg bug with a different
   pattern.

Both probably involve a single underlying codegen bug. See
[sqlite-debug.md](sqlite-debug.md) for triangulation paths.

### Roadmap structural items still open

* **#3 — Mach-O `tcc -ar` driver.** tcc's built-in `-ar` only
  knows ELF. Need to parse Mach-O nlist + write
  `__.SYMDEF SORTED` BSD-style with #1/N long names. ~150-300
  lines. Localized.
* **#4 — Mach-O archive alacarte loader.** Currently
  force-whole-archive; would also fix the long-standing
  `libgcc.a` whole-archive crash from session 025. Substantial.
* **#7 — Self-link diagnostics.** Continued from session 040
  (REL24, no-PIC-anchor, sect overflow already done). Future:
  un-resolved-relocation site reporting in the EXE writer.

### Larger scope

* **bcheck.c port.** Multi-session lift if we want `-b`
  instrumented builds to actually instrument.
* **DWARF debug info emission.** `tccdbg.c` has no PPC support.
  Would unblock lldb / gdb debugging of tcc-built programs.
* **AltiVec intrinsics.** None today.

### Real-world program build status

* **Lua 5.4.7.** ✅ Works.
* **zlib 1.3.1.** ✅ Works (full test suite passes).
* **sqlite3 amalgamation.** ⚠️ In-memory SELECT works;
  CREATE TABLE returns SQLITE_CORRUPT; file open SEGVs.

## How to pick up

### Quick sanity check
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
git status                  # should match origin/main
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh     # ~5 min, expect 111/111
./scripts/bench.sh          # ~2 min, expect tcc<gcc-O0<gcc-Os
./demos/v0.2.12-lua.sh      # ~30 sec, expect "fib(20) = 6765"
```

### Sqlite repro (already on ibookg37 at `/tmp/sqlite_clean.c`)
```sh
ssh ibookg37
cd /tmp/sqlite-amalgamation-3460000
~/tcc-darwin8-ppc/tcc/tcc -B ~/tcc-darwin8-ppc/tcc \
    -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
    -o /tmp/sqlite3 shell.c sqlite3.c -lm
/tmp/sqlite3 :memory: "select 1+1"          # works → 2
/tmp/sqlite3 :memory: "create table t(x)"    # CORRUPT
/tmp/sqlite3 /tmp/foo.db ".tables"           # SEGV
```

### Zlib (already on ibookg37 at `/tmp/zlib-1.3.1`)
```sh
ssh ibookg37
cd /tmp/zlib-1.3.1
./example                                    # full test passes
echo hello | ./minigzip > /tmp/x.gz && ./minigzip -d < /tmp/x.gz
```

## Codegen quirks burned in (informational)

(carrying forward from session 040 + session 041 additions)

1. **`put_nlist` narrow-int args** — bypassed via inlined byte
   writes in `ppc-macho.c::put_nlist`.
2. **Warning emission via `fprintf+fflush`** — bypassed via
   direct `write(2)` syscall in tcc.c-side warning paths.
3. **64-bit const-fold sign-ext** — partial workaround in
   `tccgen.c::gv`. Deeper fix is the open question from session 031.
4. **char/short param big-endian offset** — `+3 / +2` adjustment
   in `ppc-gen.c::gfunc_prolog`.
5. **LL-helper return r3↔r4 swap** — every LL-returning function
   gets the swap (not just FP-to-LL helpers; that earlier
   confusion was wrong).
6. **VLA × callee param-spill safety buffer** — 128 bytes below
   each VLA in `ppc-gen.c::gen_vla_alloc`.
7. **`@hi` vs `@ha` in `lis+ori` vs `lis+addi`** — `ori` is
   zero-extending so paired with `@hi`; `addi` sign-extends so
   paired with `@ha`. See session 037 findings.
8. **N_WEAK_REF in n_desc low byte** for STB_WEAK undef externals.
9. **Variadic FP arg shadow spill** when `gslot >= 8`.
10. **Struct lvalue with addr in register**: `ppc-gen.c::load`
    treats VT_STRUCT as a register move (`mr`), not a memory load.
11. **Cross-TU SECTDIFF input relocs**: the Mach-O reader
    translates scattered HA16/LO16 SECTDIFF pairs to
    R_PPC_HA16_PIC / LO16_PIC.
12. **BE Sym-union sym_scope vs enum_val**: `sym_link` skips the
    sym_scope write for IS_ENUM_VAL syms.
13. **BE bitfields force byte-wise**: every bitfield on PPC32
    uses byte-wise load/store rather than wide-container shift+
    mask, because the static initializer is byte-wise (NEW THIS
    SESSION).
14. **Apple PPC ABI long-long 4-byte alignment**: `double` and
    `long long` are 4-byte aligned in structs on Apple PPC32, NOT
    8-byte (NEW THIS SESSION).
15. **LL field load via VT_LLOCAL re-anchors after LOW load**:
    snapshot vtop before LL split-load, restore for HIGH load
    (NEW THIS SESSION).
16. **LL-arg shuffle in gfunc_call spills target slots first**:
    save_reg target_hi/lo before LL `mr` moves, so other args'
    address pointers in those slots don't get clobbered (NEW THIS
    SESSION).

See [docs/notes/ppc-codegen-pitfalls.md](../../notes/ppc-codegen-pitfalls.md)
for a postmortem aimed at parallel PPC backends (e.g. the
golang/PPC fork). Items 13-16 here may all be relevant there.

## Hosts (unchanged)

* **ibookg37** — primary build / test host (iBook G3, 900 MHz
  PowerPC 750, Tiger 10.4.11). `git` not in default `$PATH`;
  rsync from uranium.
* **imacg3** — secondary; less-used. Same configuration.
* **uranium** — main Mac (this one). Edit + commit + cut releases
  here.

## Closing notes

This session shipped three releases (v0.2.14, v0.2.15, v0.2.16),
fixed four real codegen bugs (BE bitfield read-back, Apple PPC
ABI alignment, LL field-load clobber, LL-arg shuffle clobber),
and unblocked two real-world programs (zlib full test suite,
sqlite in-memory SELECT). tests2 climbed from 110/111 (99.1%) to
**111/111 (100.0%)** — first time at 100%.

The remaining sqlite issues (CREATE TABLE CORRUPT, file open
SEGV) are the next-best targets — concrete reproductions exist,
the corrupt path is narrowed to a specific source line, and given
the pattern of bugs found this session, there's a good chance
they're 1-2 more long-long or struct-layout bugs in the VDBE /
B-tree write paths.

Good luck.
