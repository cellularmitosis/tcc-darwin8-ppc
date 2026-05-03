# Sqlite3 debugging notes — session 041

## Crash 1: in `sqlite3_open` → `sqlite3PagerSetPagesize` → `sqlite3PcacheRefCount`

### Repro

```c
#include "sqlite3.h"
int main() { sqlite3 *db; sqlite3_open(":memory:", &db); }
```
Compiled and run with tcc-darwin8-ppc, segfaults inside open.

### Triangulation

Instrumented `openDatabase` with byte-wise `write(2)` markers, then
walked into `sqlite3BtreeOpen` → `sqlite3PagerOpen` →
`sqlite3PagerSetPagesize`. The crash is in the `if` condition at
`tcc/sqlite3.c:60866`:
```c
if( (pPager->memDb==0 || pPager->dbSize==0)
 && sqlite3PcacheRefCount(pPager->pPCache)==0
 && pageSize && pageSize!=(u32)pPager->pageSize ){
```
specifically inside `sqlite3PcacheRefCount`:
```c
SQLITE_PRIVATE i64 sqlite3PcacheRefCount(PCache *pCache){
  return pCache->nRefSum;
}
```

### Two distinct bugs found

#### Bug A — Apple PPC ABI long-long alignment (`tccgen.c::type_size`)

PCache layout:
```c
struct PCache {
    PgHdr *pDirty, *pDirtyTail;     /* 0..7 */
    PgHdr *pSynced;                  /* 8..11 */
    i64 nRefSum;                     /* tcc:16, gcc:12 */
    int szCache;                     /* tcc:24, gcc:20 */
    ...
};
```

Apple PPC32 ABI uses **4-byte** alignment for `double` and `long
long` inside structs. tcc was using **8-byte** (the SysV/Linux PPC
convention).

Fix: in `type_size`, gate `*a = 4` on
`(defined TCC_TARGET_PPC && !defined TCC_TARGET_PPC64)` in addition
to the existing i386 / ARM-non-EABI cases.

After the fix, tcc-built PCache matches gcc-built PCache exactly.

#### Bug B — LL field load address-clobber for VT_LLOCAL (`tccgen.c::gv`)

tcc's PPC LL load path is:
```c
incr_offset(+PTR_SIZE);   // address += 4 (LOW at offset+4 on BE)
load(r, vtop);            // load LOW
vdup(); vtop[-1].r = r;
incr_offset(-PTR_SIZE);   // address -= 4 (back to HIGH offset)
                          // ... caller then calls load(r2, vtop) ...
```

For a register-pointer lvalue (or VT_LLOCAL after save_reg_upstack
converted it from one), `load(r, vtop)` emits `lwz r, 0(r)` —
clobbering the address register with the loaded value. The
subsequent `incr_offset(-PTR_SIZE)` then operates on the loaded
*value*, not the address, producing a garbage HIGH-half address.

Concrete failure: `i64 sqlite3PcacheRefCount(PCache *p) { return
p->nRefSum; }` compiled to:
```
lwz  r3, 24(r31)         ; r3 = arg
li   r4, 12; add r3, r3, r4   ; r3 = arg + 12 (nRefSum addr)
stw  r3, -12(r31); lwz r3, -12(r31)   ; spill+reload (save_reg)
li   r4, 4;  add r3, r3, r4   ; r3 = arg + 16 (LOW addr on BE)
lwz  r3, 0(r3)              ; r3 = LOW value
li   r4, -4; add r3, r3, r4   ; r3 = LOW_VALUE - 4  ← WRONG
lwz  r4, 0(r3)               ; SEGV: deref ~0xfff... — CRASH
```

Fix: in the LL split-load `else if (vtop->r & VT_LVAL)` branch,
snapshot the original `r/c.i/r2` before any modification. After the
LOW load + vdup, if the original vtop was `VT_LLOCAL`, re-anchor
vtop to the original VT_LLOCAL state so the HIGH load reloads the
pointer fresh from the saved local.

