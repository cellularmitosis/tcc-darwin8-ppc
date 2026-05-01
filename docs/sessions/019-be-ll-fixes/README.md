# 019 — Big-endian long-long fixes

The session 018 README closed with: *"019 — fix the BE long-long
handling in tccgen.c."* This session does that. tcc-self now compiles
real programs end-to-end: arithmetic, recursion, function calls,
arrays, printf — all produce correct PPC code on the G3.

## What was broken

tcc was written assuming little-endian. On big-endian PPC, several
code paths read or wrote the two halves of a 64-bit value at the
wrong memory offset:

| Path | LE assumption | BE reality |
|---|---|---|
| `lexpand` for VT_LVAL | `vtop[0].c.i += 4` puts HIGH at top | adds to wrong half — vtop[-1] needs the +4 |
| `gv()` 64-bit lvalue load | first load (offset+0) → LOW reg | first load is HIGH on BE |
| LL store in `gen_assign` | first `store` writes LOW at offset | LOW belongs at offset+4 on BE |
| `save_reg_upstack` LL spill | LOW first, HIGH at offset+PTR_SIZE | order must be reversed |
| `gen_cast` LL→INT shortcut | reuse same address as int | reads HIGH 32 on BE, not LOW |

And, separately discovered along the way:

| Path | Bug |
|---|---|
| PPC `TOK_UMULL` | two consecutive `get_reg(RC_INT)` calls returned the same register because the first wasn't parked in vstack — both halves of the 64-bit product overwrote each other |
| Apple PPC ABI for LL function returns (r3=HIGH, r4=LOW) | tcc convention requires sv->r=LOW; `PUT_R_RET` set sv->r=r3 → ABI/convention mismatch |
| LL arg-passing in `gfunc_call` | the two `mr`s into `(r3,r4)` form a swap when source registers are also in `(r3,r4)`; naive sequential moves clobber a source |
| `gen_opl` helper-function returns (`__udivdi3` etc.) | declared as `func_old_type` (returns INT), so `gfunc_call`'s post-call swap doesn't fire — the manual `vtop->r = REG_IRET` sequence at the call site needs the swap baked in |

## What was changed

### `tcc/tccgen.c` (+67 lines)

- **`lexpand` VT_LVAL case** — on PPC, `vtop[-1].c.i += 4` instead of
  `vtop[0].c.i += 4` so the LOW half (at offset+4 on BE) ends up at
  the bottom of the produced pair, matching the convention every
  caller assumes (`vtop[-1] = LOW, vtop[0] = HIGH`).

- **`gv()` LL VT_LVAL load** — on PPC, `incr_offset(PTR_SIZE)` first,
  load LOW from offset+4, then `incr_offset(-PTR_SIZE)` and load HIGH
  from offset+0. Result: `vtop->r = LOW, vtop->r2 = HIGH` matching the
  established convention.

- **LL assignment store in `vstore`** — on PPC, store HIGH (`vtop->r2`)
  at offset+0 first, then LOW (`vtop->r`) at offset+PTR_SIZE.

- **`save_reg_upstack`** — on PPC, swap the order of the two stores
  (HIGH first at offset, LOW second at offset+PTR_SIZE). Without this,
  spills written by `save_reg` couldn't be read back by the
  endian-corrected `gv` LL load.

