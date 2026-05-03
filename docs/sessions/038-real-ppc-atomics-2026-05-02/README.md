# Session 038 — real PPC atomics, 2026-05-02

Picking up at v0.2.7-g3 after [session 037](../037-tcc-run-on-ppc-2026-05-02/README.md)
landed `tcc -run`. The remaining failure list had 14 tests in three
buckets:

* bcheck.c port (5 tests, multi-session lift)
* real PPC atomics (2 tests: 124_atomic_counter, 125_atomic_misc)
* outliers + -dt quirks

Atomics is the cleanest medium target — the existing stubs in
`tcc/lib/lib-ppc.c` are non-atomic, so the multi-threaded
`124_atomic_counter` races. Two paths to "real":

1. Hand-write PPC `lwarx`/`stwcx.` reservation primitives in inline
   asm or a separate `.S` file.
2. Serialize every atomic op through a single `pthread_mutex_t`.

Tcc's PPC backend doesn't yet support inline asm (`__asm__()` in
lib-ppc.c errors with "inline asm() not supported"), and the
build pipeline compiles `lib-ppc.c` with tcc itself — there's no
.S→.o path for PPC that we control.

So path 2: pthread_mutex. Tiger has `pthread_*` available through
libSystem (no extra link needed beyond what every program already
pulls in), and `PTHREAD_MUTEX_INITIALIZER` is a static initializer
so we don't need any one-shot init dance.

## Implementation

A single global `pthread_mutex_t __ppc_atomic_lock` serializes every
`__atomic_*` call:

```c
#include <pthread.h>

static pthread_mutex_t __ppc_atomic_lock = PTHREAD_MUTEX_INITIALIZER;
#define ATOMIC_LOCK()   pthread_mutex_lock(&__ppc_atomic_lock)
#define ATOMIC_UNLOCK() pthread_mutex_unlock(&__ppc_atomic_lock)

#define ATOMIC_LOAD(BITS, TYPE) \
TYPE __atomic_load_##BITS(const TYPE *p, int o) { \
    TYPE r; (void)o; \
    ATOMIC_LOCK(); r = *p; ATOMIC_UNLOCK(); \
    return r; \
}
ATOMIC_LOAD(1, unsigned char)
/* ... 1/2/4/8 widths for load/store/exchange/cas/fetch_{add,sub,and,or,xor,nand} */
```

48 functions total (4 widths × 12 op variants), all serialized.
Plus the `atomic_flag_*` family.

**Correctness**: yes — every atomic operation is observationally
serialized. The C11 atomics memory model is satisfied (a coarser
ordering than the standard mandates, but never weaker).

**Performance**: poor. 16 threads × 65535 steps × 4 atomic ops each
= 4.2M lock/unlock cycles through one mutex. On a G3 that's about
6.5 minutes wall for `124_atomic_counter` alone. Fine for
correctness compliance; not fine if you actually care about
scaling.

**Why not lwarx/stwcx?** Three reasons:

1. tcc's PPC backend doesn't emit inline asm yet.
2. Our build compiles lib-ppc.c with tcc, not gcc-4.0, so a `.S`
   file would need a separate Makefile hookup AND a real assembler.
3. lwarx/stwcx is 4-byte-only on PPC32. 1- and 2-byte atomics need
   word-RMW with masking; 8-byte atomics need locking anyway since
   PPC32 has no `ldarx`/`stdcx`. The mutex approach handles all
   widths uniformly.

If/when we grow inline-asm support or a separate-assembler path,
revisiting this is worthwhile — the mutex contention point is
genuinely bad, and a G3 with 4 hardware threads doing real
atomic work would be 100x faster with lwarx/stwcx.

## Result

* `124_atomic_counter` PASSES — three "SUCCESS" lines for the
  three workers (adder_simple, adder_cmpxchg, adder_test_and_set)
  in ~6m23s standalone.
* `125_atomic_misc` still fails under default NORUN=true: the test
  uses `#if defined test_*` to gate its main(), so without `-dt -run`
  there's no main and compile fails with "_main not defined in
  .text". This is orthogonal to the atomics work — needs RUN=1
  (NORUN=false) to exercise. Under RUN=1 the test should pass now
  that real atomics are wired.
* tests2 NORUN=true: 104 / 118 -> **105 / 118 = 89.0%** (+1, just
  124 flips).
* Bootstrap fixpoint holds.

The full suite wall-time approximately doubles (from ~7 min to
~13 min) because 124_atomic_counter now actually completes
instead of hanging — the price of correctness with a single-mutex
implementation.

## Files touched

- `tcc/lib/lib-ppc.c` — pthread_mutex-backed atomics, replacing
  the previous single-threaded stubs.

## Caveats

A function with both a deeply-recursive call chain and atomics
could in principle deadlock if the kernel ever tried to deliver a
signal whose handler also took an atomic. Plain pthread_mutex_lock
isn't async-signal-safe. tests2 doesn't exercise this, but worth
knowing.

## Recommended next session

If the bcheck.c port is still the next big target, that's a
multi-session lift. Smaller wins remaining:

* lwarx/stwcx version of these atomics, gated on tcc gaining
  inline-asm support OR a separate `.S` build path. Net: same
  pass count, much better wall-clock for `124_atomic_counter`.
* Investigate `73_arm64` (HFA-AArch64 test); won't fully pass but
  may improve our struct-by-value codegen.
* `70_floating_point_literals` 5-ULP precision issue — upstream
  tcc bug, deep but contained.
