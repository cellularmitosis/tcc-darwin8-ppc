# Handoff — end of session 046 (2026-05-06)

## TL;DR

Three patch releases shipped this session: **v0.2.29**,
**v0.2.30**, **v0.2.31**. Headline:

* **Dylib slide works end-to-end** (closes session-045 item A1
  + the v0.2.29 follow-up about __const).
* **Mach-O archive alacarte loader** lands (closes structural
  roadmap item #4).
* libtcc2.dylib (the dlltest harness) loads at a slid address
  and `tcc_compile_string` + `tcc_relocate` + `tcc_get_symbol`
  all run correctly — proving the dylib slide infrastructure
  works for a non-trivial real-world dylib.

* HEAD: `73e9ab0`.
* tests2: still **111 / 111 (100%)**.
* abitest-tcc: still 24/24.
* dlltest: still passes.
* Bootstrap fixpoint still holds.
* All commits and changes on `origin/main`.

## What landed since v0.2.28 (start of this session)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.29-g3 | `c804ee8`..`03bb840` (3) | 111/111 | **Dylib slide support: PIC-for-local-data + locrel emission.** `ppc_need_pic_for_sym` now returns 1 for local syms in DLL mode (PIC sectdiff is slide-invariant); PIC base setup pinned to function prolog so r30 dominates all uses regardless of control flow (lazy emission was unsafe — found via libtcc2's tcc_parse_args). `macho_output_dylib` walks reloc tables of __data/__mod_init_func/__mod_term_func and emits Mach-O VANILLA local relocation_info for each ADDR32-to-local in LC_DYSYMTAB.locrel. dyld walks locrel at load time and adds the slide. Verified by [`v0.2.29-dylib-slide.sh`](../../../demos/v0.2.29-dylib-slide.sh) — `mmap(MAP_FIXED)` blocker forces slide; pointer-to-data, pointer-to-pointer, fn-pointer, fn-array, constructor all check out. |
| v0.2.30-g3 | `702b4f0`..`c4e65ad` (3) | 111/111 | **Mach-O archive alacarte loader.** Pre-fix, tcc force-loaded every member of every `.a` archive on Mach-O. Two changes: (a) new `tcc_load_alacarte_macho` parses BSD `__.SYMDEF` / `__.SYMDEF SORTED` and pulls in only the members that resolve currently-undefined symbols (iterates until no new members add). (b) Existing SysV-style `/` symdef path (used by tcc -ar's libtcc1.a) gained a Mach-O codepath: sniff each member's magic, route `AFF_BINTYPE_MACHO_REL` through `macho_load_object_file`. Closes structural roadmap item #4. Verified by [`v0.2.30-alacarte.sh`](../../../demos/v0.2.30-alacarte.sh): 3-member archive, only 2 referenced; binary contains exactly those 2. |
| v0.2.31-g3 | `dfaf539`..`73e9ab0` (3) | 111/111 | **Const-data-with-pointers slide fix.** v0.2.29's locrel emission only covered __data / __mod_init_func / __mod_term_func; pointer-bearing `static const struct {...} tbl[]` lived in __TEXT,__const which is read-only so dyld couldn't write the slide fixups. tccgen now routes initialized const data to `data_section` instead of `rodata_section` when `output_type == TCC_OUTPUT_DLL`. Closes the v0.2.29 release-notes "Limitation" call-out. Verified by [`v0.2.31-libtcc-slide.sh`](../../../demos/v0.2.31-libtcc-slide.sh) — `static const struct {const char *name; int value;}` lookup table survives slide. Also confirmed end-to-end by manually slide-loading libtcc2.dylib and exercising the full compile API. |

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
| **libtcc as dylib + slide** | (manual; see findings.md) | ✅ |

## New demos this session

* [`v0.2.29-dylib-slide.sh`](../../../demos/v0.2.29-dylib-slide.sh)
  — pointer-to-data, pointer-to-pointer, fn-pointer, fn-array,
  constructor — all survive slide.
* [`v0.2.30-alacarte.sh`](../../../demos/v0.2.30-alacarte.sh)
  — 3-member archive, 2 referenced; verifies exactly 2 are in
  the output binary.
* [`v0.2.31-libtcc-slide.sh`](../../../demos/v0.2.31-libtcc-slide.sh)
  — `{const char *, int}` lookup table survives slide.

## Upstream `make test` snapshot

Stages passing: version, hello-exe, hello-run, libtest,
vla_test-run, pp-dir (24/24), tests2-dir (111/111), memtest,
**abitest-cc** (with known long-double diffs), **abitest-tcc
(24/24)**, **dlltest**, test3 (runs to completion; only fails on
known content diffs).

Stages still failing:

| Stage | Why |
|---|---|
| `libtest_mt` | "running fib in threads" — multi-threaded JIT compilation. tcc's libtcc thread safety is disabled at build time (CLAUDE.md: out of scope). |
| `cross-test` | Needs `dispatch/dispatch.h`, not on Tiger. Out of scope. |
| `btest` / `test1b` / `tccb` | Need real `bcheck.c` port. Multi-session. Now report proper "RUNTIME ERROR" backtraces but the bounds-check infrastructure itself is no-op stubs. |

## Open work (in roughly decreasing impact)

### A. Closed this session

* ~~**A1 — local relocation entries for dylib sliding**~~ ✅ Done in v0.2.29.
* ~~**B4 — Mach-O archive alacarte loader**~~ ✅ Done in v0.2.30.
* ~~**v0.2.29 follow-up: __const slide gap**~~ ✅ Done in v0.2.31.

### B. Bugs / quality items not addressed this session

1. **Apple PPC IBM double-double long double** (B8 from session 043).
   `ret_longdouble_test` and `arg_align_test` in `abitest-cc` —
   the latter passes in `abitest-tcc` because both compilers
   agree on `long double = double`. Multi-session lift.

2. **libtcc thread safety** — currently OOS per CLAUDE.md; would
   unblock libtest_mt's "running fib in threads" plus any
   genuinely concurrent JIT use.

### C. Structural items

3. **DWARF debug info for PPC** (item #8). `tccdbg.c` has no PPC
   blocks. Multi-target plumbing. Unblocks `gdb`/`lldb`
   debugging.

4. **Real bcheck.c port** (item #11). Stubs satisfy link but
   don't actually bounds-check. Unblocks `-b` instrumented
   builds and the `btest`/`test1b`/`tccb` upstream stages.
   Multi-session.

5. **AltiVec intrinsics** (item #9). None today; tcc emits
   scalar code.

6. **Self-link diagnostics** (item #7 — old roadmap #10).
   When the EXE writer fails the user gets `dyld` errors with
   no context. Better messages would shorten future
   025-style debugging considerably.

### D. Two-level namespace polish

When extra dylibs are loaded, v0.2.26's writer switches to flat
namespace (clears MH_TWOLEVEL). Strict two-level requires
per-symbol ordinal tracking (side-table mapping each undef sym
→ source dll). Future polish.

## How to pick up

### Sanity baseline

The session-045 build host (`ibookg37`) was offline; this
session used `imacg3`. The repo on imacg3 is freshly synced
from uranium and current as of `73e9ab0`.

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.29-dylib-slide.sh                # PASS
./demos/v0.2.30-alacarte.sh                   # PASS
./demos/v0.2.31-libtcc-slide.sh               # PASS
```

### Verify dylib slide on a real libtcc2

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc/tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make dlltest    # passes
# Then manually:
../tcc -B.. -I../include -I.. -I.. -DLIBTCC_AS_DLL ../libtcc.c \
    -lm -ldl -lpthread -shared -o libtcc2.dylib
otool -lv libtcc2.dylib | grep nlocrel        # should be ~140
# Slide test programs in /tmp/slide_libtcc2_full2.c (see findings.md)
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents used in session 046. The work was three closely-
related dylib improvements that benefited from accumulated
context across the same investigation arc, plus the alacarte
loader which was a well-scoped change. Spawning subagents
would have been overkill for any of these.

## Closing notes

Three releases in one session, mirroring session 045's pace.
Each closes a specific deferred item or limitation:

* **v0.2.29 dylib slide**: highlighted as A1 in session 045's
  HANDOFF. Required two coupled changes — PIC for local data
  refs (with eager prolog setup) and locrel emission for static
  initializers. The lazy-PIC-base-setup bug was a real surprise
  and a good lesson; it had been latent since the first PIC
  use was added but only surfaced once we had multiple PIC use
  sites in the same function (libtcc2's tcc_parse_args
  triggering it cleanly).

* **v0.2.30 archive alacarte**: structural roadmap item #4.
  The two-format problem (BSD `__.SYMDEF SORTED` vs. SysV `/`)
  was the main complexity; both coexist on Tiger boxes (Apple
  tools produce BSD; tcc -ar produces SysV). The first-draft
  underscore-strip bug was a clean reminder that
  symtab-name conventions differ between compile-time and
  .o-loading.

* **v0.2.31 const-data slide**: closes the "Limitation"
  call-out from v0.2.29's release notes. Routes pointer-bearing
  initialized const data to .data in DLL mode so the locrel
  walker picks it up. Trade-off: lose RO protection for const
  data; gain correctness under slide. With this, the dylib
  slide story is complete for any standard C-with-tcc dylib.

The remaining test failures cluster into the same two groups
as last session: needs bcheck.c port (btest/test1b/tccb) and
needs libtcc thread safety (libtest_mt fib-in-threads). Both
remain deliberate out-of-scope items per CLAUDE.md / roadmap.
The honest test score on the upstream stages we DO target
remains extremely high.
