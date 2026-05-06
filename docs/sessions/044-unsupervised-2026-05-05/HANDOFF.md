# Handoff — end of session 044 (2026-05-05 → 2026-05-06)

## TL;DR

Three patch releases shipped this session: **v0.2.22**, **v0.2.23**,
**v0.2.24**. The big-ticket items:

* **`int *p = &arr[N]` static initializers actually point at &arr[N]** —
  closes the highest-impact open item from session 043.
* **sqlite3 with on-disk database works end-to-end** — fifth
  real-world program (lua, zlib, bzip2, cJSON, sqlite). Closes
  both deferred sqlite bugs from session 041.
* **`tcc -ar` is now native Mach-O** — tcc is fully self-sufficient
  on Tiger PPC. No more `XAR=$(AR)` workaround. Closes structural
  item #3 from the roadmap.

* HEAD: `2f078dd`.
* tests2: still **111 / 111 (100%)**.
* Bootstrap fixpoint still holds.
* `tcctest.c` diff vs gcc reference: **44 → 33 lines** (relocation_test
  divergence gone; remaining 33 are all "won't fix").
* `abitest-cc` upstream: dramatically improved — only `ret_longdouble_test`
  and `arg_align_test` fail (both gated on Apple PPC IBM double-double
  long double support, multi-session work). All sret/many_struct/
  reg_pack/etc. tests now pass.
* All commits and changes on `origin/main`.

## What landed since session 043 close (v0.2.21-g3)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.22-g3 | `3512438`..`657dcf0` (2) | 111/111 | **`int *p = &arr[N]` (N != 0) actually points at &arr[N].** Three-place fix: `ppc-link.c::relocate()` switches R_PPC_ADDR32 from OVERWRITE to ADD (matches i386's `R_386_32` implicit-addend convention); `ppc-macho.c::exe_resolve_section_relocs()` mirrors that; `ppc-macho.c::macho_translate_relocs()` zeros the in-place after the .o loader absorbs the addend into the synthetic anchor sym. tcctest.c diff drops 44 → 33 lines. |
| v0.2.23-g3 | `be861b4`..`7e9b3da` (2) | 111/111 | **sqlite3 with on-disk database works end-to-end.** Static-init data slots like `(sqlite3_syscall_ptr)fcntl` used to resolve to the `__nl_symbol_ptr` slot ADDRESS (4 bytes of data, not callable code), so calling them faulted. Fix: in `collect_extern_stubs`, also allocate a stub when an extern `STT_FUNC` is data-referenced via `R_PPC_ADDR32` (was: only `R_PPC_REL24` calls). Closes the deferred file-open SEGV + (already-fixed-by-v0.2.22-but-never-retested) CREATE TABLE → SQLITE_CORRUPT bug from session 041. |
| v0.2.24-g3 | `9a5aa8d`..`2f078dd` (2) | 111/111 | **`tcc -ar` is native Mach-O on PPC** (was: ELF only, fell back to `/usr/bin/ar`). New code path inside `tcc_tool_ar()` handles fat binaries (PPC32 slice selection), parses Mach-O header + LC_SYMTAB, walks 12-byte nlist entries, selects defined-external syms, writes BE-32 symdef offsets host-endian-independently. Removed `XAR=$(AR)` workaround; libtcc1.a now builds via in-tree tcc. Closes structural item #3. **Implemented by Sonnet subagent in one shot — first try success, ~140 lines of C.** |

## Real-world programs

