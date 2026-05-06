# Session 042 — upstream tcc test suite (2026-05-05)

## Arrival state

* HEAD: `81a109e` (HANDOFF + ppc-codegen-pitfalls postmortem from
  session 041).
* Latest release: **v0.2.16-g3**.
* tests2: **111/111 (100.0%)** under `scripts/run-tests2.sh`.
* zlib 1.3.1 full test suite: passes.
* sqlite in-memory SELECT: works. CREATE TABLE → SQLITE_CORRUPT and
  `sqlite3_open()` → SEGV both deferred.

See [../041-pickup-2026-05-03/HANDOFF.md](../041-pickup-2026-05-03/HANDOFF.md)
for the full picture of where we were.

## Goal of this session

Up to now we'd only run our home-grown `tests2` runner. Upstream
tcc has a much richer test suite under `tcc/tests/` (`make test`):
`tcctest.c`, `abitest.c`, `libtcc_test*.c`, `vla_test.c`,
`asm-c-connect-*`, `pp/`, `memtest`, `dlltest`, `cross-test`,
`btest`, `tccb`. Run it cold, triage, fix what's tractable.

## Result

5 commits, several genuine codegen / lib gaps closed. After-fixes
score:

| Stage | Before | After |
|---|---|---|
| `version` | ✅ | ✅ |
| `hello-exe` | ✅ | ✅ |
| `hello-run` | ✅ | ✅ |
| `libtest` | ✅ | ✅ |
| `libtest_mt` | ❌ SEGV (Error 139) | ❌ Error 255 (different mode) |
| `test3` | 🚫 didn't run | ⚠️ runs, gets 770/4500 lines into tcctest then SEGVs |
| `test1b` | 🚫 didn't run | ⚠️ runs, missing `__bound_strrchr` symbol |
| `abitest-cc` | ❌ 3 fails | ❌ same 3 fails (need long double 128-bit + 13-FPR) |
| `abitest-tcc` | ❌ Bus error | ❌ same Bus error |
| `vla_test-run` | ✅ | ✅ |
| `tests2-dir` (under `make test`) | ❌ 108/111 (3 -b -run failures) | ✅ **111/111** |
| `pp-dir` | ✅ 24/24 | ✅ 24/24 |
| `memtest` | ❌ 512-byte leak | ✅ |
| `dlltest` | 🚫 unsupported | 🚫 unsupported |
| `cross-test` | 🚫 unsupported | 🚫 unsupported |
| `btest` | 🚫 bcheck unported | 🚫 bcheck unported |
| `tccb` | 🚫 bcheck unported | 🚫 bcheck unported |

* Bootstrap fixpoint: still holds.
* Local `scripts/run-tests2.sh`: still 111/111.

Net new wins:
* +3 tests in tests2-dir under `-run` mode (121, 122, 132)
* memtest passes (was a 512-byte leak)
* test3 / test1b now actually run (previously couldn't even build
  the gcc-built reference output `test.ref`)

## Commits landed (5)

| | Commit | Headline |
|---|---|---|
| 1 | `30a1825` | ppc-gen: add VT_STRUCT case in load() for sym+off path |
| 2 | `8514952` | lib: add builtin.o, alloca-ppc.S, fixsfdi/fixunssfdi to libtcc1.a |
| 3 | `46d2721` | tcctest: skip weak_test on Apple PPC |
| 4 | `f89527e` | ppc-gen, libtcc: free ppc_pic_pairs side-table at tcc_delete |
| 5 | `a0e8587` | lib: ship bcheck.o + bt-log.o stubs for PPC |

See [findings.md](findings.md) for the surprising / load-bearing
ABI corners we hit. Full triage notes in [triage.md](triage.md).

## Bugs fixed in detail

### 1. `tcctest.c::stdarg_test` — VT_STRUCT in sym+off load (ppc-gen.c)

`stdarg_test` passes `pts[3]` (a struct of 2 doubles, indexed into
a global array) through varargs. tcc errored out at compile time:

