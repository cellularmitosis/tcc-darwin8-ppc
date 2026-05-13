# Handoff — end of session 075 (2026-05-13)

## TL;DR

**v0.2.54-g3 lands the prerequisite for roadmap #7.5 Phase 2
item 1b** — `tcc -gstabs -c foo.c -o foo.o; tcc -gstabs foo.o
-o foo` now produces a gdb-debuggable Mach-O exe, matching what
the one-step `tcc -gstabs foo.c -o foo` flow has produced since
v0.2.51. The two-step compile-then-link flow real Makefiles use
is what unlocks gdb-on-Tiger for normal build systems, not just
single-source quick demos.

* **HEAD at session start:** `7c062ba` (end of session 074 /
  v0.2.53-g3).
* **HEAD at session end:** `c4c7733` (v0.2.54-g3, tagged + pushed).
* **Source changes:** `tcc/ppc-macho.c` only (+92 / -10 lines
  across 5 hunks).
* **Demo added:** [`demos/v0.2.54-stabs-roundtrip.{c,sh}`](../../../demos/)
  with [`v0.2.54-stabs-roundtrip-helpers.c`](../../../demos/v0.2.54-stabs-roundtrip-helpers.c)
  as the second TU.
* **tests2 111 / 111; abitest cc+tcc 24/24; bootstrap fixpoint
  holds.**

The session-074 HANDOFF had labelled item 1b as "session-sized."
Investigation upgraded that to "multi-session arc" — three
structural gaps to close before `dsymutil` can produce a
standalone `.dSYM`. This session lands the first (stabs round-trip
through `.o`); the remaining two (N_OSO schema + per-`.o`-path
threading, plus the dsymutil end-to-end demonstration) are
follow-up work.

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — five hunks:
  1. `classify_section` (dwarf_map[]): forward mappings for
     `.stab` → `__DWARF,__stab` and `.stabstr` → `__DWARF,__stabstr`.
  2. `macho_section_to_elf`: reverse mappings.
  3. `emit_section_relocs` (top gate): split the `!xlat_present`
     reject into a STT_SECTION-passthrough — they're not in the
     Mach-O nlist xlat table but the section number suffices.
  4. `macho_load_object_file` (new Pass 1.5): walk just-loaded
     `.stab` entries, add merged-`.stabstr` offset to each
     non-zero `n_strx`. Defensive `stab_section->link` /
     `sh_entsize` wiring on first encounter.
  5. SECTDIFF target-section search in `macho_translate_relocs`:
     skip `S_ATTR_DEBUG` sections. Fixes the bug where
     `__DWARF,__stab` at `addr=0` was incorrectly picked as the
     target for PIC data references to extern globals.
  6. `emit_stab_nlist`: skip placeholder Stab_Sym entries with
     `n_type == 0` (each loaded `.o` contributes its own leading
     empty entry; without this, multi-`.o` round-trips emit
     junk stabs).

### Files added

* [`demos/v0.2.54-stabs-roundtrip.c`](../../../demos/v0.2.54-stabs-roundtrip.c) (TU 1: main, references extern `g_seed` + extern functions)
* [`demos/v0.2.54-stabs-roundtrip-helpers.c`](../../../demos/v0.2.54-stabs-roundtrip-helpers.c) (TU 2: `g_seed` def + `helper_add` + `helper_mul`)
* [`demos/v0.2.54-stabs-roundtrip.sh`](../../../demos/v0.2.54-stabs-roundtrip.sh) (driver: 6-step verification)
* [`docs/sessions/075-n-oso-scope-2026-05-13/README.md`](README.md)
* This `HANDOFF.md`.

### Files updated (docs only)

* [`docs/roadmap.md`](../../roadmap.md):
  - v0.2.54-g3 row added at the top of the release table.
  - Header: "shipped through v0.2.53-g3" → "v0.2.54-g3";
    "Thirty-one" → "Thirty-two".
  - Structural item **#7.5** updated to credit v0.2.54 (Phase
    2 item 1b prerequisite landed).
* [`demos/README.md`](../../../demos/README.md): v0.2.54 row added
  above the v0.2.53 row (newest-first).

### Memory updates

None. The bug shaken out during implementation (SECTDIFF
target-section overlap with debug sections at `addr=0`) is now
documented in the session-075 README and is captured by the
v0.2.54 demo's PIC-extern-data assertion — no separate memory
entry needed.

## Status of session 074's open items

