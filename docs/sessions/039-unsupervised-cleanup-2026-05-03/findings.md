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

## Apple PPC nlist's high byte carries the LIBRARY ORDINAL

Two-level namespace lookup: each undef external has `n_desc >> 8`
as the index of the LC_LOAD_DYLIB it should resolve from
(1-based; 0 = "any library", which generally fails). Our PPC
backend hardcodes 1 because we only ever load libSystem (well,
plus libpthread.dylib which is a shim that forwards back to
libSystem on Tiger).

If we ever want to load multiple dylibs, this needs to track per-
symbol which dylib provides it.
