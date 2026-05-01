# 012 — Varargs (stdarg.h, va_start / va_arg / va_end)

Tcc's own source uses varargs in `tcc_warning`, `tcc_error`,
`cstr_printf`, and helpers throughout. This session enables that.

## Restructure: param spills moved to the prologue

Previous design (sessions 005-010): incoming args were spilled to
NEGATIVE offsets from FP, in source order, going downward. Worked
for fixed args but broke `&last_fixed_arg + sizeof(last_fixed_arg)`
because that walks UP into the FP-save slot.

New design: prologue UNCONDITIONALLY spills all 8 arg-passing GPRs
(r3..r10) to the **caller's parameter save area** at `r31+24..+52`.
Body code addresses each fixed param at a POSITIVE offset from FP:
param `i` lives at `r31 + 24 + i*4`. Variadic args land at the
slots above the last named param.

Cost: 8 extra `stw` instructions in every function prologue (32
bytes), even for no-arg and non-variadic functions. Constant
PROLOG_SIZE = 52 bytes. Worth it: keeps the prolog size constant
(no need for "is this variadic" flow analysis) and lets the standard
char*-based va_list machinery in `tcc/include/tccdefs.h` (i386
fallback branch) work without a backend hook:

```c
typedef char *__builtin_va_list;
#define __builtin_va_start(ap, last) \
    (ap = ((char *)&(last)) + ((sizeof(last) + 3) & ~3))
#define __builtin_va_arg(ap, t) \
    (*(t *)((ap += (sizeof(t) + 3) & ~3) - ((sizeof(t) + 3) & ~3)))
```

`&last` is `r31 + (24 + last_index*4)`. Adding `sizeof(last)` (4 for
int) gives the next slot — the first variadic arg.

## Frame layout (final)

```
high addr (caller's frame)
| caller's parameter save area (32 bytes for r3..r10):       |
|   +52(r31) spilled r10                                     |
|   +48      spilled r9                                      |
|   ...                                                       |
|   +24      spilled r3   <-- first incoming arg             |
| caller's linkage:                                           |
|   +8(r31)  saved LR (we wrote it in prolog)                |
+----+ <-- OLD_SP, our r31 (FP)
| r31 save (4 bytes)         <-- at r31-4
| user locals (loc=-8 down)  <-- addressed via offset(r31)
|    ...
| outgoing parameter area (32 bytes, r1+24..55)
| linkage area (24 bytes,    r1+0..23)
+----+ <-- NEW_SP, our r1
```

Two addressing regions sharing the same FP register: positive
offsets reach incoming params, negative offsets reach our locals.

## Test record

```
sum_n(5, 1,2,3,4,5)             →  15
sum_n(7, 1,2,3,4,5,6,7)         →  28  (saturates r3..r10)
sum_n(3, 10,20,12)              →  42
add_lots returning long long    →  220 (low byte of 1500)
```

Plus all prior sessions' tests still pass:
- return constants, locals, arithmetic, control flow → still working
- function calls (recursive `fib(10)=55`) → still working
- long long add through helper                    → still working
- struct member access via pointer                → still working
- Mach-O .o output + gcc-link + run on G3         → still working

## Demo

[`demos/s012-varargs.c`](../../../demos/s012-varargs.c) — `sum_n(7,
1..7) + 14 = 42`. Verified on imacg3.

## Why no `gen_va_start` / `gen_va_arg` hook

`tccgen.c` only dispatches to `gen_va_start` / `gen_va_arg` for
arm64 / riscv64 / x86_64 — those targets have struct-shaped va_lists
that need backend help. For PPC we fall through to the i386-style
plain `char *` va_list, which tcc handles generically once `&last`
gives a usable address. Our prolog change makes `&last` give the
right address.

## Exit state

Bootstrap is now structurally possible: tcc's varargs-using
functions (tcc_error, tcc_warning, cstr_printf, ...) can compile
correctly. The remaining blockers for self-host:

- Floating point (a few literal-parsing helpers in tccpp.c).
- Long long divide / modulo (rare; tcc avoids these in hot paths).
- PLT for external symbols (calling libgcc helpers like `memcpy`).
- The `r2 not set` long long store edge case.

## Next session

013 — floats. Apple PPC ABI: f1..f13 for arg slots, f1 for return,
double = 64-bit, long double = 128-bit double-double (Apple-only).
For v1 we'll do single + double, defer double-double.
