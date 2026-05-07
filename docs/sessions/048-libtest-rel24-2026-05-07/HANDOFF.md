# Handoff — end of session 048 (2026-05-07)

## TL;DR

One release shipped this session: **v0.2.33-g3**. Headline:

* **libtcc callback reachability fixed.** `tcc_add_symbol`-registered
  SHN_ABS callbacks now go through PLT trampolines so a JIT'd
  `bl _callback` always reaches its target on PPC32.
* **Closes the upstream `libtest` stage** — which the session-046
  HANDOFF mistakenly listed as passing.
* tests2 still 111/111, abitest-cc still 24/24, abitest-tcc still
  24/24, dlltest still passes, bootstrap fixpoint still holds.

* HEAD: `78d814d`.
* All commits and `v0.2.33-g3` tag on `origin/main`.

## What landed since v0.2.32 (start of this session)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.33-g3 | `4a4acae`..`78d814d` (3) | 111/111 | **libtcc callback (SHN_ABS) reachability via PLT trampolines.** `build_got_entries` had a `PTR_SIZE != 8` guard that skipped PLT synthesis for SHN_ABS symbols on 32-bit platforms, under the (false-on-PPC) assumption that direct REL24 always reaches absolute symbols. PPC32's 24-bit `bl` only covers ±32MB; the JIT region routinely lands past that. Fix: add `TCC_TARGET_PPC` to the same exception list as ARM. The existing PPC `create_plt_entry` (4-insn `lis/ori/mtctr/bctr`) and `relocate_plt` machinery handles emission unchanged. Plus a small diagnostics improvement to the ppc-link `R_PPC_REL24 out of range` error so it names the offending symbol. Verified by [`v0.2.33-libtcc-callback.sh`](../../../demos/v0.2.33-libtcc-callback.sh) and the upstream `libtest` stage. |

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
| libtcc as dylib + slide | manual | ✅ |
| LD ABI tcc-vs-gcc match | [`v0.2.32-longdouble.sh`](../../../demos/v0.2.32-longdouble.sh) | ✅ |
| **JIT callback into host** | [`v0.2.33-libtcc-callback.sh`](../../../demos/v0.2.33-libtcc-callback.sh) | ✅ |

## Upstream `make test` snapshot (post-v0.2.33)

| Stage | Status | Note |
|---|---|---|
| version | ✅ | |
| hello-exe | ✅ | |
| hello-run | ✅ | |
| **libtest** | ✅ | ← this session's win. |
| libtest_mt | ❌ | Multi-threaded JIT compilation. tcc's libtcc thread safety is disabled at build time (CLAUDE.md: out of scope). Now fails differently than before — looks like the second iteration's compile pass tripping up; could be a separate small bug, but per CLAUDE.md OOS. |
| test3 | ❌ | Known content diffs (LD subnormal output) since v0.2.32 made LD codegen real. tcctest itself runs to completion. |
| abitest-cc | ✅ | 24/24. |
| abitest-tcc | ✅ | 24/24. |
| vla_test-run | ✅ | |
| tests2-dir | ✅ | 111/111. |
| pp-dir | ✅ | 24/24. |
| memtest | ✅ | |
| dlltest | ✅ | |
| cross-test | ❌ | Needs `dispatch/dispatch.h`, not on Tiger. Out of scope. |
| btest / test1b / tccb | ❌ | Need real `bcheck.c` port. Multi-session. |

## Open work (in roughly decreasing impact)

### A. Closed since session 046's HANDOFF

* ~~**B1 — Apple PPC IBM double-double long double**~~ ✅ v0.2.32.
* ~~**libtest REL24 out of range**~~ ✅ v0.2.33.

### B. Bugs / quality items not yet addressed

1. **Precision-correct double-double arithmetic.** Our `__gcc_q*`
   and tf2 helpers in `lib-ppc.c` are LOSSY (high-double only).
   Real precision-preserving versions would do the standard
   compensated Knuth-style double-double algorithms. Programs that
   rely on >53-bit LD precision (rare in practice) get truncated
   results. abitest values fit in double's 53-bit mantissa so this
   isn't visible there.

2. **libtcc thread safety** (was OOS per CLAUDE.md; would unblock
   libtest_mt). Now also unblocks libtest_mt's
   second-iteration-compile-failure mode.

3. **test3 content-diff curation.** With LD now genuinely
   16-byte, several tcctest output lines differ from the
   gcc-built reference (subnormal long-double formatting). The
   test framework treats any diff as failure. We could either
   (a) update the reference output, or (b) maintain a diff
   tolerance. Either way: small but tedious work.

### C. Structural items

4. **DWARF debug info for PPC** (item #8). Unblocks gdb/lldb.

5. **Real bcheck.c port** (item #11). Stubs satisfy link but don't
   actually bounds-check. Unblocks `btest`/`test1b`/`tccb`.
   Multi-session.

6. **AltiVec intrinsics** (item #9).

7. **Self-link diagnostics** (item #7).

### D. Two-level namespace polish

When extra dylibs are loaded, the writer switches to flat namespace.
Strict two-level requires per-symbol ordinal tracking.

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.32-longdouble.sh                 # PASS
./demos/v0.2.33-libtcc-callback.sh            # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### Suggested next pickup

If continuing on B-bugs: **#1 (precision-correct dd)** is a clean
self-contained polish task — port libgcc's compensated double-double
routines into lib-ppc.c, validate against gcc-built reference for
edge cases (extended-precision sums, near-equal subtraction). One
session lift.

If continuing on structural: **#5 (real bcheck.c port)** unlocks 3
upstream test stages but is multi-session.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. Two sessions back-to-back from one investigation arc
benefited from accumulated context. The fix here was small (one
include-PPC line in tccelf.c) but only after an exact diagnosis
in tcc-link.c using a debug-print patch — and the diagnosis was
helped by knowing from session 047's verification that this was a
pre-existing issue, not a new regression.

## Closing notes

Two releases in one evening. v0.2.32 was the headline (LD ABI),
v0.2.33 is the cleanup (libtest had been broken for at least a
session, and the symptom — `R_PPC_REL24 out of range` — was easier
to chase end-to-end while the LD investigation context was still
fresh).

The PLT path on PPC32 had been there since v0.2.7's first
`tcc -run` work, but it was unreachable for SHN_ABS symbols. The
fix is one line and immediately closes a previously-deferred
test stage. Worth flagging as a pattern: when a multi-session lift
turns out to be one line, the missing piece was almost always
"who am I telling not to bypass this machinery, and why?" — in
this case, an old comment about 64-bit-only that didn't match the
underlying constraint.
