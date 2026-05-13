# Session 076 — `-g` defaults to stabs on Darwin (v0.2.55-g3)

**Date:** 2026-05-13
**Build hosts:** uranium ⇄ imacg3 (rsync + ssh)
**Arrival HEAD:** `ef7f70a` (end of session 075 / v0.2.54-g3 + handoff).
**Exit HEAD:** v0.2.55-g3 (this session).

## TL;DR

Plain `tcc -g foo.c` on PPC Darwin now produces a Tiger-debuggable
Mach-O out of the box. Pre-v0.2.55 it emitted DWARF-2, which Tiger's
bundled `gdb 6.3` only partially understands; the user had to
explicitly type `-gstabs` (or `-g0` + `-gstabs`) to get a debuggable
binary. After this session, bare `-g` → stabs (= `s->dwarf = 0`), and
DWARF is opt-in via the explicit `-gdwarf-N` flag.

This is roadmap **#7.5 Phase 2 item 1d** — the UX wrap-up to the
stabs/gdb-on-Tiger arc that ran 072 → 073 → 074 → 075. Stabs is now
the right default because:

* `gdb 6.3` (bundled with Xcode 2.5 on Tiger) reads classic stabs
  natively, no `.dSYM` step required.
* The tcc stabs pipeline is robust end-to-end across both the
  one-step `tcc -g foo.c -o foo` and two-step `tcc -g -c foo.c; tcc
  -g foo.o -o foo` flows (v0.2.51 + v0.2.54).
* Stack offsets, body markers, cross-TU debugging — all work
  (v0.2.52, v0.2.53, v0.2.54).
* DWARF embedded in Mach-O exes is not directly read by gdb 6.3
  anyway (it expects `OSO` STAB chain + `.o` companion files, or a
  `.dSYM` bundle); DWARF tooling on Tiger is `dwarfdump`, which the
  explicit `-gdwarf-2` flag still serves.

## Change

One line, plus a refreshed comment:

```diff
--- a/tcc/configure
+++ b/tcc/configure
@@ -355,12 +355,17 @@ esac

 case $targetos in
   Darwin)
-    # Tiger's bundled dwarfdump / gdb 6.3 fully understand DWARF-2 and
-    # only partially DWARF-4 (e.g. Unknown DW_FORM_sec_offset = 0x17),
-    # so default plain `-g` to DWARF-2 for PPC Darwin tooling
-    # compatibility. Users can still request newer with -gdwarf-4 etc.
-    confvars_set OSX dwarf=2
+    # On PPC Darwin / Tiger the bundled debugger is gdb 6.3, which reads
+    # classic stabs natively (no `.dSYM` round-trip required) but only
+    # partially understands embedded DWARF. The tcc stabs pipeline has
+    # been robust end-to-end across both `tcc -g foo.c` and the
+    # `tcc -g -c foo.c; tcc -g foo.o` two-step flow since v0.2.54, so
+    # default plain `-g` to stabs (= 0). Users wanting DWARF can still
+    # ask for it explicitly: `-gdwarf-2` (Tiger dwarfdump-compatible)
+    # or `-gdwarf-4`. `DEFAULT_DWARF_VERSION` (= 2 on Darwin, in
+    # tcc.h) is still what bare `-gdwarf` resolves to.
+    confvars_set OSX dwarf=0
```

That's it. The `confvars_set OSX dwarf=N` line populates
`CONFIG_DWARF_VERSION` via `tcc/configure:705` (`CONFIG_dwarf=*) print_num
CONFIG_DWARF_VERSION ${R#*=}`). `libtcc.c:2025` reads
`s->dwarf = CONFIG_DWARF_VERSION` on plain `-g`. Setting it to 0
means stabs.

`-gdwarf` (no version) still uses `DEFAULT_DWARF_VERSION` (= 2 on
Darwin, in `tcc/tcc.h:1933`) so that hasn't changed. Explicit
`-gdwarf-2` / `-gdwarf-4` still resolve to their requested version.

