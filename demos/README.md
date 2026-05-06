# Demos

One runnable demo per milestone, showcasing the new capability. Each
is a tiny C program that compiles and runs end-to-end on a real G3
running Tiger 10.4.11 via `./tcc -B. -run demos/<file>.c`.

The naming scheme is `sNNN-<slug>.c` for sessions before the first
versioned release. Once we ship `v0.1.0-g3` (the first /opt-installable
tarball), demos for new capabilities use `v<X>.<Y>.<Z>-<slug>.c`.

| Demo | Milestone | Expected exit | What it proves |
|---|---|---|---|
| [s005-return-the-answer.c](s005-return-the-answer.c) | [005 — `tcc -run` on G3](../docs/sessions/005-macho-stubs/README.md) | `42` | First end-to-end JIT execution of tcc-generated PPC code. Function prologue/epilogue, immediate load, blr return. |
| [s006-factorial.c](s006-factorial.c) | [006 — locals + arith + control](../docs/sessions/006-locals-arith-control/README.md) | `120` | Locals, integer arithmetic, while-loop, conditional branch — the first program with real computation. |
| [s007-fibonacci.c](s007-fibonacci.c) | [007 — function calls (Apple PPC ABI)](../docs/sessions/007-function-calls/README.md) | `55` | Recursive `fib(10)`. Caller-side arg packing in r3-r10, callee-side parameter spill, `bl` with R_PPC_REL24 relocation patched at JIT time, frame preservation across nested recursion. |
| [s008-array-sum.c](s008-array-sum.c) | [008 — pointers + modulo](../docs/sessions/008-pointers-modulo/README.md) | `55` | Sums an `int[10]` filled with 1..10 via a pointer-passing helper, returns `sum % 256`. Pointer deref, store-via-pointer, array-to-pointer decay, modulo. |
| [s009-real-macho.sh](s009-real-macho.sh) | [009 — real PPC Mach-O .o](../docs/sessions/009-tccmacho-ppc/SESSION_README.md) | `42` | tcc emits a real PPC Mach-O `.o`, gcc-4.0 links it, the executable runs on G3. First on-disk linkable output. |
| [s010-long-long.c](s010-long-long.c) | [010 — long long codegen](../docs/sessions/010-long-long/README.md) | `120` | 64-bit add through a function: `add64(0x12345678LL, 0x10000000LL)` low byte. Long long params (r3:r4 + r5:r6), returns, addc/adde. |
| [s011-struct.c](s011-struct.c) | [011 — structs](../docs/sessions/011-structs/README.md) | `42` | Two struct types, member access (`.` and `->`), pass-by-pointer to helpers, struct holding pointer-to-array. Confirms structs work for the patterns tcc itself uses. |
| [s012-varargs.c](s012-varargs.c) | [012 — varargs](../docs/sessions/012-varargs/README.md) | `42` | `sum_n(7, 1..7) + 14 = 42`. Standard `<stdarg.h>`. Prolog now spills all 8 GPR arg slots to caller's parameter save area so the standard char*-based va_list machinery walks forward correctly. |
| [s013-floating-point.c](s013-floating-point.c) | [013 — IEEE 754 single + double FP](../docs/sessions/013-floating-point/README.md) | `27` | `poly(a, b, c, x) = a*x*x + b*x + c` evaluated as `poly(1, 2, 3, 4)`. FP load/store/arithmetic, FP arg passing (f1..f8), int↔FP conversions, FP comparisons. |
| [s016-hello-printf.sh](s016-hello-printf.sh) | [016 — PPC PIC stubs](../docs/sessions/016-ppc-plt-stubs/README.md) | prints `hello from tcc-built program` | First-ever `printf` from tcc-emitted code on G3. PowerPC `__picsymbolstub1` + `__la_symbol_ptr` + indirect symbol table for late-binding external function calls. |
| [s025-self-link.sh](s025-self-link.sh) | [025 — Mach-O .o reader](../docs/sessions/025-macho-o-reader/README.md) | prints `hello from tcc-built and tcc-linked program` | Full self-link via tcc, no gcc. `tcc -o exe file.c` auto-loads `/usr/lib/crt1.o` and produces a working printf+malloc+strcpy executable end-to-end. |
| [v0.2.12-lua.sh](v0.2.12-lua.sh) | [040 — sqlite3/lua pickup](../docs/sessions/040-pickup-2026-05-03/README.md) | prints `fib(20) = 6765` then `done` | First non-trivial third-party program: lua 5.4.7 (~30 .c files, full standard library) builds and runs end-to-end with tcc. Surfaced two backend bugs (struct-deref-by-value; cross-TU PIC reloc translation) that v0.2.12 fixes. |
| [v0.2.17-alloca.sh](v0.2.17-alloca.sh) | [043 — unsupervised](../docs/sessions/043-unsupervised-2026-05-05/README.md) | prints `concatenated: jack/jill/jane` then `done` | `alloca()` actually works. Pre-v0.2.17, allocating with alloca and then doing anything else (printf, strcpy) silently corrupted the alloca'd memory and SEGV'd on epilog. Two coupled bugs: function epilog used `addi r1, r1, frame_size` (broken when alloca moves r1) and alloca's safety zone was 32 bytes (too small to absorb subsequent param spills). New epilog uses back-chain + FP restore; new alloca reserves 256 bytes. |

## How to run any demo

On imacg3 (or any Tiger PPC G3+):

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
./scripts/build-tiger.sh        # if you haven't built yet
cd tcc
./tcc -B. -run ../demos/sNNN-<slug>.c
echo $?                          # check the exit code matches the table
```

The `-B.` points tcc at the current directory for `libtcc1.a` (an
empty archive at present, but tcc still wants to find it). After we
ship the first /opt-installable tarball, `tcc demo.c -o demo && ./demo`
will work without `-B.`.
