# Handoff — end of session 050 (2026-05-07)

## TL;DR

One release shipped this session: **v0.2.35-g3**. Headline:

* **libm LDBL128 redirect** — predefining `__LONG_DOUBLE_128__=1`
  triggers `<architecture/ppc/math.h>`'s `__LIBMLDBL_COMPAT` macro
  to emit `__asm("_<sym>$LDBL128")` for ldexpl/expl/sinl/etc.,
  pointing them at the 16-byte-LD Tiger libm entries.
* **`make test3` diff: 45 → 13 lines** (remaining 13 are
  documented-structural).

* HEAD: `868edf1`.
* All commits and `v0.2.35-g3` tag on `origin/main`.

## Four-release evening summary (sessions 047/048/049/050)

| | Tag | Headline | Closes |
|---|---|---|---|
| 047 | v0.2.32-g3 | LD = IBM double-double | abitest-cc 22/24 → 24/24 |
| 048 | v0.2.33-g3 | libtcc callback PLT trampolines | upstream `libtest` |
| 049 | v0.2.34-g3 | Precision-correct dd arithmetic | LD precision polish |
| 050 | v0.2.35-g3 | libm LDBL128 redirect (math.h analog of v0.2.32) | test3 diff 45→13 |

All tests green at every step; bootstrap fixpoint holds at every
release; all real-world demos pass.

## What landed since v0.2.34

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.35-g3 | `307bceb`..`868edf1` (2) | 111/111 | **libm LDBL128 redirect.** Predefine `__LONG_DOUBLE_128__=1` so Tiger's `<architecture/ppc/math.h>` macro `__LIBMLDBL_COMPAT(sym)` expands to `__asm("_" #sym "$LDBL128")`. Closes the bug where parse_number's internal `ldexpl(mantissa, exp)` was a no-op (returning input unchanged) because plain `_ldexpl` is the legacy 8-byte-LD entry; verified by [`v0.2.35-libm-ldbl128.sh`](../../../demos/v0.2.35-libm-ldbl128.sh). |

## Real-world programs

