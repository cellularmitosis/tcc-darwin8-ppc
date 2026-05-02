# 030 — sqlite3 smoke test

## Result

🟡 **Sqlite3 amalgamation (3.46.0, 257K lines) does not yet
compile** with our PPC tcc. Fails on first encounter of a function
declaring more than 8 register-passed params:

```
$ tcc -B./tcc -I./tcc -I/tmp -DSQLITE_OMIT_LOAD_EXTENSION=1 \
      -DSQLITE_THREADSAFE=0 -c /tmp/sqlite3.c -o /tmp/sqlite3.o
/tmp/sqlite3.c:69176: error: ppc-gen: parameters exceed 8 GPR slots
```

The function is `sqlite3WalCheckpoint(...)` with 13 params. PPC
ABI passes args 1-8 in r3-r10 and 9+ on the caller's stack frame
at fixed offsets (24+8*4=56 onwards from caller SP). Our backend
currently asserts on params past 8 in `gfunc_prolog`
(`tcc/ppc-gen.c:1720`).

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

## What's blocking sqlite

**`gfunc_prolog` doesn't handle parameters that spill to the
caller's stack frame.** Required ABI-side work:

1. For incoming params that don't fit in r3-r10, set their offset
   to `frame_size + 24 + (param_index * 4)` (relative to the
   callee's SP after `stwu`). This requires us to know
   `frame_size` at prolog time, which we currently compute later.
2. For VT_LLONG params that straddle the r10/stack boundary
   (param 8 is a long long with low half in r10 and high half on
   stack), we'd need a small spill/copy sequence in the prolog.
3. The corresponding **caller side** in `gfunc_call` already
   handles >8 args (it pushes overflow args onto the new frame
   before the call) — but worth re-verifying it matches what
   gfunc_prolog expects.

Estimated 50-100 lines in `ppc-gen.c`. Not done in 030 because
v0.2.0 already shipped and this is post-release scope; would land
in a v0.2.1 or v0.3.0.

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
| `docs/sessions/030-sqlite-smoke/README.md` | this file |
