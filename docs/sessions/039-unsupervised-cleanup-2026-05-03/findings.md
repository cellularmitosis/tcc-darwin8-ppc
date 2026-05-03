# Session 039 findings — durable lessons

## Mach-O `N_WEAK_REF` is a low-byte n_desc bit, NOT a binding type

For weak undefined externals (`__attribute__((weak)) void f();`),
the linker output must set `N_WEAK_REF = 0x40` in n_desc's low
byte. Without it, dyld treats the UNDEF as hard and aborts at load
time when the symbol can't be resolved — even though the user code
expects the address to come back as 0 and tests `!!&f`.

The high byte of n_desc carries the two-level namespace library
ordinal (1 = first LC_LOAD_DYLIB). The low byte carries flags
including `N_WEAK_REF`. Tiger header: `<mach-o/nlist.h>`.

Detection on our side: `ELFW(ST_BIND)(esym->st_info) == STB_WEAK`
on the underlying ELF symbol entry.

## `cpp` macro args don't survive commas in PPC asm

Tried writing `atomic-ppc.S` using a cpp macro:
```
#define ATOMIC_FETCH_OP_4(name, op_insn) ...
ATOMIC_FETCH_OP_4(add, add r6, r0, r4)
```

Errors with "macro passed 4 arguments, but takes just 2" — cpp
splits on every comma in the call site. The PPC three-operand
instruction syntax (`add r6, r0, r4`) is inherently comma-rich.

Workarounds:
* Don't use cpp macros; write each function out (what I did).
* Wrap multi-comma args in parens — but parens become part of
  the substituted text, breaking the asm.
* Use C++-style variadic macros (`__VA_ARGS__`) — works in modern
  cpp but is ugly for repeated boilerplate.

For a small, fixed set of ops (~6 here), explicit duplication is
fine and readable.

## Per-file Makefile override pattern for the libtcc1.a build

To route a single source file through a different compiler (e.g.
`atomic-ppc.S` through gcc-4.0 instead of the generic
`$(XCC) = $(TCC)` rule):

```make
ifneq "$(filter ppc%,$T)" ""
$(X)atomic-ppc.o : atomic-ppc.S
	$S$(CC) -c $< -o $@
endif
```

The make ruleset's most-specific match wins, so this overrides
the generic `$(X)%.o : %.S` rule for `atomic-ppc.S` only.

`$(CC) = gcc-4.0` is set in `tcc/config.mak` from the configure
probe. `$(X)` is the cross-prefix (empty for native PPC builds).

## Variadic FP arg passing on Apple PPC has BOTH FPR and GPR-shadow paths

When calling a variadic function with FP args, the Apple PPC ABI
requires the FP value to live in TWO places:

1. The FP register `f1..f8` (so non-variadic callees can read it
   from there).
2. The corresponding GPR shadow slot — `r3..r10` if `gslot < 8`,
   or the outgoing parameter stack at `r1 + 24 + gslot*4` if
   `gslot >= 8` (so variadic callees walking `va_arg` by GPR
   slot see the right bytes).

Forgetting (2) when `gslot >= 8` produces "garbage in printf"
bugs that look like they should be parser or precision issues.
73_arm64's "0.0 0.0" output and 70_floating_point_literals'
"5-ULP error" were both this: printf reads from the GPR shadow
when fpr_used >= 8 OR when the va_arg tracking points there;
if the shadow wasn't filled, you get whatever was on the stack.

Specific cases for a `double` arg, where `gslot` is the starting
GPR-shadow slot (0-based):

* `gslot < 7`: both halves fit in r3..r10. Use `stfd; lwz; lwz`.
* `gslot == 7`: high half in r10, low half spills to stack at
  `r1 + 24 + 8*4 = r1+56`. **Don't forget the stack write.**
* `gslot >= 8`: both halves on stack at `r1 + 24 + gslot*4` /
  `r1 + 24 + (gslot+1)*4`. `stfd fS, (24+gslot*4)(r1)` writes
  both halves with one instruction.

For float args, only one slot needed. `stfs fS, (24+gslot*4)(r1)`
when `gslot >= 8`.

## Enum-in-parameter-list scope extension (NOT YET FIXED)

C standard quirk: when a function is *defined* (not just declared)
with an enum declared inside its parameter list, the enum
constants are visible inside the function body. This is the
"parameter scope extends into function body" rule from the
standard.

Tcc currently *does not* extend parameter scope into the body for
enum constants. So this code:

```c
int bar(enum ee { a = 12, b = 34 } i) {
    return a + b;       /* should be 12 + 34 = 46 */
}
```

…compiles to `return 0 + 0` (or 1+1, in our case — both `a` and
`b` resolve to `1`, suggesting they're being parsed as default-
ordinal-1 enum constants somewhere). Hits 60_errors_and_warnings
test_scope_1.

Workaround: declare the enum at file scope or inside the function
body. Both work.

The fix is in tccgen.c around the parameter parsing — needs to
keep enum constants from parameter scope alive through the body
parse. Likely involves not popping the parameter scope when
transitioning from declarator to body.

## Apple PPC nlist's high byte carries the LIBRARY ORDINAL

Two-level namespace lookup: each undef external has `n_desc >> 8`
as the index of the LC_LOAD_DYLIB it should resolve from
(1-based; 0 = "any library", which generally fails). Our PPC
backend hardcodes 1 because we only ever load libSystem (well,
plus libpthread.dylib which is a shim that forwards back to
libSystem on Tiger).

If we ever want to load multiple dylibs, this needs to track per-
symbol which dylib provides it.
