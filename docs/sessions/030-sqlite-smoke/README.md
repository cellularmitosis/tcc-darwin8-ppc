# 030 — sqlite3 smoke test

## Result

🟡 **Sqlite3 amalgamation (3.46.0, 257K lines) does not yet
compile** with our PPC tcc — but two of the blockers got fixed in
this session before we hit the third (struct-by-value):

| Blocker | Status |
|---|---|
| Header conflict on `ssize_t`/`intptr_t` in `<stddef.h>` | ✅ fixed (guarded with `#ifndef _SSIZE_T` etc.) |
| `ppc-gen: parameters exceed 8 GPR slots` (sqlite3WalCheckpoint, 13 args) | ✅ fixed (just remove the artificial cap; offset math already handles it) |
| `ppc-gen: gen_opi op 0x85 not yet supported` (TOK_PDIV) | ✅ fixed (alias to `divw` instruction) |
| `ppc-gen: struct parameters not yet supported` (line 122853) | ❌ blocking |

Exit state: sqlite3.c now reaches line ~122853 (deep in the SQL
engine) before stopping on the struct-by-value gap. That gap is
~200-500 lines of work in `ppc-gen.c::gfunc_call` and
`gfunc_prolog`; deferred to a future session.

## What was fixed along the way

While trying to compile sqlite3.c, hit and fixed an unrelated
header-conflict bug:

* `tcc/include/stddef.h` defines `ssize_t`/`ptrdiff_t`/`intptr_t`/
  `uintptr_t`/`size_t` as plain typedefs. Tiger's `<sys/types.h>`
  redefines `ssize_t` (gated by `#ifndef _SSIZE_T`). Result: any
  source that includes both `<stddef.h>` and `<sys/types.h>` gets
  an `incompatible redefinition of 'ssize_t'` error. Affects
  sqlite3.c at line 14682 and several of the failing tests in
  029's tests2 baseline (the atomic-counter ones).

  Fix: gate each typedef in `stddef.h` with the matching
  conventional `#ifndef _SSIZE_T` etc. so subsequent includes
  short-circuit.

This was a small win — should bump a handful of tests2 cases
into the pass column on the next baseline run.

## What was actually wrong with `gfunc_prolog`

Embarrassingly little! Looking at the existing code:

```c
param_offset = 24 + gpr_index * 4;
gfunc_set_param(sym, param_offset, 0);
```

The offset formula was already correct for both arg ranges:
- Args 1-8 (gpr_index 0..7): live at r31+24..52 where the prolog
  spilled r3-r10 to caller's param save area.
- Args 9+ (gpr_index 8+): live at r31+56+ where the caller pushed
  them in `gfunc_call`'s overflow path (already implemented).

The only thing stopping us was an artificial `if (gpr_index +
slots > 8) tcc_error(...)` check. Removing it (and tightening
the FPR check, which is still real — only 8 FPRs available)
made >8-arg functions Just Work.

Verified: `int sum10(int,int,int,int,int,int,int,int,int,int)`
compiles, links, runs, returns the correct sum.

## Other things sqlite would likely hit after >8-arg functions

Quick scan of sqlite3.c for things our backend currently chokes on:

| Pattern | Approx hits |
|---|---|
| `struct foo bar(...)` (struct return) | dozens |
| `int foo(struct bar baz, ...)` (struct param) | dozens |
| `__builtin_expect`, `__builtin_unreachable` etc. | ~50 |
| VLAs | rare but present in some tests |
| `_Atomic` / `<stdatomic.h>` | only if built with threads |

So even with the >8-arg fix, sqlite would hit several more
backend gaps before compiling cleanly. Each is documented in the
029 tests2 baseline.

## What's next

Sqlite needs sustained backend work before it's reachable. The
sequence I'd suggest:

1. `>8-arg function prolog/epilog` (this session's blocker).
2. `struct-by-value pass/return` (also the biggest tests2
   failure category).
3. `__builtin_expect` and friends — most are just "ignore the
   hint and treat as the underlying expression".
4. VLAs.

After all that, retry `tcc -c sqlite3.c`.

## Files touched

| | |
|---|---|
| `tcc/include/stddef.h` | guard typedefs with conventional `_X_T` macros so they don't conflict with system headers |
| `tcc/ppc-gen.c` | drop the `> 8 GPR slots` cap in gfunc_prolog (the underlying offset math was already right); add TOK_PDIV (alias to signed `divw`) |
| `docs/sessions/030-sqlite-smoke/README.md` | this file |
