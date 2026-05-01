# 007 — Function calls (Apple PPC ABI)

Direct + recursive function calls work end-to-end on the G3.

## Arrival state

After 006, we could compile straight-line C with locals, arithmetic,
and control flow inside one function. A second function definition
caused tcc to abort at the `gfunc_call` / `gfunc_prolog` stubs.

## What was added

### Caller side — `gfunc_call`

For each arg `i` (0..nb_args-1), `gv(RC_R(i))` materializes it into
the corresponding GPR (r3..r10). After args are in place:

- **Direct call** (`vtop->r` is `VT_CONST | VT_SYM`): emit `bl 0`
  (`0x48000001`) and record an `R_PPC_REL24` relocation against
  `vtop->sym`. The relocation handler patches the displacement at
  JIT/link time.
- **Indirect call** (function pointer in a register): `mtctr rS ;
  bctrl` (CTR-based call so we don't burn LR on the indirect step).

Bails out (clearly) on the unsupported cases for v1: FP args,
struct args, long-long args, more than 8 args.

### Callee side — `gfunc_prolog` parameter unpacking

Walks `func_type->ref->next` (tcc's parameter list). For each int
param `i` (0..7):

1. `loc -= 4` to allocate a fresh local slot.
2. Emit `stw r(3+i), loc(r31)` — spill the incoming register arg.
3. `gfunc_set_param(sym, loc, 0)` — tell tcc "param `sym` lives at
   offset `loc` from FP."

The spill instructions are emitted right after the reserved 20-byte
prologue placeholder. At execution time the prologue runs first
(setting up `r31`), then the spill instructions fire and our params
end up in known stack slots — addressable just like any other local.

### Frame layout updated

`ppc_frame_size()` now always reserves 32 bytes for the outgoing
parameter area above the linkage area:

```
NEW_SP+0..23    linkage area
NEW_SP+24..55   outgoing parameter area (8 GPR slots)
NEW_SP+56..     gap, then locals growing upward
r31 + loc       user/spilled-param local at offset `loc` from FP
r31 - 4         saved r31 (FP)
r31             OLD_SP
```

Minimum frame size is now 64 bytes. Reserving the parameter area
unconditionally trades 32 bytes per frame for not needing flow
analysis to detect "this function makes calls."

### `ppc-link.c` — real relocation handling

Replaced the abort-on-everything `relocate()` with implementations
for:

- `R_PPC_NONE` — no-op.
- `R_PPC_ADDR32` — write 32-bit absolute (data references, GOT
  entries when we get there).
- `R_PPC_REL24` — patch the LI field (bits 6-29, mask `0x03fffffc`)
  of `b` / `bl` with the signed byte displacement to the symbol.
  Range-checked to ±32MB.
- `R_PPC_ADDR16_HA` / `_HI` / `_LO` — split-immediate forms used
  by `lis` + `addi` / `ori` sequences. Not exercised by v1 codegen
  yet but cheap to add and we'll need them.

## Test record

```
sq(7)        →  49     (single arg, multiply)
sum(20, 22)  →  42     (two args, add)
max(7, 42)   →  42     (two args, branch on comparison)
factorial(5) →  120    (recursive)
fib(10)      →  55     (deeply recursive — exercises the call mechanism hard)
eight(1..8)  →  36     (saturates the GPR arg slots)
```

All correct. Recursive Fibonacci is the strongest signal: 177 nested
calls without corrupting frames, args, or LR.

## Bug encountered (and resolved): stale binary in tests

First test run after the change reported all wrong values. Adding
fprintf debug showed the codegen was correct; the wrong values came
from a stale `./tcc` binary that hadn't been rebuilt because the
parent `make` invocation timed out before linking. Force-rebuild
(`touch ppc-gen.c && make`) gave correct results.

Lesson for future sessions: **after any ppc-gen.c edit, force a
rebuild**. Tiger's parent `make` doesn't always notice when a
download or rsync touches files mid-run.

## Decisions

- **8-arg limit** (in-register only) for v1. Stack args above 8 are
  straightforward to add but require separate codegen for the stack
  slot stores. Will land when we hit a real program that needs >8
  args.
- **Always reserve outgoing parameter area** (32 bytes). Simpler
  than tracking "does this function make calls."
- **`bl 0` placeholder + R_PPC_REL24 relocation**, not direct
  encoding. Means symbol resolution is fully owned by the relocator,
  same path as future ELF/Mach-O writes.
- **`mtctr ; bctrl` for indirect calls**, not `mtlr ; blrl`. CTR
  is the conventional indirect-call target; LR stays clean.

## What's still stubbed

- `gen_va_start` / `gen_va_arg` — varargs.
- FP args / FP return values.
- Struct args (by hidden pointer or in regs per Apple ABI).
- Long long args.
- More than 8 args.
- Calls into dynamic libraries (printf et al) — needs `tccmacho.c`
  PPC support.

## Exit state

- `tcc -B. -run` correctly compiles and runs programs with multiple
  user-defined functions, including deep recursion.
- The R_PPC_REL24 relocation handler is real; future calls (like
  branches that overflow 14-bit BD) will hit it cleanly.
- Frame layout is settled — locals, params, linkage area, outgoing
  parameter area all in their right slots.

## Demo

[`demos/s007-fibonacci.c`](../../../demos/s007-fibonacci.c) — recursive
`fib(10) = 55`. Verified on imacg3.

## Next session

Session 008+ candidates, in rough cost-vs-unblock order:

1. **`gen_opi('%')`** — modulo. PPC has no native instruction; compute
   `a - (a/b)*b` with a temp register. ~30 minutes.
2. **`gen_opf` and FP args** — floating point codegen. Apple PPC ABI
   is interesting because FP and int slots interleave for varargs.
3. **`gen_va_start` / `gen_va_arg`** — needed for `printf`. Apple PPC
   uses a regsave area at the bottom of the callee's frame plus a
   pointer.
4. **`tccmacho.c` PPC support** — the big one. Without it: no real
   `.o` output, no calling into libSystem, no bootstrap. Several
   sessions of careful work.
