# Handoff — end of session 076 (2026-05-13)

## TL;DR

**v0.2.55-g3 flips the `-g` default on PPC Darwin from DWARF-2 to
stabs** (roadmap #7.5 Phase 2 item 1d). After this, bare
`tcc -g hello.c -o hello && gdb hello` Just Works on Tiger out of
the box — no need to type `-gstabs` to get a Tiger-debuggable
binary. DWARF is still reachable via the explicit `-gdwarf-2` /
`-gdwarf-4` flags.

* **HEAD at session start:** `ef7f70a` (end of session 075 / v0.2.54-g3 + handoff).
* **HEAD at session end:** `69a8637` (v0.2.55-g3, tagged + pushed).
* **Source changes:** `tcc/configure` only (+9 / -4 lines, one
  setting flipped + comment rewritten).
* **Demo added:** [`demos/v0.2.55-g-default-stabs.{c,sh}`](../../../demos/).
* **tests2 111 / 111; abitest cc+tcc 24/24; bootstrap fixpoint
  holds; v0.2.54 cross-TU regression demo still green.**

Three sessions in one day on the stabs/gdb-on-Tiger arc (072→073→
074→075→076 spread across two days):

* **072 (v0.2.51)**: STAB emission into LC_SYMTAB — `gdb` sees
  file:line, source listing, backtraces, `print <global>`.
* **073 (v0.2.52)**: PSYM/LSYM offset fixup — `print <param>` /
  `print <local>` show correct values across stepping.
* **074 (v0.2.53)**: BNSYM/ENSYM body markers — format matches
  gcc-4.0.
* **075 (v0.2.54)**: stabs round-trip through `.o` — cross-TU
  debugging via the two-step compile-link flow.
* **076 (v0.2.55, this session)**: bare `-g` defaults to stabs —
  UX wrap-up.

The only remaining roadmap #7.5 work is the actual N_OSO +
`dsymutil` end-to-end work (substantive but discrete next step).
Phases 1, 1a, 1b-prerequisite, 1c, and 1d are now all landed.

## What landed

### Files edited

* [`tcc/configure`](../../../tcc/configure) — lines 357-367:
  `confvars_set OSX dwarf=2` → `confvars_set OSX dwarf=0`, plus
  comment rewritten to explain the rationale (Tiger `gdb 6.3` is
  a native stabs reader; embedded DWARF in Mach-O only partially
  understood by it; v0.2.54 made the stabs pipeline robust across
  both compile-link flows; explicit `-gdwarf-N` still works).

That's the whole patch. Threading: `tcc/configure:705`
(`CONFIG_dwarf=*` → `print_num CONFIG_DWARF_VERSION ${R#*=}`) writes
`#define CONFIG_DWARF_VERSION 0` into the generated `config.h`;
`tcc/libtcc.c:2025` reads `s->dwarf = CONFIG_DWARF_VERSION` on the
`-g` option entry point. Result: bare `-g` → `s->dwarf = 0` (stabs).

`DEFAULT_DWARF_VERSION` (`tcc/tcc.h:1933`, = 2 on Darwin) is
untouched — it's what bare `-gdwarf` (no version suffix) resolves
to, separate from `CONFIG_DWARF_VERSION`.

### Files added

* [`demos/v0.2.55-g-default-stabs.c`](../../../demos/v0.2.55-g-default-stabs.c) — tiny program (global, function with params + locals, printf).
* [`demos/v0.2.55-g-default-stabs.sh`](../../../demos/v0.2.55-g-default-stabs.sh) — driver:
  1. Build with bare `-g` (NO `-gstabs`); assert `__DWARF,__stab`
     section + full SO/FUN/SLINE/BNSYM/ENSYM chain in `LC_SYMTAB`;
     assert NO `__debug_info` section.
  2. Build same source with explicit `-gdwarf-2`; assert
     `__debug_info` present, stabs entries absent — regression
     check that DWARF is still reachable when asked for explicitly.
  3. Smoke-run both exes.
  4. Script `gdb -batch` on the bare-`-g` build: break in
     `compute`, print params, step locals, print global, walk bt,
     assert file:line.
* [`docs/sessions/076-g-default-stabs-2026-05-13/README.md`](README.md)
* This `HANDOFF.md`.

### Files updated (docs only)

* [`docs/roadmap.md`](../../roadmap.md): v0.2.55-g3 row prepended;
  header bumped ("v0.2.54-g3" → "v0.2.55-g3", "Thirty-two" →
  "Thirty-three"); #7.5 status updated to credit v0.2.55 with
  closing Phase 2 item 1d.
* [`demos/README.md`](../../../demos/README.md): v0.2.55 row added
  above v0.2.54 (newest-first).

### Memory updates

None. The one non-obvious finding (`v0.2.54` moved `.stab` into the
`__DWARF` segment, so "has a `__DWARF` segment" is no longer a
DWARF-vs-stabs discriminator) is captured in the demo's comment
block and in the session README — codebase-derivable from
`tcc/ppc-macho.c::classify_section`, so no memory entry needed.

## Status of session 075's open items

| # | Item | Status |
|---|---|---|
| (075 #1) | `N_OSO` proper — Phase 2 item 1b | **unchanged** — substantive follow-up, see below |
| (075 #2) | `-g` default → stabs on Darwin (Phase 2 1d) | **landed this session (v0.2.55)** |
| (075 #3 carried) | self-link diagnostics → `macho_output_dylib` (068 #2) | unchanged |
| (075 #3 carried) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (075 #3 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (075 #3 carried) | Sibling r11 watch (067 #3) | unchanged |
| (075 #3 carried) | AltiVec intrinsics | unchanged |
| (075 #3 carried) | bcheck.c port | unchanged |
| (075 #4) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more confirmed rebuild cycle) |
| (075 #5) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

### 1. `N_OSO` + `dsymutil` end-to-end — roadmap #7.5 Phase 2 item 1b

The stab chain now round-trips through `.o`; bare `-g` produces
gdb-debuggable Mach-O by default. The remaining piece for
`dsymutil exe -o exe.dSYM` to actually produce a standalone `.dSYM`
bundle is the `N_OSO` records that point at the per-`.o`-file paths
(with `st_mtime`) so dsymutil can find the companion objects.

This is unchanged from session 075's handoff item #1 — repeated
verbatim for the next session's convenience:

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
* For the direct `tcc -g foo.c -o foo` flow (no intermediate
  `.o`): two options — either write a survivable temp `.o`
  (gcc-4.0's behavior — the `.o` lives in `/tmp/` long enough
  for `dsymutil` to read it), or accept dsymutil only works in
  the two-step flow. The two-step flow is more useful for real
  build systems anyway; temp-`.o` is nice-to-have.
* Verify end-to-end: `tcc -g -c foo.c && tcc -g foo.o -o foo &&
  dsymutil foo -o foo.dSYM` produces a working bundle that
  Tiger gdb can find when given the exe path.

Likely a session of focused work given the load-time bookkeeping
and the dsymutil-on-Tiger verification dance.

### 2. (carried) Multi-session arcs

Unchanged from session 075's handoff:

* (068 #2) Extend self-link diagnostics to `macho_output_dylib`.
* (068 #3) Post-write linter (`otool` / `nm` sanity over emitted
  bytes).
* (068 #4) `-vv` diagnostic for reader's silent dropped relocs.
* (067 #3) Sibling r11 watch (no surface yet).
* AltiVec intrinsics.
* bcheck.c port.

Each is its own multi-session arc; pick one with the user.

### 3. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after a few more
confirmed rebuild cycles on imacg3 — session 076 counts as one
more (clean re-configure from `rm config.mak config.h` succeeded
cleanly).

### 4. (optional) ibookg37 `--builtins+bitfields` generator-host validation

From session 071's handoff. Lower priority — the default-opts
sweep there already validated ibookg37 as a generator host.

## How to pick up

### Verify the v0.2.55 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.55-g-default-stabs.sh
```

Expected last line:
`OK: bare -g now emits stabs by default; -gdwarf-2 still emits DWARF.`

Also re-verify v0.2.54 (regression check):

```sh
bash demos/v0.2.54-stabs-roundtrip.sh
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

v0.2.55-g3 was tagged and pushed at the end of this session
(`git tag v0.2.55-g3`; `git push origin main`; `git push origin
v0.2.55-g3`). No housekeeping remaining.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

This is the smallest source-change release in the
072→076 arc: one configure line flipped, plus a refreshed comment
explaining the rationale. The substantive work was the four prior
sessions; this one banks the UX win that makes it visible without
the user typing `-gstabs`.

The one mid-session correction worth flagging: my first demo asserted
"bare `-g` build has NO `__DWARF` segment" as the discriminator
between stabs and DWARF builds. That was wrong — v0.2.54 moved
`.stab` / `.stabstr` INTO the `__DWARF` segment (`__DWARF,__stab` and
`__DWARF,__stabstr`, with `S_ATTR_DEBUG`, mirroring `__DWARF,__debug_*`).
So both a stabs-emitting and a DWARF-emitting build now produce a
`__DWARF` segment; the discriminator is what's inside it. Fixed
the demo to check for `__stab` vs `__debug_info` instead, then it
passed cleanly. This is captured in the demo's comment block and in
the session README so a future reader doesn't burn time on the same
miscount.

After v0.2.55 the only #7.5 work left is the actual N_OSO record
emission + the dsymutil-on-Tiger verification dance — a substantive
but well-scoped session of work on top of v0.2.54's existing stab
round-trip chain.

Next session: [docs/sessions/076-g-default-stabs-2026-05-13/HANDOFF.md](docs/sessions/076-g-default-stabs-2026-05-13/HANDOFF.md)