- **`ALLOW_SUBTYPE_ACCESS = 0` on PPC** — the gen_cast shortcut for
  narrowing-from-memory ("a cast to a smaller type can just change
  the type tag") is endian-dependent. On BE the LOW 32 bits of an
  in-memory LL live at `addr+4`, not `addr+0`. Disabled the shortcut
  on PPC; cast LL→INT now goes through `lexpand+vpop` which honors
  the BE offset.

- **`gen_opl` swapped `reg_iret` / `reg_lret` on PPC** — libgcc
  helpers (`__udivdi3` et al.) follow Apple PPC ABI and return r3=HIGH,
  r4=LOW. tcc's `PUT_R_RET` machinery doesn't see the LL return type
  for these calls (because `vpush_helper_func` uses `func_old_type`
  with VT_INT return), so the manual `vtop->r = reg_iret` setup in
  `gen_opl` would record the SValue with the wrong reg-to-half mapping.
  Swapping the two locals at definition time fixes every
  helper-mediated LL operation.

### `tcc/ppc-gen.c` (+107 lines)

- **`TOK_UMULL` register parking** — set `vtop[-1].r2 = hi_slot`
  *between* the two `get_reg(RC_INT)` calls so the second call sees
  the first's reservation in vstack and picks a different register.

- **`gfunc_call` post-bl swap for LL returns** — when the called
  function's declared return type is VT_LLONG, emit
  `mr r0,r3 ; mr r3,r4 ; mr r4,r0` after the `bl`/`bctrl`. Apple ABI
  delivered (r3=HIGH, r4=LOW); the swap presents (r3=LOW, r4=HIGH)
  which matches tcc's convention for `PUT_R_RET(LL)`.

- **`gfunc_epilog` pre-blr swap for LL returns** — symmetric: when
  the current function's return type is VT_LLONG, swap r3↔r4 just
  before `blr`. tcc's gen_return path leaves r3=LOW, r4=HIGH; the
  swap presents (r3=HIGH, r4=LOW) per Apple ABI to the caller.

- **`gfunc_call` LL arg `mr` reorder** — the two `mr` instructions
  that move the materialized LL halves into target ABI slots can
  conflict (e.g. moving (r3,r4) → (r3,r4) reversed = a swap). New
  three-case logic:
  1. True swap (`lo_reg == target_hi && hi_reg == target_lo`): use
     a 3-instruction swap via r0.
  2. `hi_reg == target_lo`: emit HIGH first.
  3. otherwise: emit LOW first.
  Without this, a `__udivdi3(a, b)` call with both args spilled would
  silently corrupt b's HIGH half.

## Test record

| Test | Result |
|---|---|
| `tcc-self -v` | exits 0, prints version ✓ |
| `int main(){return N;}` for N ∈ {0, 1, 5, 10, 42, 100, 0xff, 0xffff, -1, -42} via tcc-self compile + gcc link | all match `N & 0xff` ✓ |
| `int sq(int n){return n*n;}` + main calling `sq(7)` | exit 49 ✓ |
| `fact(5)` recursive | exit 120 ✓ |
| `printf("hello from tcc-self %d\n", 42)` | prints, exit 0 ✓ |
| `int a[3]={10,20,30}` with element reads | reads 10, 20, 30 (was reading a[0] for all indices) ✓ |
| Constant fold `1+1`, `2*3`, `5*5`, `10-3` | correct ✓ |
| LL function return `u64 ident(u64 x){return x;}` | preserves all 64 bits ✓ |
| LL run-time division `100ULL/7ULL` | exit 14 ✓ |
| All previous demos (s005..s016) | still pass ✓ |

## What's still broken

1. **Constant-folded division** still gives 0 for cases like `100/7`
   (compile-time fold via `gen_opic_sdiv`). The runtime path is fine;
   the bug is in `gfunc_call`'s register allocation when an LL helper
   call has both LL args formed by complex expressions. Materializing
   the second arg's spill reload uses a scratch register (e.g. r5)
   which happens to be an ABI slot already populated for the first
   arg's HIGH half. tcc's vstack tracking releases the previous arg's
   reg as "free" after `vtop--`, so the allocator picks it as a temp.
   Affects at minimum `gen_opic_sdiv(100, 7)` and `(a>>63 ? -a : a)`
   patterns.

2. **`tcc-self -run <fn-call.c>`** still aborts (SIGILL = exit 132)
   for any program that does a function call. Compile + gcc-link works
   fine; only the JIT path is broken. `return 5` via `-run` works.

3. **Self-host loop** (`tcc-self compiles tcc.c → tcc-self2`) blocked
   on `__FLT_EVAL_METHOD__` not being defined as a built-in macro.
   `math.h` errors out in the `#if __FLT_EVAL_METHOD__ == 0` ladder.
   Workaround: predefine via `-D__FLT_EVAL_METHOD__=0`.

## Files changed

- `tcc/tccgen.c`: lexpand, gv() LL load, LL assignment store,
  save_reg_upstack LL spill, ALLOW_SUBTYPE_ACCESS guard, gen_opl
  reg_iret/reg_lret swap.
- `tcc/ppc-gen.c`: TOK_UMULL register parking, gfunc_call/epilog
  LL ABI r3↔r4 swaps, LL arg-passing `mr` reorder.

## Decisions

- **Endian-aware paths are gated on `TCC_TARGET_PPC` rather than a
  generic `BIG_ENDIAN` check.** PPC is currently the only big-endian
  target this fork supports; if/when more land we'll generalize.

- **Three-instruction r3↔r4 swap** at every LL call/return is cheap
  (12 bytes) and keeps tcc's internal convention unchanged. The
  alternative — overriding `R_RET`/`R2_RET` per type — required
  matching changes in many places (`RC_RET`, `gen_return`, etc.) and
  was attempted then reverted.

- **`save_reg` swap rather than `store()` swap**. Our `store(VT_LLONG)`
  in ppc-gen.c is already endian-correct, but `save_reg` calls
  `store()` once per half with type=LL and r2=VT_CONST, which trips
  the "only low half" path. Easier to fix at the save_reg level than
  to make the half-store path endian-aware.

## Next session

020 — fix the gfunc_call ABI-slot tracking so spilling/reloading args
during a multi-arg call doesn't clobber already-placed slots. This is
the last known blocker for full self-host of the `gen_opic_sdiv`
pattern and possibly others.
