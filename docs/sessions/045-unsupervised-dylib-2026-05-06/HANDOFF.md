# Handoff — end of session 045 (2026-05-06)

## TL;DR

Four patch releases shipped this session: **v0.2.25**, **v0.2.26**,
**v0.2.27**, **v0.2.28**. Headline:

* **Mach-O dylib output works.** `tcc -shared -o foo.dylib foo.c`
  produces a real PPC dylib that dyld loads via dlopen.
* **Link-time dylib support works.** `tcc -lz hello.c` actually
  works at runtime — closes upstream **`dlltest`**.
* **Long-deferred JIT heisenbug fixed.** One-line bug:
  `__clear_cache` was a no-op stub when tcc compiled itself.
  Delegate to libSystem's `sys_icache_invalidate`. Closes
  5+ sessions of deferred pain across abitest-tcc, libtest_mt,
  test3, dlltest.
* **abitest-tcc is now 24/24** (was variable 5–19 with heisenbug,
  then deterministic 20/24 after heisenbug fix). Last 4 tests
  fixed by `save_regs(nb_args)` (vs `nb_args+1`) so the
  func-ptr LVAL's volatile-GPR address survives arg setup.
* **PPC backtraces work.** `rt_get_caller_pc` + `rt_getcontext`
  Tiger PPC. libtest_mt "producing some exceptions" passes;
  test1b prints real "RUNTIME ERROR" with backtrace.

* HEAD: `a218bbb`.
* tests2: still **111 / 111 (100%)**.
* Bootstrap fixpoint still holds.
* All commits and changes on `origin/main`.

## What landed since v0.2.24 (start of this session)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.25-g3 | `7e54907`..`bb08966` (2) | 111/111 | **First Mach-O dylib output.** ~600 lines of `macho_output_dylib()` in ppc-macho.c. MH_DYLIB filetype, no __PAGEZERO/UNIXTHREAD/crt-shim, LC_ID_DYLIB, default __TEXT vmaddr 0x40000000. PIC stubs (8 insns / 32 bytes each, `(slot - .L1)` SECTDIFF anchor) — invariant under whole-image slide. LC_DYSYMTAB always emitted (else dyld faults in `doBindExternalRelocations` during dlopen). |
| v0.2.26-g3 | `0e2838f`..`c11d93a` (2) | 111/111 | **Link-time dylib support.** `macho_load_dll()` (was no-op) parses LC_SYMTAB, registers defined-externals as UNDEF in our symtab, captures install name from LC_ID_DYLIB, adds DLLReference. Output writers emit one LC_LOAD_DYLIB per loaded dll; switch to flat namespace (clear MH_TWOLEVEL) when extras are loaded. **Closes upstream `dlltest`**. |
| v0.2.27-g3 | `deb6a84`..`645ee5e` (2) | 111/111 | **JIT heisenbug fixed (5+ sessions deferred).** `__clear_cache` no-op stub when tcc compiles itself → JIT page reuse saw stale icache → SIGILL/SIGBUS/SIGSEGV at random iterations. Delegate to libSystem's `sys_icache_invalidate`. One-line. test3 runs to completion; abitest-tcc deterministic 20/24 (was variable 5–19); dlltest stable. |
| v0.2.28-g3 | `d027c24`..`5fc7df2` (2) | 111/111 | **abitest-tcc 24/24 + Tiger PPC backtraces.** `save_regs(nb_args)` (vs `nb_args+1`) so func-ptr LVAL's volatile-GPR address survives arg setup. `rt_get_caller_pc` + `rt_getcontext` Tiger PPC (`mcontext->ss.{srr0,r1}`). many_struct_test_3 / stdarg_* / arg_align_test all flip from crash to pass. libtest_mt "producing some exceptions" passes. test1b reports proper RUNTIME ERROR. |
| (cleanup) | `a218bbb` | 111/111 | Drop unused `sect_const`/`sect_stub`/`sect_nlptr` decls from `macho_output_exe`. |
| (polish)  | `291343e` | 111/111 | New demo `v0.2.28-sqlite-dylib.sh` — builds the entire 250-kloc sqlite3 amalgamation as a `tcc -shared` dylib, then dlopens it and runs CREATE TABLE + INSERT. Real-world dylib stress test passes. |
| (polish)  | `0ecc2bc` | 111/111 | Dedupe libSystem in `LC_LOAD_DYLIB` output. `-lpthread`/`-ldl`/`-lm` all install-name to libSystem on Tiger; dyld saw two entries before. Now one. |

## Real-world programs

