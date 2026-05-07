# Session 046 — dylib slide + follow-ups (2026-05-06)

## Arrival state

HEAD: `ec9b12d` (post-v0.2.28). Session 045 shipped four
patch releases (v0.2.25 — v0.2.28); see
[`../045-unsupervised-dylib-2026-05-06/HANDOFF.md`](../045-unsupervised-dylib-2026-05-06/HANDOFF.md).

* tests2 111/111
* abitest-tcc 24/24
* dlltest passes
* sqlite-as-dylib stress test passes
* dlopen of tcc-built dylibs works **at preferred vmaddr**

The build host previously named in the handoff (`ibookg37`) is
offline. Switched to `imacg3` (Tiger 10.4 / Darwin 8 / G3, identical
target environment). `~/tcc-darwin8-ppc` on imacg3 was stale (May 1)
so I rsync'd from uranium and re-ran the v0.2.28 baseline.

Sanity baselines:

* `build-tiger.sh configure` ✅
* `FIXPOINT=1 bootstrap-tcc-self.sh` ✅ (fixpoint holds)
* `run-tests2.sh` ✅ 111/111
* `demos/v0.2.25-dylib.sh` ✅
* `demos/v0.2.26-link-dylib.sh` ✅
* `demos/v0.2.27-jit-heisenbug.sh` ✅
* `demos/v0.2.28-clean-abitest.sh` ✅ (abitest-tcc 24/24; abitest-cc
  fails on `ret_longdouble_test` and `arg_align_test`, both expected
  per session 045 — Apple long double 128-bit ABI)

## Goals (in priority order)

1. **A1 — local relocation entries for dylib sliding.** Currently
   tcc-built dylibs only work when dyld loads them at the preferred
   vmaddr (0x40000000). If dyld slides them, absolute references in
   `__data` (e.g. `int *p = &arr[N]` static initializers) won't be
   patched. Function calls survive (PIC stubs). The fix: walk data
   sections during dylib emission, detect ADDR32 relocs, emit
   corresponding `relocation_info` entries in DYSYMTAB.locrel.

2. **C — strict two-level namespace** (when extra dylibs loaded).
   Per-symbol ordinal tracking. Polish.

3. **B4 — Mach-O archive alacarte loader.** Currently force-whole-
   archive. Roadmap item.

I'll keep cutting patch releases as items land.
