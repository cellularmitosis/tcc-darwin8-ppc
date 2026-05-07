# Handoff — end of session 049 (2026-05-07)

## TL;DR

One release shipped this session: **v0.2.34-g3**. Headline:

* **Precision-correct double-double arithmetic** in `lib-ppc.c`
  replaces the lossy v0.2.32 placeholder stubs.
* `(2^53 + 1) - 2^53 == 1` now (was 0).
* Closes the B1 polish item from session 048's HANDOFF.

* HEAD: `1da1733`.
* All commits and `v0.2.34-g3` tag on `origin/main`.

## Three-release evening summary (sessions 047/048/049)

| | Tag | Headline | Closes |
|---|---|---|---|
| 047 | v0.2.32-g3 | LD = IBM double-double | abitest-cc 22/24 → 24/24 |
| 048 | v0.2.33-g3 | libtcc callback PLT trampolines | upstream `libtest` |
| 049 | v0.2.34-g3 | precision-correct dd arithmetic | dd polish |

All tests green at every step; bootstrap fixpoint holds at every
release; all real-world demos still pass.

## What landed since v0.2.33

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.34-g3 | `3c76470`..`1da1733` (2) | 111/111 | **Precision-correct double-double arithmetic.** Replace lossy `__gcc_q*` / tf2 stubs with Knuth Two-Sum + Veltkamp split + Dekker product (the standard libgcc soft-fp dd algorithms). No fma dependency. tf2 compares are lexicographic over `(hi, lo)` — exact for canonical dd values. Verified by [`v0.2.34-dd-precision.sh`](../../../demos/v0.2.34-dd-precision.sh) (5 dd-precision invariants). |

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
| **dd-precision invariants** | [`v0.2.34-dd-precision.sh`](../../../demos/v0.2.34-dd-precision.sh) | ✅ |

## Upstream `make test` snapshot (post-v0.2.34)

Same as post-v0.2.33: version, hello-exe, hello-run, libtest,
abitest-cc, abitest-tcc, vla_test-run, tests2-dir, pp-dir, memtest,
dlltest all passing. Still failing on:

| Stage | Reason |
|---|---|
| `libtest_mt` | Multi-threaded JIT compilation — libtcc thread safety OOS per CLAUDE.md. |
| `test3` | Known content diffs (alignof quirks, _Bool size, signed/unsigned UB cases). Not LD-related. |
| `cross-test` | Needs `dispatch/dispatch.h`. Out of scope. |
| `btest` / `test1b` / `tccb` | Need real `bcheck.c` port. Multi-session. |

## Open work (in roughly decreasing impact)

### A. Closed across sessions 047 / 048 / 049

* ~~B1 — Apple PPC IBM double-double LD~~ ✅ v0.2.32.
* ~~libtest REL24 out of range~~ ✅ v0.2.33.
* ~~Lossy dd helpers~~ ✅ v0.2.34.

### B. Bugs / quality items

1. **libtcc thread safety** (was OOS per CLAUDE.md; would unblock
   libtest_mt's "running fib in threads" stage and any genuinely
   concurrent libtcc embedding).

2. **test3 content-diff curation.** Most remaining diffs are
   structural (alignof, _Bool size, UB) — not bugs. Could either
   curate the reference output or add a tolerance to the test
   driver. Tedious but small.

3. **`fmadd` codegen.** PPC has hardware fused multiply-add but
   tcc-PPC doesn't emit it. Adding `__builtin_fma()` (and ideally
   recognizing the `a*b + c` pattern in gen_opf) would let the dd
   `two_prod` use 2 flops instead of ~10. Small win on G3, no
   correctness impact.

### C. Structural items

4. **DWARF debug info for PPC** (item #8). `tccdbg.c` has no PPC
   blocks. Multi-target plumbing. Unblocks gdb/lldb debugging.

5. **Real bcheck.c port** (item #11). Multi-session. Unblocks
   `btest`/`test1b`/`tccb`.

6. **AltiVec intrinsics** (item #9). Multi-session. Big project.

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
./scripts/run-tests2.sh                          # 111/111
./demos/v0.2.32-longdouble.sh                    # PASS
./demos/v0.2.33-libtcc-callback.sh               # PASS
./demos/v0.2.34-dd-precision.sh                  # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### Suggested next pickup

If continuing on B-bugs: **B3 (fmadd codegen)** is small and
self-contained. Adds a `__builtin_fma` token, recognizes it in
gen_opf, emits PPC's `fmadd` instruction. Small win for tcc-built
programs that use fma; the `dd_two_prod` in lib-ppc.c can also
shrink to two flops once the codegen exists. One-session lift.

If continuing on structural: **#5 (real bcheck.c port)** unlocks 3
upstream test stages but is multi-session.

If looking for something visible to users: **#4 (DWARF)** would
make tcc-built programs debuggable with lldb / gdb on Tiger.
Multi-target plumbing — touches `tccdbg.c` and the Mach-O writer.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents in any of sessions 047/048/049. Three releases in one
evening, each building on the prior session's accumulated
investigation context. The pattern: a complete fix arc (diagnose →
implement → test → polish) tends to want one continuous head doing
the work.

## Closing notes

Three releases, all green-tested. The B-list from session 046's
HANDOFF is now closed (B1 done; B2 OOS per CLAUDE.md). Next pickup
is a choice between the small-self-contained items (B3 fmadd, A2
test3 curation) or the multi-session structural items (DWARF,
bcheck.c, AltiVec).

A trend worth flagging: each "multi-session lift" we've actually
sat down to look at recently has turned out to fit in one
disciplined session. v0.2.32 (LD = double-double) was tagged
multi-session in three prior HANDOFFs. v0.2.33 (libtest REL24) was
implicitly assumed to need bisection across history, but turned
out to be a one-line fix once we read the existing code carefully.
v0.2.34 (precision-correct dd) was textbook port. The actual
multi-session items (bcheck.c port, full DWARF emission, AltiVec
intrinsics) remain outstanding and are likely genuine
multi-session lifts — but the B-list inheritance from a prior
HANDOFF is worth re-examining each session before deferring.
