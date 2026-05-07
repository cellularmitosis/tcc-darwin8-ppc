# Handoff — end of session 051 (2026-05-07)

## TL;DR

One release shipped this session: **v0.2.36-g3**. Fifth release of
the evening.

* **`__builtin_fma` / `__builtin_fmaf`** emit PPC's hardware
  `fmadd` / `fmadds` inline — one fused instruction per call.
* **lib-ppc.c `dd_two_prod` simplified** from 10 flops (Veltkamp
  split + Dekker product) to 2 flops (mul + fma).
* All v0.2.34 dd-precision invariants still hold.

* HEAD: `4006503`.
* All commits and `v0.2.36-g3` tag on `origin/main`.

## Five-release evening summary (sessions 047/048/049/050/051)

| | Tag | Headline | Concrete win |
|---|---|---|---|
| 047 | v0.2.32-g3 | LD = IBM double-double | abitest-cc 22/24 → 24/24 |
| 048 | v0.2.33-g3 | libtcc callback PLT trampolines | upstream `libtest` |
| 049 | v0.2.34-g3 | Precision-correct dd arithmetic | LD precision polish |
| 050 | v0.2.35-g3 | libm LDBL128 redirect | test3 diff 45 → 13 |
| 051 | v0.2.36-g3 | __builtin_fma codegen | dd_two_prod 10 → 2 flops |

All tests green at every step; bootstrap fixpoint holds at every
release; all real-world demos pass.

## What landed since v0.2.35

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.36-g3 | `4bb410b`..`4006503` (3) | 111/111 | **`__builtin_fma` / `__builtin_fmaf` emit PPC `fmadd`/`fmadds` inline.** New tokens in tcctok.h; new `gen_ppc_fmadd` helper in ppc-gen.c (computes `FRA*FRC + FRB` per PPC convention); new dispatcher case in tccgen.c expression parser parses 3 args, gv-rotate-gv-rotates them into 3 distinct FPRs, calls the helper. lib-ppc.c's dd_two_prod uses `__builtin_fma(a, b, -p)` to compute the rounding error in one exact instruction, replacing the Veltkamp+Dekker form. |

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
| libm LDBL128 redirect | [`v0.2.35-libm-ldbl128.sh`](../../../demos/v0.2.35-libm-ldbl128.sh) | ✅ |
| **__builtin_fma codegen** | [`v0.2.36-builtin-fma.sh`](../../../demos/v0.2.36-builtin-fma.sh) | ✅ |

## Upstream `make test` snapshot

Same set passing as post-v0.2.35: version, hello-exe, hello-run,
libtest, abitest-cc, abitest-tcc, vla_test-run, tests2-dir, pp-dir,
memtest, dlltest. Still failing on:

| Stage | Reason |
|---|---|
| `libtest_mt` | Multi-threaded JIT compilation — out of scope per CLAUDE.md (libtcc thread safety disabled). |
| `test3` | 13 lines of structural diff (Apple ABI alignof, `_Bool` size, UB cast). Documented; not bugs. |
| `cross-test` | Needs `dispatch/dispatch.h`. Out of scope. |
| `btest` / `test1b` / `tccb` | Need real `bcheck.c` port. Multi-session. |

## Open work

### Closed across sessions 047-051

* ~~B1 — Apple PPC IBM double-double LD~~ ✅ v0.2.32.
* ~~libtest REL24 out of range~~ ✅ v0.2.33.
* ~~Lossy dd helpers~~ ✅ v0.2.34.
* ~~test3 LD-subnormal divergence (math.h LDBL128)~~ ✅ v0.2.35.
* ~~`__builtin_fma` not recognized / dd_two_prod 10-flop fallback~~ ✅ v0.2.36.

### Bugs / quality items

1. **libtcc thread safety** (was OOS per CLAUDE.md; would unblock
   libtest_mt's "running fib in threads" stage).

2. **test3 structural curation.** 13 diff lines; all documented as
   intentional differences. Could be filtered from the diff, or
   the reference output curated.

### Structural items

3. **DWARF debug info for PPC** (item #8). `tccdbg.c` has no PPC
   blocks. Multi-target plumbing. Unblocks lldb/gdb debugging of
   tcc-built programs.

4. **Real bcheck.c port** (item #11). Multi-session. Unblocks
   `btest`/`test1b`/`tccb`.

5. **AltiVec intrinsics** (item #9). Multi-session. Big project.

6. **Self-link diagnostics** (item #7). Quality-of-life: when EXE
   writer fails the user gets `dyld` errors with no context.

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
./demos/v0.2.36-builtin-fma.sh                   # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### Suggested next pickup

The B-list is now genuinely closed. Next sessions are about
larger structural work or new exploration:

* **DWARF debug info** — high user-facing impact (lldb/gdb work
  on tcc-built programs). Multi-session: need PPC DWARF reg
  numbers, .debug_line emission, .debug_info with subprogram
  tags, Mach-O __DWARF segment placement. Could be staged: line
  table only first, types later.

* **Real bcheck.c port** — unlocks 3 upstream test stages. Needs
  PPC-specific bcheck instrumentation in ppc-gen.c plus the
  bcheck runtime. Multi-session.

* **Real-world program exploration** — try larger programs (jq,
  duktape, mongoose) and see if any surface new bugs. Speculative
  but has been productive in past sessions (v0.2.12 lua, v0.2.18
  bzip2, v0.2.21 cjson all surfaced fixes).

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents in any of sessions 047-051. The five-release evening
benefited from fully accumulated context — each session
identified a follow-up that the prior session's investigation had
hinted at.

## Closing notes

Five releases tonight. The B-list inheritance from session 046 is
closed; what remained was a chain of small follow-ups that each
session's work made obvious. The pattern that emerged:

1. v0.2.32 (LD ABI) — the headline, 26 months of "multi-session"
   shipping in one session.
2. v0.2.33 (libtest fix) — uncovered by 047's verification of
   prior HANDOFF claims.
3. v0.2.34 (dd precision) — closed v0.2.32's "lossy" placeholder.
4. v0.2.35 (libm LDBL128) — discovered while diff-bisecting
   test3 after the LD work.
5. v0.2.36 (fmadd codegen) — natural next step after v0.2.34's
   dd_two_prod hit the no-fma fallback path.

The remaining items in the open-work list are genuine
multi-session structural lifts. Stopping here is a reasonable
end-state for the evening.