```
tcctest.c:2870: error: ppc-gen: load via sym+off of bt 0x7 not yet supported
```

The PIC / sym-relative load path in `ppc-gen.c::load()` had cases
for VT_BYTE, VT_SHORT, VT_INT/PTR/FUNC under the `extra_off != 0`
branch but no VT_STRUCT. The matching extra_off == 0 case below
already handled it correctly: "loading" a struct value via a
symbol just means computing its address. For non-zero offset,
addr_gpr already holds &sym (after the addi at 826-829), so we
just need `addi r, addr_gpr, extra_off`.

### 2. Missing libtcc1.a helpers — `__builtin_*`, `alloca`, `__fixsfdi`

Three independent gaps surfaced when building tcctest.c:

* `__builtin_ffs` / `__builtin_clz` / `__builtin_ctz` /
  `__builtin_popcount` / `__builtin_parity` (and long / longlong
  variants): the portable C reference impl in `lib/builtin.c` was
  simply not added to OBJ-ppc-osx. Compiles cleanly under tcc.

* `alloca`: tcc emits direct calls to `_alloca`. macOS does NOT
  provide it in libSystem (alloca is a compiler builtin on Apple).
  New `lib/alloca-ppc.S` does the standard PPC leaf-function trick:
  round size up to 16, drop sp by size+32, replicate back chain at
  new sp, return new_sp+32 so subsequent callees still have room
  for their linkage area without stomping the alloca'd region.

* `__fixsfdi` / `__fixunssfdi` (float → long long / unsigned long
  long): one-line wrappers over `__fixdfdi` / `__fixunsdfdi`.

### 3. `tcctest.c` `weak_test` — gate off on Apple PPC

Tiger 10.4 dyld can't resolve undefined-weak symbols at flat-
namespace load time. `tcctest.gcc` exits before main() with
"Symbol not found: _weak_v1" even with `-undefined dynamic_lookup`.
This blocked `make test.ref` (and therefore `test1`/`test2`/
`test3`). Extended the existing `__APPLE__ + GCC_MAJOR>=15` skip to
also include `__ppc__ / __ppc64__`.

### 4. `ppc_pic_pairs` 512-byte leak (memtest)

`MEM_DEBUG=2` reported "512 bytes leaked" pointing at
`ppc-gen.c:1873`. The PIC pair side-table is `tcc_realloc`'d on
demand and never freed. `ppc_pic_pairs_reset()` only resets the
count to allow reuse across TUs in the same TCCState. Added
companion `ppc_pic_pairs_free()` called from `tcc_delete()` under
`#ifdef TCC_TARGET_PPC`.

### 5. tests2 121/122/132 — bcheck.o / bt-log.o stub files

`tcc -b -run` looks up `bcheck.o` and `bt-log.o` BY NAME via
`tccelf.c::tcc_add_runtime`, fails with "file 'bcheck.o' not
found". Our `__bound_*` stubs already live in lib-ppc.c so
libtcc1.a covers the symbols, but the file lookup is positional —
the .o files must exist on disk regardless. Added minimal
`lib/bcheck-ppc.c` and `lib/bt-log-ppc.c` installed as the
canonical names. After this, all 3 tests pass under upstream
`make test` (the `-run` path).

## What's left (deferred — listed in priority order)

### `test3` (and `test1`) — gets 770/4500 lines into tcctest.c

After the 5 fixes above, `tcc -run tcctest.c` compiles cleanly and
runs ~770 lines deep into the 4500-line stress test, then SEGVs in
`stdarg_test` mid-`printf("%f", 1.0)`. Output stops at "1.0000"
(4 chars of "1.000000"). Earlier output also shows divergences vs
test.ref:
* `aligntest3 sizeof=16 alignof=4` (ours) vs `alignof=8` (ref) —
  expected, our Apple PPC ABI alignment fix from session 041.
* `sizeof(_Bool) = 1` (ours) vs `4` (ref) — gcc-4.0 quirk.
* `sizeof(long double) = 8` (ours) vs `16` (ref) — we don't
  implement IBM double-double yet.
