# Handoff — end of session 074 (2026-05-13)

## TL;DR

**v0.2.53-g3 lands Phase 2 item 1c of roadmap #7.5** —
Apple-conventional `N_BNSYM` (0x2e) / `N_ENSYM` (0x4e) markers
emitted around every function body's `N_FUN` entry when
`tcc -gstabs` targets Mach-O.

* **HEAD at session start:** `b2febb0` (end of session 073 /
  v0.2.52-g3).
* **HEAD at session end:** this session's commit.
* **Source changes:** `tcc/stab.def` (+10 lines: two
  `__define_stab` entries + comments) and `tcc/tccdbg.c`
  (+19 lines: BNSYM emission in `tcc_debug_funcstart`, ENSYM
  emission in `tcc_debug_funcend`, both `#ifdef TCC_TARGET_MACHO`).
* **Version bump:** v0.2.52-g3 → **v0.2.53-g3**.
* **Demo added:** [`demos/v0.2.53-bnsym-ensym.{c,sh}`](../../../demos/).
* **tests2 111 / 111; abitest cc+tcc 24/24; bootstrap fixpoint
  holds.**

The A/B comparison against v0.2.52 found that **Tiger gdb 6.3
itself shows no observable behavior change** — `break <fn>`,
`info line <fn>`, `info function <fn>`, `info address <fn>`, and
function disassembly are byte-identical with vs without
BNSYM/ENSYM. The session-073 HANDOFF anticipated a prolog-skip
improvement; empirically gdb 6.3's existing SLINE-based heuristic
already gets `break helper` right on PPC. So 1c is
convention/correctness-level work, with the real payoff being
infrastructure for Phase 2 item 1b (`dsymutil` walks the
BNSYM/ENSYM-bracketed stab subrange when building `.dSYM` bundles).

## What landed

### Files edited

* [`tcc/stab.def`](../../../tcc/stab.def):
  - `__define_stab (N_BNSYM, 0x2e, "BNSYM")` between `N_MAIN` and
    `N_PC`.
  - `__define_stab (N_ENSYM, 0x4e, "ENSYM")` between `N_DEFD` and
    `N_EHDECL`.
  - Comments cite Apple's `<mach-o/stab.h>` format spec
    (`0,,n_sect,0,address`).

* [`tcc/tccdbg.c`](../../../tcc/tccdbg.c):
  - `tcc_debug_funcstart`, non-DWARF branch: emit `N_BNSYM` before
    the opening `N_FUN`, value=`func_ind`, reloc'd against
    `text_section`/`section_sym`. Inside `#ifdef TCC_TARGET_MACHO`.
  - `tcc_debug_funcend`, Mach-O branch: emit `N_ENSYM` after the
    closing `N_FUN` size-entry, value=`func_ind + size`, same
    reloc target. Inside the existing `#ifdef TCC_TARGET_MACHO`
    block.

### Files added

* [`demos/v0.2.53-bnsym-ensym.c`](../../../demos/v0.2.53-bnsym-ensym.c)
* [`demos/v0.2.53-bnsym-ensym.sh`](../../../demos/v0.2.53-bnsym-ensym.sh)
* [`docs/sessions/074-bnsym-ensym-2026-05-13/README.md`](README.md)
* This `HANDOFF.md`.

### Files updated (docs only)

* [`docs/roadmap.md`](../../roadmap.md):
  - v0.2.53-g3 release row added at the top of the release table.
  - Header: "shipped through v0.2.52-g3" → "v0.2.53-g3";
    "Thirty patch releases" → "Thirty-one".
  - Structural item **#7.5** updated to credit v0.2.53 (Phase 1 +
    Phase 2 items 1a and 1c all done).
  - Risk-register *No DWARF / no debugger* entry updated to credit
    v0.2.53.
* [`demos/README.md`](../../../demos/README.md): v0.2.53 row added
  above the v0.2.52 row (newest-first).

### Memory updates

None this session. `reference_tiger_gdb_stabs.md` from session 072
remains the durable note on Tiger gdb 6.3's stabs handling; this
session's finding (BNSYM/ENSYM emission is invisible to gdb 6.3's
SLINE-based prolog skip) lives in the session 074 README rather
than memory — it's already captured in the v0.2.53 release-row
prose in roadmap.md.

## Status of session 073's open items