| Program | Demo | Status |
|---|---|---|
| Lua 5.4.7 | [`v0.2.12-lua.sh`](../../../demos/v0.2.12-lua.sh) | ✅ |
| zlib 1.3.1 | (no demo) | ✅ |
| bzip2 1.0.8 | [`v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) | ✅ |
| cJSON 1.7.18 | [`v0.2.21-cjson.sh`](../../../demos/v0.2.21-cjson.sh) | ✅ |
| HTTP server | [`v0.2.21-httpd.c`](../../../demos/v0.2.21-httpd.c) | ✅ |
| sqlite3 (file) | [`v0.2.23-sqlite-file.sh`](../../../demos/v0.2.23-sqlite-file.sh) | ✅ |
| Tiger libz via tcc -lz | [`v0.2.26-link-dylib.sh`](../../../demos/v0.2.26-link-dylib.sh) | ✅ |
| LD ABI tcc-vs-gcc match | [`v0.2.32-longdouble.sh`](../../../demos/v0.2.32-longdouble.sh) | ✅ |
| JIT callback into host | [`v0.2.33-libtcc-callback.sh`](../../../demos/v0.2.33-libtcc-callback.sh) | ✅ |
| dd-precision invariants | [`v0.2.34-dd-precision.sh`](../../../demos/v0.2.34-dd-precision.sh) | ✅ |
| **libm LDBL128 redirect** | [`v0.2.35-libm-ldbl128.sh`](../../../demos/v0.2.35-libm-ldbl128.sh) | ✅ |

## Upstream `make test` snapshot (post-v0.2.35)

Same set passing as post-v0.2.33: version, hello-exe, hello-run,
libtest, abitest-cc, abitest-tcc, vla_test-run, tests2-dir, pp-dir,
memtest, dlltest. test3 diff is now down to 13 lines of structural
divergence (documented in session 050 README).

Still failing on:

| Stage | Reason |
|---|---|
| `libtest_mt` | "running fib in threads" — multi-threaded JIT compilation. Out of scope per CLAUDE.md (libtcc thread safety disabled at build time). |
| `test3` | 13 lines of structural diff (alignof, _Bool size, UB cast). Not bugs. |
| `cross-test` | Needs `dispatch/dispatch.h`. Out of scope. |
| `btest` / `test1b` / `tccb` | Need real `bcheck.c` port. Multi-session. |

## Open work

### Closed across sessions 047-050

* ~~B1 — Apple PPC IBM double-double LD~~ ✅ v0.2.32.
* ~~libtest REL24 out of range~~ ✅ v0.2.33.
* ~~Lossy dd helpers~~ ✅ v0.2.34.
* ~~test3 LD-subnormal divergence~~ ✅ v0.2.35.

### Bugs / quality items

1. **libtcc thread safety** (was OOS per CLAUDE.md; would unblock
   libtest_mt's "running fib in threads" stage).

2. **test3 structural curation.** 13 remaining diff lines are all
   intentional differences (Apple PPC32 ABI alignof for double in
   structs is 4 not 8; gcc-4.0's `_Bool` is 4 bytes vs C99's 1;
   `csf`-cast UB chosen differently). Could either curate the
   reference output or filter known-structural lines from the diff.

3. **`fmadd` codegen.** PPC has hardware fused multiply-add but
   tcc-PPC doesn't emit it. Adding `__builtin_fma()` would let dd
   `two_prod` use 2 flops instead of ~10. Small win on G3, no
   correctness impact.

### Structural items

4. **DWARF debug info for PPC**. `tccdbg.c` has no PPC blocks.
   Multi-target plumbing. Unblocks lldb/gdb debugging.

5. **Real bcheck.c port**. Multi-session. Unblocks
   `btest`/`test1b`/`tccb`.

6. **AltiVec intrinsics**. Multi-session. Big project. Niche.

7. **Self-link diagnostics**. Quality-of-life: when EXE writer
   fails the user gets `dyld` errors with no context.

### Two-level namespace polish

When extra dylibs are loaded, the writer switches to flat
namespace. Strict two-level requires per-symbol ordinal tracking.

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh                          # 111/111
./demos/v0.2.32-longdouble.sh                    # PASS
./demos/v0.2.33-libtcc-callback.sh               # PASS
./demos/v0.2.34-dd-precision.sh                  # PASS
./demos/v0.2.35-libm-ldbl128.sh                  # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### Suggested next pickup

If continuing one-session items: **B3 (fmadd codegen)** — adds
`__builtin_fma`, recognizes the call in tccgen.c, emits PPC's
`fmadd` directly. Minor perf win for any fma-using code; the dd
two_prod in lib-ppc.c can be simplified to two flops once
available.

If continuing structural: **#5 (real bcheck.c port)** unlocks 3
upstream test stages but is multi-session.

If looking for visible-to-users impact: **#4 (DWARF)** would make
tcc-built programs debuggable with lldb on Tiger. Multi-session
plumbing — touches `tccdbg.c` and the Mach-O writer.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents in any of sessions 047-050. Four releases in one
evening, each compounding on the prior session's investigation
context. The 050 fix (the math.h analog of 047's stdio.h fix)
would have been hard to brief into a fresh subagent — it required
"remember v0.2.32's `$LDBL128` symbol-redirect work, here's a
similar bug for math functions, the reference gcc-4.0 binary uses
the LDBL128 entry too, what's the corresponding macro in
math.h?".

## Closing notes

Four releases tonight, each scoped tightly. The pattern that
emerged: when a session closes one bug, scan the immediate
neighborhood for related issues that the original investigation
hadn't surfaced. Session 047 fixed the LD ABI but missed the
libm-side LDBL128 redirect; session 050 finished that work.
Session 048 fixed an SHN_ABS-PLT bug; session 049 fixed the lossy
helpers that v0.2.32 had shipped as placeholders.

The B-list inheritance is now genuinely cleared. Next pickups
require either tackling a multi-session structural item (DWARF,
bcheck, AltiVec) or finding new small bugs from upstream-test
diff inspection. Both paths are reasonable; neither is urgent.
The session-046 HANDOFF mistake (libtest claimed-but-not-passing)
is a useful reminder to verify upstream-test claims rather than
taking them at face value.
