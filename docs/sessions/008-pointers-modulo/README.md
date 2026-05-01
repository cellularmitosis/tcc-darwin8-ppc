# 008 — Pointers, modulo, arrays-as-pointers

Three small additions that together unlock a much bigger surface of
real C code.

## What was added

### `load()` and `store()` — pointer dereference

When `(sv->r & VT_VALMASK) < VT_CONST` and `VT_LVAL` is set, the
value is "lvalue at the address held in this register" — a pointer
dereference.

- `load()`: emit `lwz/lhz/lha/lbz rD, 0(base_gpr)` based on
  `VT_BTYPE`, with `extsb` for signed byte.
- `store()`: emit `stw/sth/stb rS, 0(base_gpr)`.

**Subtle convention point:** for `(reg | VT_LVAL)`, the effective
address is `reg + 0`, not `reg + sv->c.i`. The `c.i` is residual
from the pre-`gv` state (e.g. when the value came from `*(local at
offset c.i)`); after `gv()` loads the value into the register, the
register holds the full address and `c.i` is stale.

We initially got this wrong and emitted `lwz r3, -8(r3)` for `*p`
where p was a local at -8(r31). The dereferenced load read from
`(p+(-8))` instead of `p`. Verified by checking arm-gen.c
(`fc=sign=0;` line 621) and i386-gen.c's `gen_modrm` register
branch (which doesn't include `c` in the encoding when the base is a
register). Both confirm offset zero is the right convention.

### `gen_opi()` — modulo

PPC has no native modulo instruction. Compute as `a - (a/b)*b`
using a freshly-allocated temp register:

```
divw[u]  tmp, rA, rB    ; tmp = a / b
mullw    tmp, tmp, rB   ; tmp = (a/b) * b
subf     rA, tmp, rA    ; rA = a - tmp
```

`get_reg(RC_INT)` picks a free register that isn't holding a
vstack value, so the temp doesn't collide with `rA` or `rB`.

## Test record

```
*p (read)              → returns the loaded int
*p = 99                → mutates the pointed-to local
swap(int*, int*)       → mutates both, demonstrates 4 round-trips
sum_array(int*, n)     → walks an array via pointer arithmetic
modulo (signed/unsigned) → 100 % 30 = 10, 17 % 5 = 2
```

The biggest test is `sum_array` from the demo: `int a[10];` declared,
filled with `a[i] = i+1`, passed to `sum_array(a, 10)`. tcc decays
the array to `int*`, the callee walks it, reads each element via
pointer dereference, accumulates. Result = 55, which is correct.

## Decisions

- **`(reg | VT_LVAL)` ignores `sv->c.i`.** Matches every other
  backend; this is the tcc convention. If we ever need register-base
  + compile-time displacement (e.g. struct field access where the
  base is in a register), tcc emits the displacement separately as
  pointer arithmetic and `c.i` stays at 0.
- **Modulo uses a temp register.** An alternative is to clobber the
  second operand (`rB`) — fine for v1 since `gen_opi` consumes it
  anyway — but the tmp approach keeps the code symmetric with
  the other two-operand integer ops. Performance equivalent.

## Demo

[`demos/s008-array-sum.c`](../../../demos/s008-array-sum.c) — fills
`int a[10]` with 1..10, sums them via a pointer-passing helper,
returns the sum mod 256. Exit 55 on imacg3.

## Exit state

Pointer arithmetic, dereference, arrays, and the modulo operator
all work. The combined surface is now roughly: any straight-line C
that doesn't need floats, structs, varargs, or `printf`. Big chunks
of real code compile correctly.

## Next session

Roughly:

1. **Floats** — `gen_opf`, FP load/store, FP arg passing per Apple ABI.
2. **Varargs** — `gen_va_start` / `gen_va_arg` for the Apple PPC
   convention (regsave area + arg pointer).
3. **`tccmacho.c` PPC support** — the big unblocker for real `.o`
   output, dylib loading, calling into libSystem (printf!), and
   bootstrap.