| # | Item | Status |
|---|---|---|
| (073 #1b) | Emit `N_OSO` pointing at real `.o` path | unchanged |
| (073 #1c) | Emit `N_BNSYM` / `N_ENSYM` markers around function bodies | **landed (this session)** |
| (073 #1d) | Default `-g` to stabs on Darwin | unchanged |
| (073 #2 carried) | self-link diagnostics extension to `macho_output_dylib` (068 #2) | unchanged |
| (073 #2 carried) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (073 #2 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (073 #2 carried) | Sibling r11 watch (067 #3) | unchanged |
| (073 #2 carried) | AltiVec intrinsics | unchanged |
| (073 #2 carried) | bcheck.c port | unchanged |
| (073 #3) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more confirmed rebuild cycle) |
| (073 #4) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

### 1. Continue Phase 2 polish for roadmap #7.5

Two of the three Phase 2 sub-items remain. **1b is by far the
highest-value remaining item** — it unlocks standalone `.dSYM`
bundles via `dsymutil`.

#### 1b. Emit `N_OSO` pointing at a real `.o` file path

For the `tcc -gstabs -c foo.c && tcc -gstabs foo.o -o foo` flow,
threading the `.o`'s real on-disk path through the link sequence
into the linked exe's `N_OSO` entry would let
`dsymutil exe -o exe.dSYM` produce a standalone .dSYM bundle.

`dsymutil` walks the binary's stab chain, reads each `N_OSO` to
find the source `.o`, opens it, walks ITS stab chain, and lifts
the debug entries into a debug-map .dSYM. The current
`emit_stab_nlist` writes an `N_OSO` with empty path (acceptable
for the gdb-on-Tiger interactive flow that v0.2.51 unlocked, since
gdb reads everything from the linked exe's LC_SYMTAB directly).
For dsymutil, the path must point at a `.o` that actually exists
at link time AND at dsymutil run time.

Approach sketch:
* Capture each input `.o`'s absolute path in the link state.
* Per-section / per-stab range, decide which input `.o` it came
  from (probably via the source-file `N_SO` already in the chain).
* Emit one `N_OSO` per input `.o` with the captured path and the
  `.o`'s `st_mtime` (so dsymutil knows the .o hasn't drifted since
  link).
* Handle the BNSYM/ENSYM-bracketed function ranges correctly —
  dsymutil expects each function's stab subrange to be scoped
  inside an OSO subrange.

The direct `tcc -gstabs foo.c -o foo` flow has no intermediate
.o on disk — either write a temp .o for it (mirrors gcc-4.0's
behavior; the `.o` survives in `/tmp/` long enough for dsymutil
to read it), or accept that dsymutil only works in the explicit
two-step flow. The two-step flow is the more useful answer for
real build systems.

This is a session-sized chunk — likely most of a focused day —
and the most natural follow-up to today's work.

#### 1d. Decide whether to default `-g` to stabs on Darwin

Currently `-g` defaults to DWARF-2 (set in v0.2.39's configure).
DWARF gives lldb + later-macOS step-debugging; stabs gives Tiger
gdb step-debugging. Since gdb is the bundled debugger on Tiger,
flipping the default to stabs would make `tcc -g hello.c` produce
a Tiger-debuggable binary out of the box — but it's a user-facing
behavior change worth doing on its own session and announcing,
and only after we're confident the stabs pipeline is robust.
Probably best deferred until after 1b lands.

### 2. (carried) Multi-session arcs

Unchanged from session 073's handoff:

* (068 #2) Extend self-link diagnostics to `macho_output_dylib`.
* (068 #3) Post-write linter (`otool` / `nm` sanity over emitted
  bytes).
* (068 #4) `-vv` diagnostic for reader's silent dropped relocs.
* (067 #3) Sibling r11 watch (no surface yet).
* AltiVec intrinsics.
* bcheck.c port.

Each is its own multi-session arc; pick one with the user.

### 3. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after a few more confirmed
rebuild cycles on imacg3 — this session counts as one of them.

### 4. (optional) ibookg37 `--builtins+bitfields` generator-host validation

From session 071's handoff. Lower priority — the default-opts
sweep there already validated ibookg37 as a generator host.

## How to pick up

### Verify the v0.2.53 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.53-bnsym-ensym.sh
```

Expected last line:
`OK: tcc -gstabs emits paired BNSYM/ENSYM around every function.`

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0` + `FIXPOINT HOLDS`.

### Tag + push (if not done already this session)

If the session ended without pushing the v0.2.53 tag:

```sh
# from uranium
git tag v0.2.53-g3 -m "gstabs N_BNSYM/N_ENSYM body markers (roadmap #7.5 Phase 2 1c)"
git push origin main
git push origin v0.2.53-g3
```

(User has push authority granted in memory; this is a release-tag
push, so it's reasonable to do without per-action sign-off.)

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

A small, surgical change — 2 stab.def entries + 2 emission points
+ a demo — that brought tcc's stabs output format into alignment
with gcc-4.0 on Tiger PPC. The interesting non-obvious finding
from the empirical A/B was that **Tiger gdb 6.3 doesn't need
BNSYM/ENSYM** — its SLINE-based prolog skip already lands
`break helper` at the body's first source line. So we shipped 1c
on the basis of format-correctness and the upcoming dsymutil/N_OSO
work (1b) rather than an observable Tiger-gdb fix.

The honest reframing in the session README / demo is that this
release's value is structural: the stab chain now matches what
Apple's tools expect to walk. Real payoff comes when 1b lands
and dsymutil starts actually producing .dSYM bundles from tcc-
linked exes.

Next session: [docs/sessions/074-bnsym-ensym-2026-05-13/HANDOFF.md](docs/sessions/074-bnsym-ensym-2026-05-13/HANDOFF.md)