* `promote char/short funcret 137 -1` (ours) vs `-1987475063 -1`
  (ref) — sign-extend bug somewhere in fn-result narrow types.

A minimal repro of the stdarg crash (extracted from `vprintf1`)
runs cleanly. Need to bisect what's different about the in-test
context — possibly stack pressure or local-variable interaction.

### `abitest-cc`: `ret_8plus2double_test` — needs 13 FPR support

Apple PPC32 ABI uses **f1..f13** for FP args, not f1..f8 (which is
SysV/Linux convention). Bumping requires re-sizing `NB_REGS`,
`reg_classes[]`, `RC_F(x)`'s bitmask shift, and the prolog's
FP-spill area. Probably half a session's work to do correctly.

### `abitest-cc`: `ret_longdouble_test`, `arg_align_test` — 128-bit long double

Apple PPC `long double` is 16-byte IBM double-double. We treat it
as plain 8-byte double (`LDOUBLE_SIZE 8` in ppc-gen.c). A real
implementation would need ABI plumbing + arithmetic helpers
(`__addtf3`, `__multf3`, etc.). Multi-session lift.

### `abitest-tcc` Bus error in `ret_2float_test`

The tcc-built version of abitest crashes on the second test. Same
JIT path as `abitest-cc` (both link against the same libtcc.a) but
the harness itself is compiled differently. Suggests a codegen bug
in the harness, not the JIT. No isolation work done yet — would
need to bisect with smaller versions of the harness.

### `libtest_mt` — multi-threaded libtcc SEGV

The multi-threaded libtcc test ran into "could not relocate tcc
state" messages (probably deliberate stress) then crashed in
"running tcc.c in threads". After our session-042 fixes the
failure mode changed from Error 139 (SEGV) to Error 255, suggesting
slightly different progression but still crashes. Multi-session
work.

### `test1b` — missing `__bound_strrchr` symbol

New failure surfaced after we shipped bt-log.o. `test1b` =
`-b -bt1` mode tries to use bound-checking hooks for libc string
functions. We provide `__bound_strlen / strcmp / strncmp` in
lib-ppc.c but not strrchr/strchr/strstr/etc. Easy to add — add
more pass-through stubs.

### `dlltest` / `cross-test` / `btest` / `tccb`

Same as before: dlltest needs Mach-O dylib output (unported);
cross-test needs cross-compilers configured; btest/tccb need real
bcheck.c port. All multi-session.

## How to pick up

### Quick sanity check
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh                                        # 111/111
cd tcc/tests && /opt/make-4.3/bin/make -k test 2>&1 | tail -100  # ~5 min
```

### Test3 stdarg_test crash repro
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc/tcc/tests
../tcc -B.. -I../include -I.. -I.. -w -o /tmp/tcctest tcctest.c
/tmp/tcctest > /tmp/test.outexe 2>&1
echo "exit=$?"            # 139 (SEGV)
tail -10 /tmp/test.outexe # crashes at "1.0000" in stdarg_test
diff /tmp/test.outexe ~/tcc-darwin8-ppc/tcc/tests/test.ref | head -30
```

## Files modified

* `tcc/ppc-gen.c` — VT_STRUCT in load() sym+off; ppc_pic_pairs_free
* `tcc/libtcc.c` — call ppc_pic_pairs_free in tcc_delete
* `tcc/lib/Makefile` — pull in builtin.o, alloca-ppc.o, bcheck.o, bt-log.o
* `tcc/lib/lib-ppc.c` — `__fixsfdi` / `__fixunssfdi`
* `tcc/lib/alloca-ppc.S` — NEW; PPC alloca leaf function
* `tcc/lib/bcheck-ppc.c` — NEW; minimal bcheck.o stub
* `tcc/lib/bt-log-ppc.c` — NEW; minimal bt-log.o stub
* `tcc/tests/tcctest.c` — gate weak_test off on Apple PPC

## Logs

* [upstream-test-baseline.log](upstream-test-baseline.log) — before
* [upstream-test-after.log](upstream-test-after.log) — after
