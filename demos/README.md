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
