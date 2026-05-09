# Session 056 findings

## v0.2.46-g3 — float-to-int const-fold saturation

### Bug

tcc on PPC had **two divergent float-to-int paths** for the same C
source:

* **Const-fold path** (`gen_cast` in tccgen.c): used host
  `(int64_t)long_double` / `(uint64_t)long_double`. The host's
  libgcc `__fix*di` either saturates to *int64* range (returning
  -1 = `0xFFFFFFFFFFFFFFFF` on positive overflow) or simply
  truncates the bottom 64 bits. After tcc masks to 32 bits and
  sign-extends, this gives `-1` for a hugely-positive float.
* **Runtime path** (`gen_cvt_ftoi` in ppc-gen.c): emits PPC's
  `fctiwz` instruction, which saturates to int32 range —
  `0x7FFFFFFF` for positive overflow / `0x80000000` for negative
  overflow / NaN.

Same `int x = (int)1e10f` in C → two different values depending
on whether tcc folds at compile time (`1410065408`) or evaluates
at run time (`0x7FFFFFFF`). gcc-4.0 saturates in both cases.

### How csmith found it

`--float --builtins` seed-92's `func_65` contains:

```c
(*g_93) = safe_sub_func_float_f_f(
    ((*l_108) = (
        (safe_mod_func_uint16_t_u_u(...) , 0x7.C870D1p+91)
    )),
    (*g_93)
);
```

The comma's right operand is the float constant `0x7.C870D1p+91`
(≈ 1.93e28). The assignment to `*l_108` (an `int *` aliased to
the global `g_99`) requires float→int conversion. With the float
being a compile-time constant, tcc folds — and the fold returns
-1, not the saturated INT_MAX gcc-4.0's runtime fctiwz produces.

### Fix

`tccgen.c` `gen_cast`, in the constant-fold branch:

```c
#if defined(TCC_TARGET_PPC) && !defined(TCC_TARGET_PPC64)
if (dbt_bt != VT_LLONG) {
    long double ld = vtop->c.ld;
    if (ld != ld)             /* NaN */
        vtop->c.i = (int64_t)(int32_t)0x80000000;
    else if (ld >= 2147483648.0L)
        vtop->c.i = 0x7FFFFFFF;
    else if (ld < -2147483648.0L)
        vtop->c.i = (int64_t)(int32_t)0x80000000;
    else
        vtop->c.i = (int64_t)ld;
} else
#endif
```

Then the existing post-mask at lines 3551-3558 handles char/short
truncation by zeroing high bits and sign-extending bit 7/15.

VT_LLONG keeps the existing path because the runtime there goes
through `__fixsfdi` / `__fixdfdi` (libtcc1 lib-ppc.c) whose
overflow semantics already match the host helper (both return -1
on positive overflow). No need to override.

### Why fctiwz semantics, not C-compliant clamp?

For unsigned int targets, fctiwz is technically wrong — it gives
`0x7FFFFFFF` for `(unsigned int)1e10f` but C semantics would want
either bottom-32-bits or `UINT_MAX`. But tcc's *runtime*
`gen_cvt_ftoi` also uses fctiwz for unsigned VT_INT (no
PPC-native unsigned-fp-to-int instruction), so matching fctiwz in
the const folder keeps const-fold and runtime consistent. Fixing
unsigned-int saturation correctly would be a separate runtime
change. (The C standard says out-of-range float-to-int is
undefined behavior, so any consistent answer is conformant.)

### Verification

* Minimal repro pre-fix: `int x = (int)0x7.C870D1p+91f; printf("%d", x);`
  printed -1 (now prints 2147483647).
* csmith seed-92: gcc-4.0=7B499BDB, tcc pre-fix=65EE2E44, tcc
  post-fix=7B499BDB.
* tests2: 111/111 still passes.
* abitest-cc / abitest-tcc: 22/22 each.
* Bootstrap fixpoint: holds (tcc-self2.o == tcc-self3.o).
* All 9 demos pass (32, 33, 40, 41, 42, 43, 44, 45, 46).

## creduce learning

While debugging, I started creduce with 4 parallel workers on a
565-line program with PID-isolated remote-side temp files. Each
test iteration is a scp + ssh round-trip (~3.6s); in 30 minutes
of running it had only reduced 21% → 27% by bytes. Manual
narrowing (instrument with printfs to find the diverging
function, then read the source for the offending construct) was
~10× faster on this specific program because:

1. csmith outputs the function-call graph as a comment at the
   top of each function (`reads`, `writes` lists).
2. The harness's `print_hash_value=1` prints intermediate
   checksums per global variable, which makes "first divergent
   global" a sub-second diff.
3. Once the divergent global is known, instrumenting each
   function's return with a printf of that global localizes the
   bug to a single function call.

Worth keeping in mind: creduce is the right tool for unstructured
test-case minimization, but for csmith specifically — where the
program structure is regular and there's a built-in tracing
mechanism — manual narrowing wins.

## Bug-hunt yield estimate

After this fix, tcc's float-to-int paths are consistent for PPC.
There may be a separate runtime bug for unsigned-int FP
conversion (`(unsigned int)1e10f` saturates as signed instead of
to UINT_MAX) but it's not surfacing in csmith differentials
because gcc-4.0 happens to give the same wrong answer in some
cases — needs deeper investigation if it ever surfaces.

## Open from session 055 still open

* Lua `files.lua` line 84 `assert(io.output():seek() == 0)` still
  fails on tcc-built lua. Not investigated this session.
* Dead `VT_LLONG` store path cleanup. Not done.
* OSO STAB emission for gdb (roadmap #7.5).
* Self-link diagnostics (roadmap #7).
* AltiVec intrinsics (roadmap #9).
* Real bcheck.c port (roadmap #11).
