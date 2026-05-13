# Handoff — end of session 072 (2026-05-13)

## TL;DR

**v0.2.51-g3 lands Phase 1 of roadmap #7.5** (OSO STAB emission for
gdb-on-Tiger). `tcc -gstabs -o exe file.c` on Tiger PPC now produces
a Mach-O whose `LC_SYMTAB` carries the classic stabs+ chain Apple's
`gdb 6.3` reads directly — file:line breakpoints, source listing,
backtraces with file:line per frame, and `print <global>` all work
end-to-end. Pre-v0.2.51 the `-gstabs` flag was accepted but the
Mach-O writer silently dropped `stab_section`.

* **HEAD at session start:** `187d6cf` (end of session 071).
* **HEAD at session end:** this session's commit.
* **Source changes:** `tcc/ppc-macho.c` (+helper, +2 call sites),
  `tcc/tccdbg.c` (+3 `#ifdef TCC_TARGET_MACHO` branches).
* **Version bump:** v0.2.50-g3 → **v0.2.51-g3**.
* **Demo added:** [`demos/v0.2.51-gstabs-oso-stab.{c,sh}`](../../../demos/).
* **tests2 111 / 111; abitest-cc 24/24; abitest-tcc 24/24;
  bootstrap fixpoint holds.**

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c):
  - New `emit_stab_nlist()` helper (just below `put_nlist`).
  - Wired into `macho_output_exe` (line ~2660) and
    `macho_output_dylib` (line ~4257) just after the strtab init,
    contributing to `n_localsym`.
* [`tcc/tccdbg.c`](../../../tcc/tccdbg.c):
  - `tcc_debug_line`: N_SLINE inside a function now uses
    `put_stabs_r` with absolute `ind` on Mach-O (Apple convention),
    keeps GNU function-relative offset elsewhere.
  - `tcc_debug_finish`: N_LBRAC / N_RBRAC similarly switch to
    absolute `func_ind + cur->{start,end}` with reloc on Mach-O.
  - `tcc_debug_funcend`: emit a paired `N_FUN` end-entry with
    empty name and `n_value = size` after `tcc_debug_finish`
    on Mach-O.

### Files added

* [`demos/v0.2.51-gstabs-oso-stab.c`](../../../demos/v0.2.51-gstabs-oso-stab.c)
* [`demos/v0.2.51-gstabs-oso-stab.sh`](../../../demos/v0.2.51-gstabs-oso-stab.sh)
* [`docs/sessions/072-oso-stab-gdb-tiger-2026-05-13/README.md`](README.md)
* This `HANDOFF.md`.

### Files updated (docs only)

* [`docs/roadmap.md`](../../roadmap.md):
  - v0.2.51-g3 release row added at the top of the release table.
  - Structural item **#7.5** ticked off (was the next unticked
    item before this session).
  - "Risk register" note about gdb-on-Tiger expanded to credit
    v0.2.51.
* [`demos/README.md`](../../../demos/README.md):
  - v0.2.51 row added after v0.2.50.

### Memory updates

* New: [`reference_tiger_gdb_stabs.md`](../../../../.claude/projects/-Users-cell-claude-tcc-darwin8-ppc/memory/reference_tiger_gdb_stabs.md)
  — empirical finding that Tiger gdb 6.3 reads classic stabs+ from
  the linked exe's nlist directly (no OSO / `.o` / `.dSYM`
  required). Index entry added to `MEMORY.md`.
* Updated: [`host_ibookg37.md`](../../../../.claude/projects/-Users-cell-claude-tcc-darwin8-ppc/memory/host_ibookg37.md)
  — added a "Hardware state (as of 2026-05-13)" section noting that
  the spinning disk may be on final days and the user has an mSATA
  adapter on hand for the eventual upgrade. Implication: don't lean
  on the host for long-running irreplaceable state.

## Status of session 071's open items