After the fix, the same function compiles to:
```
lwz  r3, 24(r31)             ; r3 = arg
li   r4, 12; add r3, r3, r4  ; r3 = arg + 12
stw  r3, -12(r31); lwz r3, -12(r31)
li   r4, 4;  add r3, r3, r4  ; r3 = arg + 16
lwz  r3, 0(r3)               ; r3 = LOW value
lwz  r4, -12(r31)            ; r4 = arg + 12 (re-derived)
lwz  r4, 0(r4)               ; r4 = HIGH value
or   r0,r3,r3;  or r3,r4,r4;  or r4,r0,r0  ; LL return swap
```

### Verification

* `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` — fixpoint holds.
* `./scripts/run-tests2.sh` — 111/111.
* `./demos/v0.2.12-lua.sh` — lua 5.4.7 builds + runs.
* `./sqlite3 :memory: "select 1+1"` — now reaches `sqlite3_prepare_v2`
  before crashing (was crashing in `sqlite3_open` before).

## Crash 2 (deferred): `sqlite3_prepare_v2`

After v0.2.15, `select 1+1` still crashes — but now inside
`sqlite3RunParser` → `sqlite3Parser` → `yy_reduce` (LEMON-
generated rule reducer).

### Triangulation

* `sqlite3_open` works.
* `sqlite3_prepare_v2(db, "create table t(x)", ...)` returns
  rc=26 (error) but doesn't crash.
* `sqlite3_prepare_v2(db, "pragma user_version", ...)` returns
  rc=0; crash happens during `sqlite3_step`.
* `sqlite3_prepare_v2(db, "select 1", ...)` crashes during
  prepare — specifically inside `yy_reduce`.

So the bug is reached only by SELECT-style statements that need
expression-tree construction during parsing.

### Crash root cause [FIXED in v0.2.16-g3]

The crash was tcc's `gfunc_call` LL-arg shuffle clobbering another
arg's address register. Specifically, in calls like
`func(state->fd, 0LL, SEEK_CUR)`:

1. arg1 (state->fd lvalue) was computed: `r4 = state + 20`.
2. arg2 (LL value) was materialized via `gv(RC_INT)`, then `mr`
   shuffled into ABI slots r4 and r5.
3. The `mr r4, r5` step CLOBBERED r4 — but arg1 still needed r4!
4. arg1's later `lwz r3, 0(r4)` loaded from r4=0 instead of
   state+20 → SEGV.

Fix: in the LL move block of `gfunc_call`, call save_reg on
target_hi and target_lo BEFORE materializing the LL. That spills
any vstack entry using those regs to a local; arg1 reloads from
the local later.

After fix:
- `./sqlite3 :memory: "select 1+1"` → 2
- `./sqlite3 :memory: "select hex(1234)"` → 31323334
- zlib `./example` full test suite passes

## Crash 3 (deferred): file-based sqlite_open and CREATE TABLE

After v0.2.16, simple SELECT queries work but:

- `sqlite3_open("/tmp/test.db", ...)` SEGV in xOpen dispatch
  (PC = junk, looks like wild jump via corrupted function pointer).
- `sqlite3 :memory: "create table t(x); ..."` returns
  SQLITE_CORRUPT (11, "database disk image is malformed") — not a
  crash, but B-tree state is wrong.

Both probably involve the same underlying bug: a struct field at
the wrong offset or a function pointer table populated with junk.
sqlite uses `sqlite3_vfs` (lots of function pointers) and
`sqlite3_io_methods` (more function pointers). Static initializers
should populate them correctly, but tcc may have a layout bug for
one of them.

Next session investigation paths:
1. Compare tcc-built and gcc-built `aVfs[]` static initializer
   bytes — diff to find layout discrepancy.
2. Walk the unixVfs struct in gdb to verify each function pointer
   matches the symbol address.
3. Look for STRUCT initializers with mixed types (int + pointer +
   long long) where alignment might still differ.
