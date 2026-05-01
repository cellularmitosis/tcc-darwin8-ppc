# 010 — Long long codegen

64-bit integer (`long long`) arithmetic, parameter passing, return,
and stack storage. Apple PPC ABI: `long long` lives in two consecutive
GPRs (r3:r4 for the first arg, etc.); high half in the lower-numbered
register, low half in the higher.

## What was added (in `ppc-gen.c`)

- **`gen_opi(TOK_UMULL)`** — unsigned 32×32 → 64 multiply. Allocates
  TWO fresh result registers (low + high) so we don't clobber the
  source operands. tcc's `gen_opl` for long-long-mul calls UMULL on
  duplicates of the operands and expects the originals to survive.
- **`gen_opi(TOK_ADDC1 / TOK_ADDC2)`** — `addc` (sets carry) and
  `adde` (uses carry). Used by `gen_opl('+')` for the low+high halves.
- **`gen_opi(TOK_SUBC1 / TOK_SUBC2)`** — `subfc` and `subfe`.
- **`store(VT_LLONG, VT_LOCAL)`** — emit two `stw` instructions
  (high at `offset(r31)`, low at `offset+4(r31)`).
- **`load(VT_LLONG, VT_LOCAL)`** — tcc loads each half via separate
  `gv()` calls with adjusted offsets, so a single `lwz` per call
  suffices.
- **`gfunc_call`** — first pass: assign starting GPR slot per source
  arg, accounting for `long long` consuming two slots. Second pass:
  `gv(RC_R(slot))` materializes each arg into the target register;
  tcc's `rc2` mechanism auto-allocates the +1 slot for the high half.
- **`gfunc_prolog`** — for each `long long` param, allocate 8-byte
  local slot and spill both incoming GPRs.

## Bug fixed: UMULL was clobbering operand registers

First cut wrote the low result into `ra_gpr` (the L1 register).
tcc's gen_opl, however, calls UMULL on duplicate vstack entries
that share the same register slots as the originals. Clobbering
those registers breaks the subsequent `gen_op('*')` calls that
compute the cross-half products. Fix: allocate fresh registers for
both halves of UMULL's result and update `vtop[-1].r` to point at
the new low slot.

## Test record

```
long long literal cast to int       → works
long long + / - / *  (with locals)  → works
long long arg passing (1, 2 args)   → works
long long return                    → works
long long arithmetic in a loop      → works (factorial, sum)
fib(20) returning long long         → fails (separate r2-not-set issue)
long long divide / modulo           → fails (needs __divdi3 from PLT)
```

## Demo

[`demos/s010-long-long.c`](../../../demos/s010-long-long.c) — passes two
64-bit values to a helper, sums them, returns the low byte.
0x12345678 + 0x10000000 = 0x22345678 → exit 120.

## What's deferred

- `long long` divide / modulo: tcc emits calls to `__divdi3` /
  `__moddi3` (libgcc helpers). These require a working PLT for
  external symbols. Will land with the next chunk of dynamic linking
  work.
- The `r2 not set` failure mode in some fib-like patterns. Need to
  trace where tcc calls `store` with VT_LLONG but doesn't materialize
  the high-half register before the call.

## Next session

011 — struct passing per Apple PPC ABI.
