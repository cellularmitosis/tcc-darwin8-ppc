# Session 038 findings — durable lessons

## Tiger pthread is in libSystem; no -lpthread needed

Surprised me at first. `nm /usr/lib/libpthread.dylib` shows every
pthread function as `U` (undefined), and `nm /usr/lib/libSystem.B.dylib`
also shows them as `U`. But:

```c
#include <pthread.h>
int main() {
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    pthread_mutex_unlock(&m);
    return 0;
}
```

…compiles and links cleanly with just `-B./tcc -I./tcc` (no
`-lpthread`), produces a binary that lists `libSystem.B.dylib` as
its only library dependency, and runs. The actual implementations
appear to live in libSystem proper despite the naming, with
libpthread.dylib serving as a stub-forwarder.

**Implication**: any libtcc1.a helper can call pthread functions
without forcing the user to pass `-pthread`. We use this for the
mutex-backed atomic ops; it'd also work for any other internal
synchronization helper we might want.

## tcc's PPC backend doesn't emit inline asm

`__asm__("add %0, %1, %2" : "=r"(z) : "r"(x), "r"(y))` errors with
"inline asm() not supported". So any libtcc1 code that needs raw
PPC instructions has to come from a separate `.S` file (compiled
by an external assembler), not from a `.c` with inline-asm blocks.

Our build currently has no PPC `.S` → `.o` path — `lib-ppc.c` is
compiled by tcc itself. The Makefile does have a `%.S → %.o` rule
that uses `$(XCC)`, but `XCC = $(TCC)` for PPC, and tcc PPC's
assembler doesn't cover all the instructions we'd need (lwarx /
stwcx / sync / isync / etc).

**Implication**: until tcc gains inline asm OR we add a gcc-4.0
fallback for assembling .S files into libtcc1.a, anything needing
hand-written PPC instructions either goes in `ppc-gen.c`'s codegen
directly (emitted as raw 4-byte words like `o(0x7c0004ac)` for
`sync`) or has to be done via the C-level workaround (e.g. the
mutex approach for atomics).

## PPC's pattern for "real" lock-free atomics, when we eventually want it

For future reference, the canonical PPC32 lock-free atomic pattern
is load-linked / store-conditional:

```
loop:
  lwarx  rD, 0, rA      ; load *rA with reservation, into rD
  ; ...modify rD to compute new value (e.g. addi rD, rD, 1)...
  stwcx. rN, 0, rA      ; store rN to *rA if reservation intact;
                        ; sets CR0[EQ]=1 on success, 0 on failure
  bne-   loop           ; retry on failure
```

For ordering:
* `sync` — full memory barrier (heavy weight). Use before/after
  the lwarx/stwcx pair if the operation has acquire/release
  semantics.
* `lwsync` — lighter store-store + load-store barrier (PPC ≥ 970,
  not on G3/G4 7400 — use `sync` for portability).
* `isync` — instruction barrier; cheap, pairs with the bne- to
  prevent speculation past the loop.

For 1- and 2-byte atomics: PPC32 has no lbarx/sbarx, so we'd need
to do RMW on the containing 4-byte word with masking. For 8-byte
atomics: PPC32 has no ldarx/stdcx (those are PPC64), so the mutex
fallback is the only option.

If/when we add this, the right home is probably a new
`tcc/lib/atomic-ppc.S` compiled by gcc-4.0 during the build.