| # | Item | Status |
|---|---|---|
| (071 #1, sub) | OSO STAB / gdb-on-Tiger | **landed (this session)** |
| (071 #1, sub) | self-link diagnostics extension to `macho_output_dylib` (068 #2) | unchanged |
| (071 #1, sub) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (071 #1, sub) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (071 #1, sub) | Sibling r11 watch (067 #3) | unchanged |
| (071 #1, sub) | AltiVec intrinsics | unchanged |
| (071 #1, sub) | bcheck.c port | unchanged |
| (071 #2) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (071 #3) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

### 1. v0.2.51 Phase 2: polish OSO STAB to make `dsymutil` + locals work

The Phase 1 work above is what was strictly required for the
interactive-debug case; three follow-ups would round it out:

#### 1a. Align tcc's `N_PSYM` / `N_LSYM` stack offsets with the PPC
backend's actual frame layout.

Right now `print x` inside `helper(int x)` shows
`x = 1048672` (or similar garbage) because the offset emitted in
N_PSYM doesn't match where the PPC backend stores the parameter
in the frame. Look at `tccgen.c`'s param/local layout vs
`tccdbg.c`'s offset arg to `tcc_debug_stabs` (which goes into
`Stab_Sym.n_value`). On PPC the standard 24-byte linkage area
sits at SP+0..23 and the first param spills to SP+24 (0x18) —
that's what tcc emits — but gdb may interpret offsets relative to
*FP* (which on PPC isn't strictly defined; r1 is SP). Probable
fix: emit offsets relative to whatever register the function's
prolog uses as its de-facto frame base, or emit
register-class (`R`) entries for params live in r3..r10 before
they get spilled.

This is the highest-value Phase 2 item — without it, the
interactive debugging story is "you can step, but you can't see
your values."

#### 1b. Emit `N_OSO` pointing at a real `.o` file path.

Currently the N_OSO STAB is empty (gcc-4.0 does the same on
direct .c→exe compile, and Tiger gdb is fine with it). Adding a
real path lets `dsymutil exe -o exe.dSYM` produce a standalone
.dSYM bundle — the standard Apple workflow for distributing
stripped binaries + separate debug info.

The tricky bit: for `tcc -gstabs foo.c -o foo` there's no
intermediate .o on disk. Options:
* Force tcc to write a temporary `.o` (in `/tmp/` or alongside the
  exe) when `-gstabs` is active and the source flow goes from .c
  directly to exe.
* Or accept that dsymutil only works in the explicit
  `tcc -gstabs -c foo.c && tcc -gstabs foo.o -o foo` flow.

The `.o`-then-link path is more useful in practice — it's what a
real build system would do. Empty-OSO support for the direct flow
+ real-OSO for the explicit-.o flow is probably the right answer.

#### 1c. Emit `N_BNSYM` / `N_ENSYM` markers around function bodies.

Apple-conventional, optional. Helps gdb's prolog-skip logic find
the body more accurately — currently `break helper` resolves to
an address a few instructions past the function entry (gdb's
heuristic). Cosmetic, but cheap to add.

#### 1d. Decide whether to default `-g` to stabs on Darwin.

Currently `-g` defaults to DWARF-2 (set in v0.2.39's configure).
DWARF gives lldb + later-macOS step-debugging; stabs gives Tiger
gdb step-debugging. Since gdb is the bundled debugger on Tiger,
flipping the default to stabs would make `tcc -g hello.c` produce
a Tiger-debuggable binary out of the box. But it's a user-facing
behavior change worth doing on its own session and announcing.

### 2. (carried) Multi-session arcs

Unchanged from session 071's handoff:

* (068 #2) Extend self-link diagnostics to `macho_output_dylib`.
* (068 #3) Post-write linter (`otool` / `nm` sanity over emitted
  bytes).
* (068 #4) `-vv` diagnostic for reader's silent dropped relocs.
* (067 #3) Sibling r11 watch (no surface yet).
* AltiVec intrinsics.
* bcheck.c port.

Each is its own multi-session arc; pick one with the user.

### 3. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

The pre-069 imacg3 tree backup. Inherited from session 069. Safe
to drop after a few more confirmed rebuild cycles on imacg3 — this
session counts as one of them.

### 4. (optional) ibookg37 `--builtins+bitfields` generator-host validation

From session 071's handoff. Lower priority — the
default-opts sweep there already validated ibookg37 as a
generator host.

## How to pick up

### Verify the v0.2.51 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.51-gstabs-oso-stab.sh
```

Expected last line:
`OK: tcc -gstabs produces gdb-debuggable Mach-O on Tiger.`

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0` + `FIXPOINT HOLDS`.

### Tag + push (if not done already this session)

If the session ended without pushing the v0.2.51 tag:

```sh
# from uranium
git tag v0.2.51-g3 -m "OSO STAB emission for gdb-on-Tiger (Phase 1)"
git push origin main
git push origin v0.2.51-g3
```

(User has push authority granted in memory; this is a
release-tag push, so it's reasonable to do without per-action
sign-off.)

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

This was a Phase-1 session: the core capability (interactive gdb on
Tiger) lands cleanly, and the polish items (locals, OSO, default
behavior) are well-scoped follow-ups. The interesting non-obvious
findings:

* **Tiger gdb 6.3 doesn't require external .o files for stabs.**
  Apple's debug-map convention *can* point at external .o via
  N_OSO, but gdb is happy reading the full stabs+ chain directly
  from the linked exe's nlist (which is what gcc-4.0 does when
  there's no intermediate .o on disk anyway). This drastically
  simplified Phase 1 — we didn't have to thread .o paths through
  the linker.
* **The gap was a Mach-O-writer-drops-stab_section, not a missing
  emission.** tcc's existing `-gstabs` infrastructure already
  generated correct STAB entries; they just needed to make it into
  `LC_SYMTAB`. The translation pass is ~90 lines.
* **GNU stabs ≠ Apple stabs** at the n_value-encoding level.
  GNU's stab convention uses function-relative offsets for
  N_SLINE / N_LBRAC / N_RBRAC inside a function; Apple uses
  absolute text VAs (with reloc) throughout. Same for the paired
  `N_FUN` end-entry — Apple emits an explicit size entry, GNU
  infers from the next FUN. The two are interchangeable in
  principle but each debugger only reads its own convention, so
  `#ifdef TCC_TARGET_MACHO` branches in tccdbg.c are the cleanest
  way to support both.

Next session: pick from the Phase 2 polish items (most useful:
local-variable offsets so `print x` works) or one of the carried
multi-session arcs.

Next session: [docs/sessions/072-oso-stab-gdb-tiger-2026-05-13/HANDOFF.md](docs/sessions/072-oso-stab-gdb-tiger-2026-05-13/HANDOFF.md)
