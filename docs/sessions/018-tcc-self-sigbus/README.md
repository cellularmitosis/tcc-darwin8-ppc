# 018 — tcc-self -v SIGBUS fix

`tcc-self -v` now exits 0 with the version string instead of
SIGBUS-ing in `error1` -> `pp_error` -> `strcpy(NULL)`. The original
crash chain was correct -- `pp_error` was getting called with no
live token because the control flow into `error1` was miscompiled.
But the root cause was deeper: a PIC base register (r30) was
left uninitialized because the per-function PIC base setup was
emitted **lazily** at the first PIC use site. When the first PIC use
sat inside one branch of an `if`/`else` (e.g. `tcc_compile`'s
`if (fd == -1) ... else { tcc_open_bf(...); file->fd = fd; }`), the
setup ran only on the path that hit it first; the other path then
crashed when it tried to use r30.

## Root cause

`ppc_emit_pic_base_setup()` (the `mflr r0 ; bcl ; mflr r30 ; mtlr r0`
sequence) was emitted by `ppc_emit_pic_addr_load()` only on the
first call within a function:
```c
if (!ppc_func_pic_base_emitted)
    ppc_emit_pic_base_setup();
```
This is wrong: that "first call" can be inside a conditional branch.
The else-branch then sees `r30 = <whatever>` (typically the caller's
PIC base, e.g. `main`'s) and reads from the wrong slot.

In `tcc_compile`, the wrong-slot read returned 0 (or a junk
non-zero pointer) for `file`; `*(file + 8) = fd` then either
crashed or silently corrupted unrelated memory, eventually leading
to the `pp_error`-with-no-token crash visible in the SIGBUS.

## Fix

`tcc/ppc-gen.c` `gfunc_prolog`: emit `ppc_emit_pic_base_setup()`
unconditionally at function entry when `output_type == TCC_OUTPUT_OBJ`.
This guarantees r30 is valid for every PIC access regardless of
control flow. Costs 16 bytes per function (4 instructions); for
non -c outputs (`-run`, full link) PIC isn't needed and the setup
is skipped.

Removed the lazy fallback from `ppc_emit_pic_addr_load`; left the
`ppc_func_pic_base_emitted` flag in place but it's now set by
`gfunc_prolog` directly.

## Other bugs fixed along the way

While chasing the SIGBUS several other latent codegen bugs surfaced.
They're all pre-existing -- gcc-built tcc had them too -- and only
matter once tcc-self has to compile its own source. Each is fixed
with a minimal targeted change.

### 1. Initialized data section was written little-endian on PPC

`tccgen.c:init_putv()` writes constants into the section data with
`write32le` / `write16le` / `write64le`. Those write byte-by-byte
in LE order. PPC is big-endian and reads via `lwz`, so:
```c
int g_init = 42;            // bytes written: 2a 00 00 00
return g_init;              // lwz reads: 0x2a000000 = 704_643_072
```
Every initialized integer global (including the `tcc_options[]`
table, the FlagDef arrays, etc.) read as garbage at runtime.

Fix: PPC-specific `q[0] = (val >> 24) & 0xff; ...` MSB-first stores
for `VT_SHORT`, `VT_LLONG` (32-bit-build branch only; VT_PTR/VT_INT
also wrapped). Pattern matches the existing PPC-specific paths for
`VT_FLOAT` / `VT_DOUBLE`.

### 2. Struct member offset (`extra_off`) ignored in load/store via global

The non-PIC global access path in `tcc/ppc-gen.c` `load()` /
`store()` emitted:
```
lis  rT, ha16(sym)
lwz  rD, lo16(sym)(rT)        ; <-- extra_off (e.g. 8 for g.c) IGNORED
```
The relocator overwrites the lo16 field with `lo16(sym)`, so any
addend stashed there is wiped. Result: `g.c` always read `g.a`'s
value.

Fix: when `extra_off != 0`, materialize the full symbol address
into `addr_gpr` first via `addi addr_gpr, addr_gpr, lo16(sym)`,
then use `extra_off` as the load/store displacement. One extra
instruction per non-zero-offset global member access; `extra_off=0`
keeps the original two-instruction `lis + lwz` path.

Same fix applied to the address-of path (`!want_load`) for
`&g.field`-style expressions.

### 3. Long-long argument register passing only set the LOW half

`tcc/ppc-gen.c:gfunc_call` for `bt == VT_LLONG`:
```c
gv(RC_R(gslot));   // materializes ONE register
```
With our `RC2_TYPE` returning 0 for non-`RC_IRET` cases, `gv` only
allocates one register for LL values; the HIGH half ends up in
whatever the allocator picks (often `r3`, the lowest free slot),
which then gets overwritten by the next arg's `gv`.

Fix: for an LL arg targeting register pair `(gslot, gslot+1)`:
```c
gv(RC_INT);                      // materialize both halves
lo_reg = TREG_TO_GPR(vtop->r);
hi_reg = TREG_TO_GPR(vtop->r2);
mr r(3+gslot),     hi_reg;       // HIGH into low-numbered reg (Apple ABI)
mr r(3+gslot+1),   lo_reg;       // LOW into high-numbered reg
```

## What works after this session

| Test | Result |
|---|---|
| `int main(){return 42;}` via `-run` | exit 42 ✓ |
| `fib(10)` via `-run` | exit 55 ✓ |
| `printf("hi\n")` compile + gcc-link + run | works ✓ |
| `int g = 0x12345678 ; printf("%x", g)` | prints 12345678 ✓ |
| Initialized struct array iteration | works ✓ |
| Struct member access via global | works ✓ |
| Struct member access via pointer arg | works ✓ |
| All 11 tcc sources compile via tcc | 11/11 OK ✓ |
| `tcc-self` link (no `-read_only_relocs`) | succeeds ✓ |
| **`tcc-self -v`** | **exits 0 with version string ✓** |

## What's still blocking full self-host

1. **`tcc-self -B. -run /tmp/r42.c`** fails with
   `tcc_get_symbol(_main) not defined`. The .o produced by tcc-self
   has `_main` as `t` (STB_LOCAL) instead of `T` (STB_GLOBAL). And
   `return 42` compiles to `li r3, 0x52` (= 82) not `li r3, 0x2a`
   (= 42). Both are symptoms of a deeper BE long-long handling bug
   in `tccgen.c`: many code paths (lexpand for VT_LVAL, gen_opl
   shift-by-32, gen_cast LL→INT, struct-copy of 8-byte unions, etc.)
   read or write the two halves of a 64-bit value with a swapped
   offset on big-endian targets. Tcc was written assuming little
   endian and the high half is always at offset +4. PPC needs the
   opposite.

2. The deeper bug manifests in tcc-self specifically because:
   - tcc-self was compiled by gcc-built ./tcc, which has the bug
     baked into the codegen it emits.
   - tcc-self uses LL arithmetic in many places (parse_number's
     `n = n*b + t`, CValue.i = uint64_t, etc.).
   - Wrong LL handling propagates into wrong literal values, wrong
     symbol bindings, etc.

3. An attempted fix to tccgen.c's `lexpand` to swap the offset
   on PPC was reverted because it broke gen_opl's shift logic
   (which compensates for the LE convention internally). A proper
   fix needs coordinated changes across lexpand, gen_opl, gen_cast,
   and possibly more. Out of scope for this session.

4. An attempted fix to ppc-gen.c's pointer-deref `load`/`store` to
   honor `sv->c.i` as a struct member offset was reverted because
   it broke `tcc-self -v` -- in practice tccgen always emits an
   `addi` to fold the offset into the base register before calling
   load via reg, so c.i is always 0 there. Adding it caused
   double-application.

## Other notes

- Pre-existing limitation from session 017 still applies:
  `__floatundidf` is gcc-compiled into a stub object alongside the
  link. Should be added to `tcc/lib/` for proper self-host.

## Files changed

- `tcc/ppc-gen.c`:
  - `gfunc_prolog`: unconditional PIC base setup for OBJ output
  - `gfunc_call`: LL arg passing via gv+mr to enforce Apple ABI
  - `load()` / `store()` global path: handle extra_off via addi
- `tcc/tccgen.c`:
  - `init_putv`: PPC-specific big-endian stores for VT_SHORT,
    VT_INT/VT_PTR (32-bit build), VT_LLONG (32-bit build)

## Next session

019 — fix the BE long-long handling in tccgen.c. lexpand should
present `vtop[0] = HIGH, vtop[-1] = LOW` regardless of endianness,
and gen_opl / gen_cast should be audited to use that convention
consistently. Once fixed, retest tcc-self -B. -run /tmp/r42.c and
the full self-host loop.