| Program | Demo | Status |
|---|---|---|
| Lua 5.4.7 | [`v0.2.12-lua.sh`](../../../demos/v0.2.12-lua.sh) | ✅ |
| zlib 1.3.1 | (no demo) | ✅ |
| bzip2 1.0.8 | [`v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) | ✅ |
| cJSON 1.7.18 | [`v0.2.21-cjson.sh`](../../../demos/v0.2.21-cjson.sh) | ✅ |
| HTTP server | [`v0.2.21-httpd.c`](../../../demos/v0.2.21-httpd.c) | ✅ |
| sqlite3 (file) | [`v0.2.23-sqlite-file.sh`](../../../demos/v0.2.23-sqlite-file.sh) | ✅ |
| **Tiger libz via tcc -lz** | **[`v0.2.26-link-dylib.sh`](../../../demos/v0.2.26-link-dylib.sh)** | **✅** |

## New demos this session

* [`v0.2.25-dylib.sh`](../../../demos/v0.2.25-dylib.sh) — first Mach-O dylib output.
* [`v0.2.26-link-dylib.sh`](../../../demos/v0.2.26-link-dylib.sh) — `tcc -lz` against Tiger's libz.
* [`v0.2.27-jit-heisenbug.sh`](../../../demos/v0.2.27-jit-heisenbug.sh) — 30-iteration JIT loop proving heisenbug fix.
* [`v0.2.28-clean-abitest.sh`](../../../demos/v0.2.28-clean-abitest.sh) — abitest-tcc 24/24.

## Upstream `make test` snapshot

Stages passing: version, hello-exe, hello-run, libtest, vla_test-run,
pp-dir (24/24), tests2-dir (111/111), memtest, **abitest-cc** (with
known long-double diffs), **abitest-tcc (24/24)**, **dlltest**, test3
(runs to completion; only fails on known content diffs).

Stages still failing:

| Stage | Why |
|---|---|
| `libtest_mt` | "producing some exceptions" now passes (rt_get_caller_pc fix). Next failure is "running fib in threads" — multi-threaded JIT compilation. tcc's libtcc thread safety is disabled at build time (CLAUDE.md: out of scope). |
| `cross-test` | Needs `dispatch/dispatch.h`, not on Tiger. Out of scope. |
| `btest` / `test1b` / `tccb` | Need real `bcheck.c` port. Multi-session. Now report proper "RUNTIME ERROR" backtraces (rt_get_caller_pc fix) but the bounds-check infrastructure itself is no-op stubs. |

## Open work (in roughly decreasing impact)

### A. Bugs / quality items not addressed this session

1. **Local relocation entries for dylib sliding** (was open work
   B3 from session 044, not done in v0.2.25). Currently dylibs
   work only when dyld loads them at the preferred vmaddr
   (0x40000000). If dyld slides them, absolute references in
   `__data` (e.g. `int *p = &arr[N]` static initializers) won't
   be patched. Function calls survive (PIC stubs). For an MVP
   this is acceptable; programs hit the slide path rarely with a
   high default vmaddr. Would need: walk data sections during
   dylib emission, detect ADDR32 relocs, emit corresponding
   `relocation_info` entries in DYSYMTAB.locrel.

2. **Apple PPC IBM double-double long double** (B8 from session 043).
   `ret_longdouble_test` and `arg_align_test` in `abitest-cc` —
   the latter passes in `abitest-tcc` because both compilers
   agree on `long double = double`. Multi-session lift.

3. **libtcc thread safety** — currently OOS per CLAUDE.md; would
   unblock libtest_mt's "running fib in threads" plus any
   genuinely concurrent JIT use.

### B. Structural items

4. **Mach-O archive alacarte loader** (item #4 in roadmap). Parse
   `__.SYMDEF SORTED` and selectively pull members vs current
   force-whole-archive. Less urgent now that `tcc -ar` builds
   libtcc1.a in-tree.

5. **DWARF debug info for PPC** (item #8). `tccdbg.c` has no PPC
   blocks. Multi-target plumbing. Unblocks `gdb`/`lldb`
   debugging.

6. **Real bcheck.c port** (item #11). Stubs satisfy link but
   don't actually bounds-check. Unblocks `-b` instrumented
   builds and the `btest`/`test1b`/`tccb` upstream stages.
   Multi-session.

7. **AltiVec intrinsics** (item #9). None today; tcc emits
   scalar code.

### C. Two-level namespace polish

When extra dylibs are loaded, v0.2.26's writer switches to flat
namespace (clears MH_TWOLEVEL). That works but is slightly less
efficient and more permissive (any dylib's symbol can shadow
another). Strict two-level requires per-symbol ordinal tracking
(side-table mapping each undef sym → source dll). Future polish.

## How to pick up

### Quick sanity check
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.25-dylib.sh                       # dylib hello
./demos/v0.2.26-link-dylib.sh                  # tcc -lz
./demos/v0.2.27-jit-heisenbug.sh               # 30 OK
./demos/v0.2.28-clean-abitest.sh               # 24/24
SQLDIR=/Users/macuser/tmp/sqlite-amalgamation-3460100 \
    ./demos/v0.2.23-sqlite-file.sh             # 3 rows
```

### Verify upstream `make test` improvement
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc/tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make -k test
# Expect: many more PASS than at session 044. dlltest passes
# (was multi-session deferred). abitest-tcc clean. test3 runs
# to completion. memtest passes. Only btest/test1b/tccb (bcheck)
# and cross-test (dispatch.h) and libtest_mt "running fib in
# threads" (thread safety) still fail.
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents used in session 045. The work was a mix of careful
code design (dylib writer layout, PIC stubs, dyld quirks) and
focused debugging (heisenbug → root-cause → one-line fix), both
of which were faster solo than a brief-and-iterate cycle would
have been. The handoff also documents two cases where the
narrative arc — "reproduce → bisect → root-cause → fix → verify"
— was the value, not the lines of code.

## Closing notes

Four releases in one session. Each closes a multi-session
deferred item:

* **dylib output**: highlighted by the user as the next big
  achievement.
* **dylib link-time support**: closes upstream `dlltest`.
* **JIT heisenbug**: 5+ sessions deferred. One-line root cause
  (no-op `__clear_cache` stub) once we knew where to look. The
  comment in the stub itself was the trail.
* **abitest-tcc 24/24 + backtraces**: turning the heisenbug
  fix into deterministic state revealed the next bug
  (`save_regs(nb_args + 1)` skipping the func ptr) which has
  probably been latent in the codebase since the first
  `gfunc_call`.

The remaining test failures cluster into two groups: needs
bcheck.c port (btest/test1b/tccb) and needs libtcc thread
safety (libtest_mt fib-in-threads). Both are deliberate
out-of-scope items per CLAUDE.md / roadmap. The honest test
score on the upstream stages we DO target is now extremely
high.
