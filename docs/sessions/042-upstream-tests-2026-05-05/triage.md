# Upstream `make test` baseline triage

Full log in [upstream-test-baseline.log](upstream-test-baseline.log).

Ran `cd tcc/tests && /opt/make-4.3/bin/make -k test` on ibookg37 at
HEAD `81a109e` after a clean rebuild.

## ✅ Passing cleanly

| Stage | Notes |
|---|---|
| `version` | tcc -v works |
| `hello-exe` | `tcc -o hello ex1.c && ./hello` |
| `hello-run` | `tcc -run ex1.c` |
| `libtest` | gcc-built libtcc_test compiling `tcc.c` via libtcc API |
| `vla_test-run` | 3/3 |
| `pp-dir` | 24/24 PPTests |

## ⚠️ Mostly passing

### `tests2-dir` — 110/113 attempted, 3 fail under `-run`

The runner uses `-run` (not NORUN=true like our `scripts/run-tests2.sh`).
Three tests use `FLAGS += -b` and fail under `-b -run` because tcc
can't find `bcheck.o` and `bt-log.o` to link in at runtime:

```
+tcc: error: file 'bcheck.o' not found
+tcc: error: file 'bt-log.o' not found
```

Affected: `121_struct_return`, `122_vla_reuse`, `132_bound_test`.

These pass cleanly in our `run-tests2.sh` because NORUN=true emits an
exe and runs it, and the no-op stubs in `tcc/lib/lib-ppc.c` (already
in libtcc1.a) satisfy the bcheck symbols. Under `-run` though, tcc
literally hunts for the OBJECT FILES `bcheck.o` and `bt-log.o` by
name in the lib path — see `tccelf.c` / `tccrun.c`. Our stubs are
inside libtcc1.a, not free-standing object files, so the run-mode
lookup misses.

**Tractable fix**: produce a shipped no-op `bcheck.o` and `bt-log.o`
alongside libtcc1.a (or add `121/122/132` to `NORUN = true` in
`tests2/Makefile`). Either is a few lines.

## ❌ Failing — real codegen bugs (priority targets)

### `abitest-cc` — gcc-built abitest exercising our libtcc

| Test | Result |
|---|---|
| `ret_longdouble_test` | failure |
| `ret_8plus2double_test` | "ppc-gen: parameters exceed 8 FPR slots" + failure |
| `arg_align_test` | failure |

These run inside `abitest-cc`, an ABI conformance test that uses
libtcc to JIT little C snippets and call them. Failures imply our
codegen / ABI handling has gaps the wider tests2 doesn't catch.

* `ret_longdouble_test` — Apple PPC long double is 128-bit IBM
  double-double; might not be wired up. Worth checking if we even
  claim to support it.
* `ret_8plus2double_test` — explicit error from `ppc-gen.c`. Diagnostic
  is "parameters exceed 8 FPR slots". On PPC SysV/Apple, FPR slots
  for params is f1..f13 (13 slots) — so the limit message itself
  looks wrong. Likely a stale guard in our ABI.
* `arg_align_test` — likely related to the long-long / Apple PPC
  4-byte alignment fix from session 041; either the abitest exercises
  a path that fix didn't cover, or it's a different alignment case
  (e.g. struct-padding pass-by-value).

### `abitest-tcc` — tcc-built abitest

```
ret_int_test... success
ret_longlong_test... success
ret_float_test... success
ret_double_test... success
ret_longdouble_test... success         ← (succeeded here, not in -cc)
ret_2float_test... make[1]: *** Bus error
```

Bus error in `ret_2float_test` when the abitest binary itself was
compiled by our tcc. Different failure mode than the cc-built
version, which suggests tcc-compiled abitest has additional codegen
issues triggered by the test infrastructure (libtcc.c gets compiled
into the test binary, which exercises a lot more code). Probably the
biggest "find-bugs-fast" target.

### `memtest` — 512 bytes leaked

`MEM_DEBUG=2` reports 512 bytes leaked from `ppc-gen.c:1873`. One
allocation site, one stack — tractable.

```
MEM_DEBUG: mem_leak= 512 bytes, mem_max_size= 4097602 bytes
../ppc-gen.c:1873: error: 512 bytes leaked
```

### `libtest_mt` — SEGV

The multi-threaded libtcc test crashes in "running tcc.c in threads
to run fib". Earlier "could not relocate tcc state" messages may be
deliberate (one thread holds the state, others get bumped). The SEGV
at the end is real.

```
running tcc.c in threads to run fib
 make[1]: *** [Makefile:102: libtest_mt] Segmentation fault
```

## 🚧 Tiger environment limitations (not codegen bugs)

### `test.ref` — gcc-built tcctest.gcc fails dyld load

```
dyld: Symbol not found: _weak_v1
  Referenced from: tcctest.gcc
  Expected in: flat namespace
make[1]: *** [Makefile:120: test.ref] Error 133
```

`tcctest.c` declares `weak_v1` weak-undefined and expects dyld to
resolve to NULL on Tiger. Tiger 10.4 dyld does NOT support
weak-undefined symbols in flat namespace — they were added later
(10.5+). gcc-4.0 on Tiger emits the symbol but the loader can't
resolve it.

This is the upstream tcctest.c hardcoding a Linux/post-10.5
assumption. **It's blocking `test3` (the canonical Auto Test)**
because `test3` requires test.ref as the reference output.

**Tractable fix**: gate the weak-symbol portion of tcctest.c off
when building on Tiger; or provide our own test.ref by running
gcc with appropriate flags (`-Wl,-undefined,dynamic_lookup`?).

## 🚫 Known unsupported features (defer)

| Stage | Why |
|---|---|
| `dlltest` | "ppc-macho: only -c and -o exe are implemented" — dylib output unported |
| `cross-test` | re-runs `tcc.c` with cross targets enabled, pulls in `dispatch/dispatch.h` (10.6+) |
| `btest`, `tccb` | bcheck.c is unported |
| `bcheck.o`/`bt-log.o` | (see tests2-dir note above) |

## Suggested attack order

Highest expected return per hour of work:

1. **Fix test.ref / weak symbol issue** → unlocks `test3`, the
   canonical 3-level recursive Auto Test on tcctest.c (4500 lines,
   covers everything from macros to floats to bitfields). This is
   one of the most useful single tests in tcc.
2. **memtest leak at ppc-gen.c:1873** → likely 1-line fix.
3. **abitest-cc failures** → 3 concrete ABI bugs.
4. **abitest-tcc Bus error** → could share a root cause with the
   above, or be its own thing.
5. **libtest_mt SEGV** → multi-threading; deeper.
6. **tests2-dir 121/122/132** → ship dummy `bcheck.o` / `bt-log.o`,
   or NORUN-override.
