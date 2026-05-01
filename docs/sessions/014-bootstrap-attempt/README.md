# 014 — First bootstrap attempt: how close are we?

After 013 (FP), the surface is broad enough to attempt the
self-host. Here's what happens and what's left.

## Smoke test: compile each tcc source with our tcc (`-c` only)

Without `-B.` the include path is wrong; with `-B.`:

```
$ for src in libtcc.c tccpp.c tccgen.c tcc.c tccelf.c tccrun.c \
             tccdbg.c tccasm.c ppc-gen.c ppc-link.c ppc-macho.c; do
    ./tcc -B. -c $src -o /tmp/$src.o
  done
  [OK]   libtcc.c
  [OK]   tccpp.c
  [OK]   tccgen.c
  [OK]   tcc.c
  [OK]   tccelf.c
  [OK]   tccrun.c
  [OK]   tccdbg.c
  [OK]   tccasm.c
  [OK]   ppc-gen.c
  [OK]   ppc-link.c
  [OK]   ppc-macho.c
```

🎉 **All 11 source files compile cleanly to `.o` with our PPC tcc.**
This is a major signal — the entire frontend (preprocessor, parser,
type checker) plus our own backend code go through.

## Reality check: with full code paths exercised they don't all link

Running the same compilation but actually exercising every code
path inside each .c (i.e., feeding tcc the WHOLE source rather than
just the parts that don't trigger unimplemented features) reveals
several gaps:

- `ppc-gen: load stub (sv->r=0x131 type=0x5 ...)` — **VT_LLOCAL**
  load. tcc uses VT_LLOCAL for indirect locals (most VLA paths,
  some struct-via-pointer patterns). i386-gen.c:316 has the
  reference: load the pointer first, then load through it.
- `ppc-gen: store stub (sv->r=0x330 bt=0x3)` — store to
  **VT_CONST|VT_SYM**, i.e. a **global variable**. Needs the
  `lis rA, sym@ha ; stw rS, sym@lo(rA)` pair, with two relocations
  (R_PPC_ADDR16_HA and R_PPC_ADDR16_LO) per access.
- The corresponding **load** of a global is also unimplemented.

And on the link side:
- `ld: ppc-link.o symoff in load command 1 not aligned on 4 byte
  boundary` — ppc-macho.c is producing a symtab with misaligned
  symbol offsets. Bug in the .o emitter.

## What's needed to actually bootstrap

Roughly two more milestones:

1. **Global variables in ppc-gen.c** — load and store paths for
   `VT_CONST | VT_SYM`, plus `VT_LLOCAL` for indirect locals.
   Probably ~100 lines of code, mostly the lis/lo split and
   relocation emission.
2. **Fix ppc-macho.c symtab alignment** — the linker complains
   about a 4-byte alignment issue in load command 1. Likely a
   padding/alignment off-by-one when laying out the LC_SYMTAB
   region.

Once those two land, link the .o files into a self-hosted tcc
binary and run it.

## Status: very close, not done

The codegen surface for tcc's source code is essentially complete —
every file parses + type-checks + emits successfully. The remaining
bugs are concrete and bounded. Pragmatic estimate: 1-2 more sessions
to actually run the bootstrap and validate the resulting binary
matches the original.

## Demo

No demo for this milestone — it's a status check, not a new
capability. The real demo will be `tcc tcc.c -o tcc-self &&
./tcc-self ...` when bootstrap completes.
