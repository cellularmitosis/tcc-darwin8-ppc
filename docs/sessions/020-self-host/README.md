# 020 — Self-host fixpoint reached 🎉

**`tcc-self` compiles `tcc.c` into `tcc-self2`, which compiles
`tcc.c` into a `.o` byte-identical to its own `.o`. Self-host
fixpoint achieved on the G3.**

```
$ /tmp/tcc-self -B./tcc -I./tcc -D__FLT_EVAL_METHOD__=0 \
                -c tcc.c -o /tmp/tcc-self2-build/tcc.o
[ link with gcc-4.0 + libgcc + floatundidf stub → /tmp/tcc-self2 ]
$ /tmp/tcc-self2 -v
tcc version 0.9.28rc (PowerPC Darwin)
$ /tmp/tcc-self2 -B./tcc -I./tcc -D__FLT_EVAL_METHOD__=0 \
                 -c tcc.c -o /tmp/tcc-self3.o
$ cmp /tmp/tcc-self2-build/tcc.o /tmp/tcc-self3.o && echo "FIXPOINT"
FIXPOINT
```

This is the first time tcc has self-hosted on big-endian PowerPC.
tcc has historically had no PPC backend in any release; this fork
adds one and now reaches the canonical self-host milestone.

## What was needed (since 019)

Two related codegen bugs blocked `gen_opic_sdiv` (constant-folded
division) which in turn broke the entire preprocessor expression
evaluator (`#if 1+1`, `#if FOO == 0`, etc.) and made `tcc-self`
unable to read `math.h`.

### `tcc/tccgen.c`: initialize `sv.r2` in `save_reg_upstack`

Upstream tcc allocates the temporary `SValue sv` on the stack and
sets `.type.t`, `.r`, `.c.i`, `.sym`, but **not** `.r2`. On x86 and
ARM that doesn't matter — their `store()` doesn't look at `sv->r2`.
Our PPC `store()` does (to distinguish full-LL stores from
single-half stores), so it was reading garbage and unpredictably
choosing the wrong code path.

Fix: set `sv.r2 = VT_CONST` explicitly. Single-line addition.

### `tcc/ppc-gen.c`: fix `store(VT_LLONG, VT_LOCAL)` single-half path

With `sv->r2` reliably `VT_CONST`, our store's "single half" branch
ran consistently — and was wrong. It wrote the half at `offset+4`
(carrying over from a prior assumption that the LOW slot is at
`offset+4`). But `save_reg_upstack` advances `sv->c.i` by `PTR_SIZE`
between the two halves and expects each call to write at the offset
it was passed.

Fix: the "single half" branch now writes at `offset` verbatim. The
"full LL" branch (when `sv->r2` is set) still writes HIGH at
`offset` and LOW at `offset+PTR_SIZE` per BE convention.

Combined with the 019 BE fixes, save/load symmetry now holds:
`save_reg` writes HIGH at L, LOW at L+4; `gv()` reads LOW from L+4
first, HIGH from L second; both match the BE memory layout.

## Test record

| Test | Result |
|---|---|
| `tcc-self` compiles `tcc.c` (ONE_SOURCE) | 613KB `.o`, no errors ✓ |
| Link `tcc-self2` from that `.o` + `floatundidf` stub + libgcc | 493972 bytes ✓ |
| `tcc-self2 -v` | prints version ✓ |
| `tcc-self2` compiles a hello-world `printf` program | runs, prints, exit 0 ✓ |
| `tcc-self2` compiles `tcc.c` again → `tcc-self3.o` | identical to `tcc-self2-build/tcc.o` ✓ |
| Constant fold `100/7`, `200/4`, `10/2`, `5*5`, `7-3` | all correct ✓ |
| Runtime `fact(5)`, `printf("hello %d", 42)` via tcc-self | correct ✓ |
| All previous demos (s005..s016) | still pass ✓ |

`tcc-self` and `tcc-self2` are not byte-identical — they differ at
byte 176 (a header timestamp / build-info-style field). The
**third-generation `.o`** matches the second-generation `.o`
exactly, which is the canonical fixpoint test for a self-hosting
compiler.

## Bootstrap procedure

The fully reproducible bootstrap on the G3:

```
$ ssh imacg3
$ cd ~/tcc-darwin8-ppc
$ ./scripts/build-tiger.sh                  # build ./tcc with gcc-4.0
$ /tmp/bootstrap-tcc-self.sh                # ./tcc compiles tcc.c → tcc-self
$ /tmp/build-tcc-self2.sh                   # tcc-self compiles tcc.c → tcc-self2
$ /tmp/tcc-self2 -v                         # works
```

Both bootstrap scripts use `-D__FLT_EVAL_METHOD__=0` because tcc
doesn't predefine that macro and `<math.h>` requires it. Adding it
as a builtin would let us drop the workaround.

The link step still needs `__floatundidf` from a tiny gcc-built
stub object alongside the `.o`. Should be added to `tcc/lib/` for
a properly-self-contained tarball.

## Files changed (since 019)

- `tcc/tccgen.c`: added `sv.r2 = VT_CONST` in save_reg_upstack;
  removed redundant fast-path branch in gv() LL load.
- `tcc/ppc-gen.c`: store(VT_LLONG, VT_LOCAL) single-half writes at
  given offset (no implicit +4).

## What remains

1. Bundle `__floatundidf` (and the rest of libtcc1's libgcc-style
   helpers) properly into `tcc/lib/lib-ppc.c` so self-host links
   without a hand-built stub.
2. Predefine `__FLT_EVAL_METHOD__=0` as a builtin macro in tccpp's
   tcc_predefs() for PPC.
3. `tcc-self -run <fn-call.c>` still SIGILLs (JIT path; non-blocking
   for self-host since OBJ output works).
4. Cut a release. `v0.1.0-g3` — first self-hosting tarball.

## Next session

021 — package up the /opt-installable G3 tarball. With self-host
working, the path to a v0.1.0-g3 release is mostly mechanical:
fold the `__floatundidf` stub into libtcc1, predefine the missing
macro, add a build script for the tarball, write release notes.