| Program | Demo | Status |
|---|---|---|
| Lua 5.4.7 | [`v0.2.12-lua.sh`](../../../demos/v0.2.12-lua.sh) | ✅ Builds + runs |
| zlib 1.3.1 | (no separate demo) | ✅ Builds + full test suite passes |
| bzip2 1.0.8 | [`v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) | ✅ Builds + round-trips byte-identical |
| cJSON 1.7.18 | [`v0.2.21-cjson.sh`](../../../demos/v0.2.21-cjson.sh) | ✅ Builds + parses |
| HTTP server | [`v0.2.21-httpd.c`](../../../demos/v0.2.21-httpd.c) | ✅ BSD sockets work |
| **sqlite3 (file)** | **[`v0.2.23-sqlite-file.sh`](../../../demos/v0.2.23-sqlite-file.sh)** | **✅ Full CREATE/INSERT/SELECT/ORDER BY round-trip** |

## Upstream `make test` snapshot

Stages passing: version, hello-exe, hello-run, libtest,
vla_test-run, pp-dir (24/24), tests2-dir (111/111), memtest. (Same
as session 043.)

Stages still failing:

| Stage | Why |
|---|---|
| `libtest_mt` | Multi-threaded JIT. Item C10 from session 043. Likely related to the abitest-tcc heisenbug. |
| `test3` | Iterated test (compile tcc N times). Hits the abitest-tcc heisenbug class. |
| `abitest-cc` | Only `ret_longdouble_test` + `arg_align_test` fail — both gated on Apple PPC IBM double-double long double (B8). Big improvement over baseline (was failing many more tests). |
| `abitest-tcc` | JIT heisenbug at `ret_2float_test`. Best-of-15: 19 passing (up from HANDOFF's 14). Worst-case: 5. Reproduced in 30-line minimal harness — see Open work A2 below. |
| `dlltest` | Needs `tcc -shared` Mach-O dylib output. Multi-session lift, attempted but deferred this session (see Open work B5). |
| `cross-test` | Needs `dispatch/dispatch.h`, not on Tiger. Out of scope. |
| `btest` / `test1b` / `tccb` | Need real `bcheck.c` port. Multi-session. |

## Open work (in roughly decreasing impact)

### A. Bugs we identified but didn't fix this session

1. **abitest-tcc / libtest_mt / test3 JIT-context heisenbug**
   (deferred 4 sessions now; partially characterized this
   session). Under various conditions, JIT'd struct-by-value or
   struct-return functions either crash (SEGV / SIGBUS / SIGILL)
   or return wrong results.

   - **Reproduces**: `tcc_new` → compile `two_float f(two_float a)
     { ... return r; }` → `tcc_relocate` → call f via fnp → 
     `tcc_delete`, in a loop. Failure rate varies (5-100% per
     iteration depending on memory layout).
   - **Doesn't reproduce**: same pattern with simple `int f(int)`
     or `double f(double)` — only struct-by-value/struct-return
     hits it.
   - **Pre-existing**: was present in v0.2.21 baseline; v0.2.22-24
     fixes don't introduce or worsen it (best-case test count
     went 14 → 19).
   - **Suspect**: ABI mismatch between gcc-compiled caller and
     tcc-JIT'd callee for sret functions, OR memory aliasing in
     the JIT page allocator (tcc_run_free → mprotect-back-to-RW
     → tcc_free → tcc_malloc reuses the same pages).
   - **Where to look first**: the JIT prolog adds 13 nops between
     the GPR spill block and `stwu r1, -frame_size(r1)` (reserved
     for FPR spills that the function doesn't use). The stack
     frame layout looks right (back-chain, LR slot, saved r30/r31
     in correct positions). The body emits correct fmuls/lfs/stfs.
     The bl-to-memset inside the function uses a relative
     displacement of +0x84fc — verify that lands on a valid memset
     stub.
   - **Failed approaches this session**:
     * Adding marker prints made the bug disappear in the smaller
       test (heisenbug, layout-sensitive).
     * Putting tcc_delete after each iteration didn't help.

2. **Apple PPC IBM double-double long double** (B8 from session 043).
   `ret_longdouble_test` and `arg_align_test` in upstream
   `abitest-cc` would pass if tcc and gcc agreed on `long double`
   size+alignment+arithmetic. Currently tcc treats LD as
   regular 8-byte double (per session 043 v0.2.19's tcctest gating).
   Multi-session lift; `lib-ppc.c` already stubs the `__gcc_q*`
   helpers (lossy fallback).

### B. Structural items (each is multi-hour or multi-session)

3. **Mach-O `-shared` dylib output**. Currently rejected at
   `tcc/ppc-macho.c:2811`. Needs `macho_output_dylib()` writer:
   MH_DYLIB filetype, no __PAGEZERO, LC_ID_DYLIB instead of
   LC_UNIXTHREAD, exported-symbol visibility. Substantial fork
   of `macho_output_exe()` (~1000 lines). Unblocks `dlltest`
   and many real-world programs that ship as `.dylib`. Attempted
   this session, scoped down for size, then deferred.

4. **DWARF debug info for PPC**. `tccdbg.c` has no PPC blocks.
   Multi-target plumbing (~10 places). Unblocks `gdb`/`lldb`
   debugging.

5. **Mach-O archive alacarte loader**. (Item #4 from roadmap.)
   Parse `__.SYMDEF SORTED` and selectively pull members vs
   current force-whole-archive. Less urgent now that `tcc -ar`
   builds libtcc1.a in-tree.

6. **Real bcheck.c port**. Stubs satisfy link but don't actually
   bounds-check. Unblocks `-b` instrumented builds and the
   `btest`/`test1b`/`tccb` upstream stages. Multi-session.

### C. Multi-thread / advanced

7. `libtest_mt` SEGV — almost certainly the same heisenbug as
   abitest-tcc.
8. AltiVec intrinsics — none today; tcc emits scalar code.

## How to pick up

### Quick sanity check
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh   # fixpoint should hold
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.18-bzip2.sh                      # PASS at the end
./demos/v0.2.21-cjson.sh                      # name=alice, sum=15
SQLDIR=/Users/macuser/tmp/sqlite-amalgamation-3460100 \
    ./demos/v0.2.23-sqlite-file.sh            # 3 rows ordered by age
```

### Verify the new `tcc -ar` Mach-O native path
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc/tcc
/opt/make-4.3/bin/make clean && PATH=/opt/make-4.3/bin:$PATH ../scripts/build-tiger.sh
file libtcc1.a                                 # current ar archive
/usr/bin/ar t libtcc1.a                        # all members listed
nm libtcc1.a | head -20                        # ~100+ symbols
```

### Reproduce the JIT heisenbug
```sh
ssh ibookg37
# /tmp/jit_2f_loop.c is a 30-line minimal repro saved during this session.
/Users/macuser/tcc-darwin8-ppc/tcc/tcc \
    -B/Users/macuser/tcc-darwin8-ppc/tcc \
    -I/Users/macuser/tcc-darwin8-ppc/tcc/include \
    -I/Users/macuser/tcc-darwin8-ppc/tcc \
    -o /tmp/jit_2f_loop /tmp/jit_2f_loop.c \
    /Users/macuser/tcc-darwin8-ppc/tcc/libtcc.c -lpthread -ldl -lm
for i in 1 2 3 4 5; do /tmp/jit_2f_loop; echo exit=$?; done
# expect: bug surfaces in some runs (silent early exit, FAIL,
# or non-zero signal), works fine in others
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|
| Bug 3 (`tcc -ar` Mach-O) | Port symbol-extraction from ELF to Mach-O. Self-contained brief: scope, references, TESTING template. | Sonnet (general-purpose) | First-try success. ~140 lines of C, well-scoped. Verified locally — bootstrap, tests2, all real-world demos pass. ~10 min wall, low token cost. |

Verdict: subagents work great when (a) the task is mechanically
well-defined, (b) the brief includes file paths and code
references the agent should consult, (c) the brief includes a
TESTING template the agent fills in (since they can't ssh to the
PPC box themselves). The `tcc -ar` task hit all three boxes.

## Closing notes

Three releases in one session, focused on real-world program
enablement and tooling self-sufficiency. The biggest user-visible
changes are: sqlite3 with on-disk databases (5th real-world
program), and `tcc -ar` not needing `/usr/bin/ar` anymore.

The JIT heisenbug remains the longest-deferred high-impact open
item. It's now reproduced as a 30-line standalone test (rather
than only via abitest's harness), which is progress toward
cracking it next session.

Bootstrap fixpoint and tests2 100% throughout the session — no
regressions from any of the three releases.
