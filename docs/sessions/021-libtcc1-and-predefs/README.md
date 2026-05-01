# 021 — Self-contained self-host

Closes the last two papercuts left by [020](../020-self-host/README.md):

- `tcc-self` no longer needs an external gcc-built `__floatundidf`
  stub — it's now bundled in `libtcc1.a`.
- `tcc-self` no longer needs `-D__FLT_EVAL_METHOD__=0` on the command
  line — `tccpp.c` predefines it for PPC just like gcc/clang do.

Both bootstrap scripts (`scripts/bootstrap-tcc-self.sh`) work
unchanged; the workarounds simply disappear.

## What was added

### `tcc/lib/lib-ppc.c` (new)

```c
double __floatundidf(unsigned long long x)
{
    double hi = (double)(unsigned int)(x >> 32);
    double lo = (double)(unsigned int)x;
    return hi * 4294967296.0 + lo;
}
```

Splits the 64-bit value into two unsigned-int halves so the body
itself doesn't reach for `__floatundidf` recursively. The
unsigned-int → double conversion goes through tcc's inline
magic-constant trick in `gen_cvt_itof` (no helper call).

### `tcc/lib/Makefile` (+5 lines)

- Add `OBJ-ppc-osx = lib-ppc.o $(OSX_O)` so the libtcc1 build
  includes our PPC helper.
- Override `XAR = $(AR)` for PPC targets — tcc's built-in `-ar`
  driver only handles ELF object files, so the system `ar` is used
  to assemble the Mach-O `.o` into `libtcc1.a`. (Eventually the tcc
  archiver should grow Mach-O support; out of scope for now.)

### `tcc/tccpp.c` (+7 lines)

Add `#define __FLT_EVAL_METHOD__ 0` to `tcc_predefs()` for PPC.
Apple's `<math.h>` requires this macro; gcc and clang predefine it
based on the float ABI. tcc evaluates FP expressions at the
operand's nominal type (no upgrades), which is the C99 "0" value.

### `scripts/bootstrap-tcc-self.sh` (new)

Replaces the per-session ad-hoc `/tmp/bootstrap-tcc-self.sh` with a
checked-in script. Takes `TCC=` (the compiler to use, default
`./tcc/tcc`), `OUT=` (output binary, default `/tmp/tcc-self`), and
`WORK=` (build dir, default `/tmp/tcc-self-build`). Same script
serves for the first generation (gcc-built tcc → tcc-self) and any
subsequent generation (tcc-self → tcc-self2, tcc-self2 → tcc-self3,
…).

## Test record

| Test | Result |
|---|---|
| `make` produces `libtcc1.a` containing `lib-ppc.o` | ✓ |
| `scripts/bootstrap-tcc-self.sh` (default args) | builds `/tmp/tcc-self` ✓ |
| `TCC=/tmp/tcc-self ./scripts/bootstrap-tcc-self.sh OUT=/tmp/tcc-self2 WORK=/tmp/tcc-self2-build` | builds `/tmp/tcc-self2` ✓ |
| `tcc-self2 -v` | works ✓ |
| `tcc-self2` compiles `tcc/tcc.c` → byte-identical to `tcc-self2-build/tcc.o` | FIXPOINT ✓ |
| Compile `(double)0x100000005ULL` → run, divide by 2^32 | exit 1 ✓ |

## What's left for `v0.1.0-g3`

1. Build a `/opt`-installable tarball:
   - `/opt/tcc-0.1.0-g3/bin/tcc` (= our self-hosted binary)
   - `/opt/tcc-0.1.0-g3/lib/libtcc1.a`
   - `/opt/tcc-0.1.0-g3/include/` (for tcc's own includes)
   - Maybe a wrapper script that points `-B` at the installed dir.
2. Release notes / install README.
3. Optional: tag `v0.1.0-g3` once the tarball is verified on a
   fresh G3.

These are the next session.
