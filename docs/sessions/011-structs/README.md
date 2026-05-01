# 011 — Structs (member access + pass-by-pointer)

No new code in `ppc-gen.c` or `ppc-link.c` for this milestone — the
struct usage that matters for tcc self-host already worked, courtesy
of the locals + pointer-deref machinery built in 008. This session is
verification + demo + scope confirmation.

## What works

- Struct local declaration: `struct point p;`
- Member assignment: `p.x = 10;`
- Member read: `return p.x + p.y;`
- Pointer-to-struct: `struct point *p = &foo;`
- Member access via pointer: `p->x`, `p->y`
- Structs containing pointers: `struct ds { int *values; int n; };`
  with the helper walking `ds->values[i]` correctly.
- Mixed-size members (char + char + int) with the natural struct
  padding tcc inserts.
- Multi-field (3+) structs in any combination.

## What's deferred

- **Struct return by value** — Apple PPC ABI: caller allocates
  result space, passes pointer in r3 as hidden first arg, callee
  writes through it, caller reads back. Not yet implemented in
  `gfunc_call` / `gfunc_prolog`. Triggers a `create_plt_entry`
  error in current code (tcc emits a memcpy call to copy the
  result).
- **Struct pass by value** — Apple has unique rules for passing
  small composites in registers. Deferred.
- **Anything that triggers tcc's `memcpy` intrinsic** for whole-
  struct copies (e.g. `struct A x = y;` where the structs are
  large). Same blocker: needs PLT or a tcc-internal memcpy
  fallback.

These are acceptable gaps for tcc self-host — tcc's own source
nearly always uses structs by pointer, never returns them by value.

## Demo

[`demos/s011-struct.c`](../../../demos/s011-struct.c) — uses two
struct types (`point` and `dataset`), passes them by pointer to
helpers, accesses members both with `.` and `->`, walks a pointer
member as an array. Returns `42`.

## Next session

012 — varargs (`gen_va_start` / `gen_va_arg`) per the Apple PPC
convention. This + dylib loading is the path to `printf`.