## Verification plan

* Build tcc-darwin8-ppc on imacg3 with the new configure.
* Demo `v0.2.55-g-default-stabs.{c,sh}`: build a small program with
  bare `tcc -g hello.c -o hello` (no `-gstabs`), inspect
  `LC_SYMTAB` for stab entries, script `gdb` to break on `main`,
  print a local, walk the backtrace.
* Regression: tests2 (111/111), abitest cc+tcc (24/24 each),
  bootstrap fixpoint.
* Sanity check that `-gdwarf-2` still emits DWARF-2 (regression
  against v0.2.39's `-gdwarf-2` path).

## Exit state

* **Source change:** 1 file, +9/-4 lines (`tcc/configure` only — comment
  rewritten + one `dwarf=2` → `dwarf=0`).
* **Demo added:** [`demos/v0.2.55-g-default-stabs.c`](../../../demos/v0.2.55-g-default-stabs.c) + [`v0.2.55-g-default-stabs.sh`](../../../demos/v0.2.55-g-default-stabs.sh).
* **Docs updated:**
  - [`docs/roadmap.md`](../../roadmap.md) — v0.2.55-g3 row prepended to release
    table; header "shipped through v0.2.54-g3" → "v0.2.55-g3";
    "Thirty-two" → "Thirty-three"; #7.5 status line updated to credit
    v0.2.55 with closing Phase 2 item 1d.
  - [`demos/README.md`](../../../demos/README.md) — v0.2.55 row added above
    the v0.2.54 row (newest-first).
* **Verification on imacg3 (Tiger 10.4.11 PPC G3):**
  - Clean re-configure (`rm config.mak config.h`) then
    `scripts/build-tiger.sh` — succeeds; `Config` line now reads
    `… OSX dwarf=0 codesign …` (was `dwarf=2`).
  - `demos/v0.2.55-g-default-stabs.sh` — green end-to-end. Bare-`-g`
    build has stabs entries (SO=6, FUN=8, SLINE=19, BNSYM=4, ENSYM=4)
    + `__DWARF,__stab` + no `__debug_info`; explicit `-gdwarf-2` build
    has `__debug_info` + no stabs; gdb workflow on bare-`-g`:
    `compute(x=3,y=4)` → step → `sum=7` → step → `prod=12`, `g_seed=7`,
    backtrace shows file:line for both frames.
  - `scripts/run-tests2.sh` — **111 / 111**.
  - `tcc/tests && make abitest` — **48 success** (24 cc + 24 tcc).
  - `FIXPOINT=1 scripts/bootstrap-tcc-self.sh` — **FIXPOINT HOLDS**
    (tcc-self2.o == tcc-self3.o).
  - `demos/v0.2.54-stabs-roundtrip.sh` — still green (regression
    check that the previous arc's two-step flow keeps working).

The discriminator between "stabs build" and "DWARF build" in the demo
needed a one-line fix during authoring: v0.2.54 moved `.stab` /
`.stabstr` INTO the `__DWARF` segment (alongside `__debug_*`), so the
presence of a `__DWARF` segment is no longer a "this build is DWARF"
signal. The discriminator is what sections live inside it —
`__stab` (stabs build) vs `__debug_info` (DWARF build). Captured in
the demo's comment block for future cargo-cult avoidance.

## Roadmap status

After v0.2.55, the **#7.5 (OSO STAB / gdb-on-Tiger)** roadmap item is
done across Phase 1 (v0.2.51), Phase 2 items 1a (v0.2.52), 1c
(v0.2.53), the 1b prerequisite (v0.2.54), and 1d (this session).
The only remaining work tracked under #7.5 is the actual `N_OSO` +
`dsymutil` end-to-end work (the headline #1b item itself) — a
substantive but discrete arc on top of v0.2.54's stab round-trip
chain. Tracked for a follow-up session.

