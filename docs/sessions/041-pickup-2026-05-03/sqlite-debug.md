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

### Crash assembly

```
li r7, 0
mr r5, r7         ; r5 = 0 (was previously some pointer)
lwz r4, 0(r5)     ; lwz r4, 0(0) — loads from absolute address 0; SEGV
```

This is a deliberate `*(NULL)` deref by tcc-emitted code. tcc
intentionally set `r5 = 0` then dereffed through it.

### Heisenbug clue

Adding a single `write(2, marker, n)` call **at the top of
`yy_reduce`, before the `switch` dispatch**, makes the crash
disappear. Adding the same marker inside `case 0`'s body (which
shifts cases 1+ but not the switch dispatch) does NOT fix it. So
the buggy code is in the function setup or switch dispatch, not
in any individual case body.

Could be:
* A stack-frame-size sensitivity: my LL field-load fix uses
  `save_reg_upstack` which may allocate a temp local that lands at
  a problematic offset (e.g., just past `0x7fff` 16-bit signed
  range, falling through to a buggy code path).
* PIC anchor / @ha-@lo offset interaction: yy_reduce's PIC anchor
  computation may end up at an address where some downstream
  `lis+addi` pair encodes wrong.
* Switch dispatch codegen (jump table or compare-chain).

### Next steps

1. Bisect the switch by inserting markers at increasingly later
   case bodies until the bug reappears — find which case body is
   the buggy one.
2. Or: dump tcc's emitted code for yy_reduce (with and without
   marker) and diff to find the divergence.
3. Or: build with `-O0`-equivalent volatility annotations on
   yy_reduce locals (forcing all-spill) and see if bug persists.
