# Session 093 — GNU m4 1.4.19 build with tcc (2026-05-18)

## Arrival state

- HEAD: `b928d3c` (end of session 092). No tag pending.
- tcc version: 0.9.28rc, v0.2.67-g3 build live on imacg3.
- tests2 last known: 111 / 111 (session 091/092).
- Bootstrap fixpoint: stable.
- Eleven real-world programs already in demos tree (lua, zlib, bzip2,
  cJSON, sqlite, sed, gzip, Python, awk, expat, bc). bc was the
  first post-v0.2.66 program that needed no tcc change.

## Goal

Direction (a) from session 092's HANDOFF: try GNU m4 — the macro
processor autoconf itself runs on. Pure ASCII string/symbol
management — a different bug-surface from bc's FP-heavy
arbitrary-precision workout. Pleasingly recursive: m4 is the
foundation autoconf-tested programs run their probes through.

Cross-validation target: Tiger's stock `/usr/bin/m4` (GNU m4 1.4.2,
shipped in Tiger).

## Work log

(filled in as the session progresses)

1. Surveyed: Tiger ships `/usr/bin/m4` and `/usr/bin/gm4` (same
   binary, GNU m4 1.4.2 from 2007). Good cross-validation target.
2. Downloaded m4-1.4.19 (2021-05-28, latest stable) on imacg3
   into `/Users/macuser/tmp/v093-m4-test/`.
3. Wrote trial build script `/Users/macuser/tmp/v093-m4-build.sh`:
   default CFLAGS, `CC="$TCC -B$TCCROOT -L$TCCROOT -I$TCCROOT/include"`,
   no Werror shim. Launched in background.
4. **Hit blocker #1** — `lib/c-stack.c:70: error: ';' expected (got
   'alternate_signal_stack')`. The token `max_align_t` was not a
   declared type at parse time. Configure had correctly detected
   `HAVE_MAX_ALIGN_T=0` (tcc's `<stddef.h>` only declares the
   typedef under `__STDC_VERSION__ >= 201112L`), so gnulib's
   `lib/stddef.h` wrapper should have defined `rpl_max_align_t` and
   `#define max_align_t rpl_max_align_t`. But the preprocessor never
   visited `lib/stddef.h` from within `c-stack.c` — tcc's own
   `stddef.h` was found first via the `-I$TCCROOT/include` flag
   baked into `CC`, which Make put **before** the per-directory `-I.`
   on the compile line. `-I` order matters in tcc (no `-isystem`-
   style demotion).
5. **Diagnosis** — tcc's include-path search is strict left-to-right
   over `-I` flags, then the `-B`-provided sysinclude as a fallback.
   The hardcoded `-I$TCCROOT/include` shadowed gnulib's wrapper
   stddef.h. Verified by preprocessing the same `<stddef.h>` include
   with `-I lib/` first vs. `-I$TCCROOT/include` first — the order
   determines which file wins.
6. **Fix #1** — drop the redundant `-I$TCCROOT/include` from the CC
   wrapper. tcc resolves its own sysinclude via `-B$TCCROOT` as a
   low-priority fallback, so user `-I` flags (e.g. gnulib's `-I.`)
   take precedence cleanly. Confirmed this still finds tcc's
   stddef.h for non-gnulib programs.
7. Retried with `CC="$TCC -B$TCCROOT -L$TCCROOT"` (no `-I`).
   Got past `c-stack.c`. Build progressed deep into lib/ (~30 files).
8. **Hit blocker #2** — `lib/sigsegv.c:938: error: field not found:
   __ss`. The bundled gnulib libsigsegv assumes modern macOS
   (>= 10.6?) PPC ucontext layout, accessing
   `((ucontext_t *) ucp)->uc_mcontext->__ss.__r1`. Tiger's PPC
   `<mach/ppc/_types.h>` uses **`ss.r1`** without the leading
   underscores. This is a Tiger-portability bug in gnulib's
   sigsegv.c, **not a tcc issue** — gcc-4.0 on Tiger would hit the
   same error. The error line is inside `#if HAVE_STACK_OVERFLOW_RECOVERY`.
9. **Pivot** — try m4-1.4.18 (2016) instead, which doesn't bundle
   `sigsegv.c` (smaller gnulib snapshot). Still has `c-stack.c` and
   `stddef.in.h` so the `-I` ordering fix is still required.
10. **m4-1.4.18 builds clean.** `src/m4` produced, 3.9 MB, links
    only `libSystem.B.dylib`, `m4 --version` reports 1.4.18.
11. **Smoke tests + cross-validation** — hand-rolled corpus of
    13 macro expansions (define / eval / ifelse / len / substr /
    translit / index / nested macro / recursion / divert) produced
    output **bit-identical** to Tiger's `/usr/bin/m4` 1.4.2.
12. **Bundled self-test** — m4 ships `checks/check-them`, a runner
    over 236 macro-expansion tests transcribed from the GNU m4
    manual (every documented macro/builtin has at least one
    example). All 236/236 PASS under tcc-built m4. Six changeword
    tests skipped (feature disabled by default — happens on every
    build, gcc or tcc).
13. **One spurious tcc error sighted** — during the first
    test-suite build, `test-xalloc-die` link emitted
    `test-xalloc-die.o: error: macho: symtab/strtab out of bounds`.
    The named .o file's symtab/strtab offsets check out against the
    file size (`stroff + strsize = 14252 + 111 = 14363 = file_size`).
    On manual rebuild, the link succeeded and the binary ran
    correctly. Couldn't reproduce; most likely a spurious / load-
    induced glitch on a heavily-loaded G3 with multiple makes
    interleaving. Documenting it in findings.md §5 in case it
    recurs in a future session.
14. **Demo authored** — [`demos/v0.2.67-m4.sh`](../../../demos/v0.2.67-m4.sh).
    Follows the v0.2.67-bc.sh template: download → configure (with
    the `-I` fix) → config.h sanity assertions → build → linkage
    check → 6 smoke tests with `/usr/bin/m4` cross-validation →
    the bundled 236-test manual-examples suite → PASS line.

## Exit state

- HEAD: post-session-093 docs commit.
- tcc source: **unchanged**. Pure demo + docs + workflow-finding session.
- tests2 unchanged (no source change to risk).
- bootstrap fixpoint unchanged.
- New demo: [`demos/v0.2.67-m4.sh`](../../../demos/v0.2.67-m4.sh).
- New findings: see [`findings.md`](findings.md) — 6 durable
  lessons covering tcc's include-search semantics, gnulib's
  Tiger-PPC gaps, the m4 self-test as a deep correctness signal,
  cross-validation patterns, intermittent-failure investigation
  discipline, and the m4-1.4.19 → 1.4.18 fall-back rationale.

Twelve real-world programs now build with tcc on Tiger PPC:
lua, zlib, bzip2, cJSON, sqlite, sed, gzip, Python, awk, expat,
bc, **m4**.
