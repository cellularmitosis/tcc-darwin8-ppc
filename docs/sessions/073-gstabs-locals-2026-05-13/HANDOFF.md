# Handoff — end of session 073 (2026-05-13)

## TL;DR

**v0.2.52-g3 lands Phase 2 item 1a of roadmap #7.5** (gdb-on-Tiger
local-variable / parameter inspection). With v0.2.51 you could step
through tcc-built code under Tiger gdb 6.3 but `print x` returned
garbage from inside the function's own outgoing-param area; v0.2.52
fixes the stabs `N_PSYM` / `N_LSYM` offsets so `print <param>` and
`print <local>` read the correct values, including stepped values
and stack-frame argument display in `bt`.

* **HEAD at session start:** `64da2b8` (end of session 072 /
  v0.2.51-g3).
* **HEAD at session end:** this session's commit.
* **Source changes:** `tcc/tccdbg.c` (+27 lines in one function;
  one new `#if defined(TCC_TARGET_PPC) && defined(TCC_TARGET_MACHO)`
  branch).
* **Version bump:** v0.2.51-g3 → **v0.2.52-g3**.
* **Demo added:** [`demos/v0.2.52-gstabs-locals.{c,sh}`](../../../demos/).
* **tests2 111 / 111; abitest-cc 24/24; abitest-tcc 24/24;
  bootstrap fixpoint holds.**

## What landed

### Files edited

* [`tcc/tccdbg.c`](../../../tcc/tccdbg.c):
  - `tcc_debug_finish` non-DWARF branch: new local `emit_value`
    initialized from `s->value`, plus a Mach-O-PPC-only branch
    that adds `ppc_last_frame_size` to it for `N_PSYM` entries
    and for nonzero `N_LSYM` entries before `put_stabs` /
    `put_stabs_r`. The nonzero guard exempts type-def LSYMs
    (struct fields / typedefs, value==0); real PPC param/local
    offsets are never zero (params +24+, locals -12-).

### Files added

* [`demos/v0.2.52-gstabs-locals.c`](../../../demos/v0.2.52-gstabs-locals.c)
* [`demos/v0.2.52-gstabs-locals.sh`](../../../demos/v0.2.52-gstabs-locals.sh)
* [`docs/sessions/073-gstabs-locals-2026-05-13/README.md`](README.md)
* This `HANDOFF.md`.

### Files updated (docs only)

* [`docs/roadmap.md`](../../roadmap.md):
  - v0.2.52-g3 release row added at the top of the release table.
  - Header: "shipped through v0.2.50-g3" → "v0.2.52-g3";
    "twenty-nine patch releases" → "thirty".
  - Structural item **#7.5** updated to credit v0.2.52 alongside
    v0.2.51 (Phase 1 + Phase 2 1a both done).
  - "Risk register" entry for *No DWARF / no debugger* updated to
    credit v0.2.52.
* [`demos/README.md`](../../../demos/README.md):
  - v0.2.52 row added (above the v0.2.51 row, keeping the
    newest-first ordering).

### Memory updates

None this session. `reference_tiger_gdb_stabs.md` from session 072
already documents the empirical finding that Tiger gdb 6.3 reads
embedded stabs from `LC_SYMTAB` directly; this session refines that
to the more specific finding that **stabs offsets are r1-relative**,
but that lives in the session 073 README rather than memory because
it's already captured in the v0.2.52 release-row prose in roadmap.md
and the comment block in `tccdbg.c` at the call site.

## Status of session 072's open items