| # | Item | Status |
|---|---|---|
| (074 #1b) | Emit `N_OSO` pointing at real `.o` path | **prerequisite landed (this session); N_OSO emission itself unchanged** |
| (074 #1d) | Default `-g` to stabs on Darwin | unchanged |
| (074 #2 carried) | self-link diagnostics extension to `macho_output_dylib` (068 #2) | unchanged |
| (074 #2 carried) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (074 #2 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (074 #2 carried) | Sibling r11 watch (067 #3) | unchanged |
| (074 #2 carried) | AltiVec intrinsics | unchanged |
| (074 #2 carried) | bcheck.c port | unchanged |
| (074 #3) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more confirmed rebuild cycle) |
| (074 #4) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

### 1. `N_OSO` proper — roadmap #7.5 Phase 2 item 1b

The stab chain now round-trips through `.o`. The remaining
piece for `dsymutil exe -o exe.dSYM` to actually produce a
`.dSYM` bundle:

* Add `__define_stab (N_OSO, 0x66, "OSO")` to `tcc/stab.def`.
* Capture each input `.o`'s absolute path (`realpath` of
  `s1->current_filename` at load time) plus its
  `stat().st_mtime` in a new per-`.o` state array in
  `s1->macho_loaded_obj_state` (or similar — no struct exists yet).
* In `emit_stab_nlist`, walk the stab chain detecting per-`.o`
  boundaries. Either:
  - Watch for N_SO entries and emit one `N_OSO` before each
    "new TU" subrange. Risk: multiple `.o`s could come from
    the same source path, so N_SO alone isn't a reliable
    boundary marker.
  - More robust: at load time, record `(start_idx, end_idx,
    realpath, mtime)` ranges for each input `.o`'s
    contribution to `stab_section`. Walk those ranges at
    emit time.
* Emit `N_OSO` with `n_strx` = string offset of the `.o`'s
  realpath, `n_desc` = 1 (per Apple convention), `n_value` =
  mtime.
* For the direct `tcc -gstabs foo.c -o foo` flow (no intermediate
  `.o`): two options — either write a survivable temp `.o`
  (gcc-4.0's behavior — the `.o` lives in `/tmp/` long enough
  for `dsymutil` to read it), or accept dsymutil only works in
  the two-step flow. The two-step flow is more useful for real
  build systems anyway; temp-`.o` is nice-to-have.
* Verify end-to-end: `tcc -gstabs -c foo.c && tcc -gstabs foo.o
  -o foo && dsymutil foo -o foo.dSYM` produces a working
  bundle that Tiger gdb can find when given the exe path.

Likely a session of focused work given the load-time bookkeeping
and the dsymutil-on-Tiger verification dance.

### 2. `-g` default → stabs on Darwin (roadmap #7.5 Phase 2 1d)

Pre-v0.2.54, this was tabled with "probably best deferred until
1b's pipeline is robust." After v0.2.54, the stabs pipeline is
robust across both the one-step and two-step flows, with
cross-TU debugging working end-to-end. Flipping the `-g` default
is a small surgical change:

* `tcc/configure` — change `DEFAULT_DWARF_VERSION` (or wherever
  the Darwin `-g` default lives) so `-g` on Darwin sets
  `s->dwarf = 0` instead of 2.
* Release note explaining the change: Tiger gdb is the bundled
  debugger on the target OS; defaulting `-g` to stabs makes
  `tcc -g hello.c` produce a Tiger-debuggable binary out of the
  box.

One commit, one demo verifying the new default, one HANDOFF.

### 3. (carried) Multi-session arcs

Unchanged from session 074's handoff:

* (068 #2) Extend self-link diagnostics to `macho_output_dylib`.
* (068 #3) Post-write linter (`otool` / `nm` sanity over emitted
  bytes).
* (068 #4) `-vv` diagnostic for reader's silent dropped relocs.
* (067 #3) Sibling r11 watch (no surface yet).
* AltiVec intrinsics.
* bcheck.c port.

Each is its own multi-session arc; pick one with the user.

### 4. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after a few more
confirmed rebuild cycles on imacg3 — session 075 counts as one
more.

### 5. (optional) ibookg37 `--builtins+bitfields` generator-host validation

From session 071's handoff. Lower priority — the default-opts
sweep there already validated ibookg37 as a generator host.

## How to pick up

### Verify the v0.2.54 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.54-stabs-roundtrip.sh
```

Expected last line:
`OK: tcc -gstabs survives the .o round-trip; multi-TU gdb workflow holds.`

Also re-verify v0.2.53 (regression check):

```sh
bash demos/v0.2.53-bnsym-ensym.sh
```

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    cd tcc/tests && make abitest && cd ../.. && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0`, 48 abitest success,
`FIXPOINT HOLDS`.

### Tag + push status

v0.2.54-g3 was tagged and pushed at the end of this session
(`git tag v0.2.54-g3`; `git push origin main`; `git push origin
v0.2.54-g3`). No housekeeping remaining.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

Three sessions in one day for the stabs/gdb-on-Tiger thread:

* **072 (v0.2.51)**: STAB emission into LC_SYMTAB; gdb sees
  file:line, source listing, backtraces, `print <global>`.
* **073 (v0.2.52)**: PSYM/LSYM offset fixup; `print <local>` and
  `print <param>` show correct values across stepping.
* **074 (v0.2.53)**: BNSYM/ENSYM markers; format convention
  matches gcc-4.0.
* **075 (v0.2.54)**: stabs round-trip through `.o`; cross-TU
  debugging via the two-step compile-link flow.

After v0.2.54 the only remaining roadmap #7.5 work is `N_OSO` /
`dsymutil` (a substantive but discrete next step), plus the
configure flip to default `-g` to stabs on Darwin (a one-commit
UX win). The stabs pipeline is comprehensively working end-to-end
for real Tiger development workflows.

The interesting non-obvious finding from this session was the
SECTDIFF target-section overlap bug: the existing comment in
`macho_translate_relocs` warned about debug sections at `addr=0`
overlapping the SECTDIFF search range and broke the assumption
that only mapped-tcc-section status was enough to filter. Adding
the `__DWARF,__stab` mapping in this session was exactly the
case the warning anticipated. The fix (skip `S_ATTR_DEBUG`)
hardens the search against any future debug-section additions
to `macho_section_to_elf`.

Next session: [docs/sessions/075-n-oso-scope-2026-05-13/HANDOFF.md](docs/sessions/075-n-oso-scope-2026-05-13/HANDOFF.md)