| # | Item | Status |
|---|---|---|
| (072 #1a) | Align tcc's `N_PSYM` / `N_LSYM` offsets with PPC frame layout | **landed (this session)** |
| (072 #1b) | Emit `N_OSO` pointing at real `.o` path | unchanged |
| (072 #1c) | Emit `N_BNSYM` / `N_ENSYM` markers | unchanged |
| (072 #1d) | Default `-g` to stabs on Darwin | unchanged |
| (072 #2 carried) | self-link diagnostics extension to `macho_output_dylib` (068 #2) | unchanged |
| (072 #2 carried) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (072 #2 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (072 #2 carried) | Sibling r11 watch (067 #3) | unchanged |
| (072 #2 carried) | AltiVec intrinsics | unchanged |
| (072 #2 carried) | bcheck.c port | unchanged |
| (072 #3) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more confirmed rebuild cycle) |
| (072 #4) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

### 1. Continue Phase 2 polish for roadmap #7.5

Pick from the three remaining sub-items. None is urgent — the
interactive-debug story is fully working now (file:line breakpoints,
source listing, stack walks with file:line, `print <global>`,
`print <param>`, `print <local>`).

#### 1b. Emit `N_OSO` pointing at a real `.o` file path

For the `tcc -gstabs -c foo.c && tcc -gstabs foo.o -o foo` flow,
threading the `.o`'s real path through the link sequence into the
linked exe's `N_OSO` entry would let `dsymutil exe -o exe.dSYM`
produce a standalone .dSYM bundle. The direct
`tcc -gstabs foo.c -o foo` flow has no intermediate .o on disk
— either write a temp .o for it (mirrors gcc-4.0's behavior),
or accept that dsymutil only works in the explicit two-step flow.
The two-step flow is the more useful answer for real build systems
that compile + link separately.

#### 1c. Emit `N_BNSYM` / `N_ENSYM` markers around function bodies

Apple-conventional, optional. Pre-v0.2.52 `break helper` resolved
to an address a few instructions past the function entry because
gdb's prolog-skip heuristic was guessing. With BNSYM/ENSYM markers
the body extent is explicit. Cosmetic but cheap.

#### 1d. Decide whether to default `-g` to stabs on Darwin

Currently `-g` defaults to DWARF-2 (set in v0.2.39's configure).
DWARF gives lldb + later-macOS step-debugging; stabs gives Tiger
gdb step-debugging. Since gdb is the bundled debugger on Tiger,
flipping the default to stabs would make `tcc -g hello.c` produce
a Tiger-debuggable binary out of the box — but it's a user-facing
behavior change worth doing on its own session and announcing.

### 2. (carried) Multi-session arcs

Unchanged from session 072's handoff:

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

### Verify the v0.2.52 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.52-gstabs-locals.sh
```

Expected last line:
`OK: tcc -gstabs PSYM/LSYM offsets read correctly under gdb.`

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0` + `FIXPOINT HOLDS`.

### Tag + push (if not done already this session)

If the session ended without pushing the v0.2.52 tag:

```sh
# from uranium
git tag v0.2.52-g3 -m "gstabs N_PSYM/N_LSYM stack offsets (roadmap #7.5 Phase 2 1a)"
git push origin main
git push origin v0.2.52-g3
```

(User has push authority granted in memory; this is a release-tag
push, so it's reasonable to do without per-action sign-off.)

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

A tight, single-fix session — the Phase 2 1a item the previous
session called out as highest-value turned out to be a one-spot
arithmetic shift once the empirical baseline made the offset
convention clear. The interesting non-obvious findings:

* **Tiger gdb 6.3 on PPC interprets stabs N_PSYM/N_LSYM offsets as
  r1-relative**, not r31-relative — i.e., gdb expects the offset
  to use the same base register as the current SP, which is gcc-4.0's
  convention. On PPC there's no architecturally-mandated frame
  pointer, so debuggers and ABIs pick their own. tcc emits to its
  own r31 (FP) convention internally; the fix is the arithmetic
  translation at stabs emission time.

* **Emit-time ordering matters and just barely works.**
  `gen_function`'s order is `tcc_debug_end_scope` (collects the
  PSYM/LSYM into `debug_info->sym`) → `gfunc_epilog` (computes
  `frame_size`, stashes `ppc_last_frame_size`) → `tcc_debug_funcend`
  → `tcc_debug_finish` (actually emits the put_stabs calls). The
  collect step happens with stale frame_size, but the emit step
  runs after gfunc_epilog so we have the right value at the only
  moment we need it. (If we had to know frame_size during collect,
  the fix would be more invasive — adding a target-specific
  callback exposing `ppc_frame_size()` to tccdbg.c. The current
  ordering is what makes the one-spot fix work.)

* **DWARF was already correct.** v0.2.39's per-prolog CFI defines
  CFA as `r1 + frame_size`, so `DW_OP_fbreg <s->value>` resolves
  to `r1 + frame_size + s->value` = `r31 + s->value` = the right
  stack slot. Only the stabs path needed the shift.

* **`bt` argument-value display was also gated on this fix.**
  Pre-v0.2.52 the backtrace listed `helper (x=-1881102064)` even
  though `print x` was the canonical garbage-read case; gdb
  evaluates frame argument values from the same N_PSYM offset, so
  fixing one fixed both. The v0.2.51 demo's `bt` now shows real
  values without any change to its script.

Next session: pick from the remaining Phase 2 polish items (1b OSO,
1c BNSYM/ENSYM, 1d default `-g` to stabs) or one of the carried
multi-session arcs.

Next session: [docs/sessions/073-gstabs-locals-2026-05-13/HANDOFF.md](docs/sessions/073-gstabs-locals-2026-05-13/HANDOFF.md)
