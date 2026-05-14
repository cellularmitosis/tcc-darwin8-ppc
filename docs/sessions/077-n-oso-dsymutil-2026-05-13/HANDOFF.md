# Handoff — end of session 077 (2026-05-13)

## TL;DR

**v0.2.56-g3 closes roadmap #7.5 Phase 2 item 1b**: `dsymutil-on-Tiger`
now consumes tcc-built `-gdwarf-2` Mach-O exes and produces a working
`.dSYM` bundle. With this, every sub-item of the stabs/gdb-on-Tiger
arc (072 → 077, v0.2.51-g3 → v0.2.56-g3) is done.

* **HEAD at session start:** `8828b62` (end of session 076 / v0.2.55-g3 + handoff).
* **HEAD at session end:** `a2fa96e` (v0.2.56-g3, tagged + ready to push).
* **Source changes:** five files in `tcc/`
  ([stab.def](../../../tcc/stab.def),
   [tcc.h](../../../tcc/tcc.h),
   [libtcc.c](../../../tcc/libtcc.c),
   [ppc-macho.c](../../../tcc/ppc-macho.c),
   [tccdbg.c](../../../tcc/tccdbg.c))
  plus one collateral demo fix
  ([v0.2.39-eh-frame.sh](../../../demos/v0.2.39-eh-frame.sh)).
* **Demo added:**
  [`demos/v0.2.56-n-oso-dsymutil.{c,sh}`](../../../demos/) plus a
  helpers TU exercising the multi-function-per-`.o` high_pc fix.
* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint holds;
  all ten v0.2.37-v0.2.55 debug demos still green; new v0.2.56 demo
  green.**

The stabs/gdb arc map across both days:

| ver | session | what it added |
|---|---|---|
| v0.2.51 | 072 | OSO STAB emission into LC_SYMTAB — file:line / list / bt / print global |
| v0.2.52 | 073 | PSYM/LSYM offset fixup — print param/local |
| v0.2.53 | 074 | BNSYM/ENSYM body markers — gcc-4.0 format match |
| v0.2.54 | 075 | gstabs survives the `.o` round-trip — cross-TU two-step flow |
| v0.2.55 | 076 | bare `-g` defaults to stabs — UX wrap-up |
| v0.2.56 | 077 | N_OSO + dsymutil end-to-end — strip-and-debug story |

## What landed

### Files edited

* [`tcc/stab.def`](../../../tcc/stab.def) — define
  `__define_stab (N_OSO, 0x66, "OSO")` with a comment block on the
  Apple debug-map convention.

* [`tcc/tcc.h`](../../../tcc/tcc.h) — new
  `struct macho_oso_state` (and per-state `struct macho_oso_func`)
  hoisted above `TCCState`; pointer + count fields added to
  `TCCState`'s Mach-O block.

* [`tcc/libtcc.c`](../../../tcc/libtcc.c) — `tcc_delete` frees each
  state's `path` + `funcs[]` array + the states array itself.

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — two
  significant additions:
  * `macho_load_object_file` Pass 1.5 records per-loaded-.o OSO
    state when `s1->dwarf != 0` and `.o` has `__DWARF,__debug_*`
    sections. Captures realpath + mtime + `__text` contribution
    range + defined-in-text function nlist entries. Functions
    sorted by offset; sizes via adjacency.
  * `emit_stab_nlist` adds a synthesized debug-map chain at the
    end of its main walk — one `N_SO source / N_OSO obj /
    {N_BNSYM, N_FUN(name), N_FUN(""=size), N_ENSYM} per fn /
    N_SO ""` triplet per OSO state.

* [`tcc/tccdbg.c`](../../../tcc/tccdbg.c) — PPC-narrow DWARF-2
  conformance fixes in `tcc_debug_funcend`:
  * `AT_frame_base` ← `DW_OP_reg31` (was `DW_OP_call_frame_cfa`,
    a DWARF-3 op Tiger's `dwarf_utilities-42` asserts on).
  * `AT_high_pc` (`DW_FORM_data4`) ← `func_ind + size` with a
    text-section reloc (was `size`, offset form, DWARF-3+ only).

* [`demos/v0.2.39-eh-frame.sh`](../../../demos/v0.2.39-eh-frame.sh)
  — collateral: swap bare `-g` for `-gdwarf` so the demo's DWARF
  emission assertions still hold post-v0.2.55. The demo's actual
  purpose (eh_frame + per-prolog CFI) is independent of the default.

### Files added

* [`demos/v0.2.56-n-oso-dsymutil.c`](../../../demos/v0.2.56-n-oso-dsymutil.c)
  — main TU; calls `helper_add` + `helper_mul` then prints `sum=N
  prod=N argc=N`.
* [`demos/v0.2.56-n-oso-dsymutil-helpers.c`](../../../demos/v0.2.56-n-oso-dsymutil-helpers.c)
  — two functions in one TU; exercises the multi-function-per-`.o`
  `AT_high_pc` fix.
* [`demos/v0.2.56-n-oso-dsymutil.sh`](../../../demos/v0.2.56-n-oso-dsymutil.sh)
  — driver. Covers both modes:
  * stabs build: zero N_OSO records (would crash Tiger gdb); scripted
    gdb still reads `info line main` + source listing out of the exe.
  * DWARF build: six SO/OSO entries (one chain per `.o`), both OSOs
    carry the .o's realpath + non-zero mtime, three BNSYM/ENSYM
    pairs straddle `main` / `helper_add` / `helper_mul` with named
    FUN markers. `dsymutil` exits 0, produces a `.dSYM`, and the
    bundle's `__debug_pubnames` lists all three functions.

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — v0.2.56-g3 row prepended;
  header bumped ("v0.2.55-g3" → "v0.2.56-g3", "Thirty-three" →
  "Thirty-four"); #7.5 row rewritten to credit v0.2.56 with closing
  Phase 2 item 1b (and reframed to describe the *DWARF-flow* +
  dsymutil workflow, which is the real shape of "dsymutil works on
  tcc binaries" given Tiger's cctools constraints).
* [`demos/README.md`](../../../demos/README.md) — v0.2.56 row added
  above v0.2.55.
* [`docs/sessions/077-n-oso-dsymutil-2026-05-13/README.md`](README.md)
* This `HANDOFF.md`.

### Memory updates

None. The Tiger-specific gotchas (`dsymutil` only consumes DWARF;
Tiger gdb 6.3 reads `.dSYM` bundles; cctools `dwarf_utilities-42`
asserts on unknown DW_OPs; gcc-4.0's `-gdwarf-2` debug-map format
in detail) are all captured in the session README's "Findings worth
remembering" block — codebase-derivable via `man dsymutil`,
`dwarfdump`, and `nm -ap` on a gcc-4.0 reference build.

## Status of session 076's open items

| # | Item | Status |
|---|---|---|
| (076 #1) | `N_OSO` + `dsymutil` end-to-end — Phase 2 item 1b | **landed this session (v0.2.56)** |
| (076 #2 carried) | self-link diagnostics → `macho_output_dylib` (068 #2) | unchanged |
| (076 #2 carried) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (076 #2 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (076 #2 carried) | Sibling r11 watch (067 #3) | unchanged |
| (076 #2 carried) | AltiVec intrinsics | unchanged |
| (076 #2 carried) | bcheck.c port | unchanged |
| (076 #3) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more confirmed rebuild cycle) |
| (076 #4) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

With v0.2.56, **all of roadmap #7.5 is done** and the stabs/gdb-on-
Tiger arc that ran 072-077 is complete. The remaining open items are
the carried multi-session arcs from prior handoffs:

### 1. (carried, optional) Strip-and-debug-via-dSYM polish

This session's v0.2.56 covers the *production* of working `.dSYM`
bundles; what's not in scope is the full **strip-and-debug** UX —
i.e. `tcc -gdwarf-2 ... -o exe; dsymutil exe -o exe.dSYM; strip exe;
gdb exe`. Two pre-existing tcc gaps surface there:

* **`strip -S exe` fails** with `the __LINKEDIT segment does not
  cover the end of the file (can't be processed)`. tcc lays the
  `__DWARF` segment *after* `__LINKEDIT` in the exe; Apple's strip
  predates this layout and rejects it. Possible fix: emit `__DWARF`
  before `__LINKEDIT` in `macho_output_exe`, or skip `__DWARF`
  emission entirely when an OSO chain is also being emitted (since
  dsymutil's whole point is to extract DWARF from `.o` files, the
  exe's own `__DWARF` is redundant). The latter is simpler and
  matches gcc-4.0's behavior — its `-gdwarf-2`-linked exes only
  contain a small linker-injected `__darwin_gcc3_*` DWARF, not the
  full per-TU DWARF.

* **`LC_UUID` not emitted.** Tiger gdb pairs an exe with its
  `.dSYM` by matching UUIDs from each binary's `LC_UUID` load
  command. tcc doesn't emit one at all, so gdb warns
  (`UUID mismatch detected`) but proceeds for the un-stripped
  case. After `strip -S` the warning becomes a hard "no debug
  info" — gdb refuses to fall back to the `.dSYM`. Fix: synthesize
  an `LC_UUID` (16 bytes; can be a content hash of the linked
  exe's text/data or just a random ID) and emit it in both the
  exe and the .dSYM's `__LINKEDIT` (tcc writes the .dSYM-side via
  `dsymutil`, which would carry the exe's UUID forward).

Likely a half-session of focused work. Useful if anyone actually
needs the strip-and-distribute-`.dSYM`-separately flow on Tiger;
not blocking for any current use case.

### 2. (carried, optional) `<string>` compile-unit at link time

When tcc emits its own DWARF compile unit at link time (for the
linker's own bookkeeping symbols), the `AT_name` is `<string>`
rather than a real source file. This shows up in `dwarfdump`
output and confuses some readers. In a clean `.dSYM` flow with
proper per-`.o` DWARF, dsymutil ignores it — but if anyone reads
DWARF directly from the linked exe, it's a sharp edge. Possible
fix: suppress the link-time compile unit when we're emitting an
OSO chain (the `.o` DWARFs cover everything). Same scope as #1.

### 3. (carried) Multi-session arcs

Unchanged from session 076's handoff:

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
confirmed rebuild cycles on imacg3 — session 077 counts as one
more (clean re-configure from `rm config.mak config.h` succeeded,
multiple full clean-builds, full test run, full demo run).

### 5. (optional) ibookg37 `--builtins+bitfields` generator-host validation

From session 071's handoff. Lower priority — the default-opts
sweep there already validated ibookg37 as a generator host.

## How to pick up

### Verify the v0.2.56 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.56-n-oso-dsymutil.sh
```

Expected last line:

```
OK: -gstabs flow unchanged (Tiger gdb reads exe-resident stabs);
    -gdwarf-2 emits an N_OSO debug-map chain dsymutil consumes cleanly.
```

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    cd tcc/tests && make abitest && cd ../.. && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0`, 48 abitest successes,
`FIXPOINT HOLDS`.

### Re-run the v0.2.37-v0.2.55 debug demos (regression check)

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    for d in v0.2.37-dwarf-obj v0.2.38-dwarf-exe v0.2.39-eh-frame \
             v0.2.44-gdebug-link v0.2.51-gstabs-oso-stab \
             v0.2.52-gstabs-locals v0.2.53-bnsym-ensym \
             v0.2.54-stabs-roundtrip v0.2.55-g-default-stabs \
             v0.2.56-n-oso-dsymutil; do
        printf "%s: " "$d"; bash demos/$d.sh 2>&1 | tail -1
    done'
```

Each line should end with `PASS` / `OK: ...` / `success`.

### Tag + push status

v0.2.56-g3 is tagged but **not yet pushed**. To finish:

```sh
git push origin main
git push origin v0.2.56-g3
```

(Push authority for this repo was granted per
`memory/feedback_push_authority.md`; the only reason to wait is if
you want a final look.)

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

The handoff that led into this session ("emit N_OSO and dsymutil
works") turned out to be partly wrong: dsymutil-on-Tiger only
consumes DWARF in `.o` files, not stabs, so the working flow is
`-gdwarf-2`-throughout, not `-g`/`-gstabs`-throughout. Once that
was clear, three additional blockers surfaced that pre-existed v0.2.56:

1. Emitting N_OSO with the `.o` path in stabs mode would crash
   Tiger gdb (it follows OSO records and reads stabs from the
   `.o`'s `LC_SYMTAB` nlist, but tcc puts `.o` stabs in
   `__DWARF,__stab` instead). Resolved by gating OSO emission to
   DWARF mode only.
2. Tcc's `DW_OP_call_frame_cfa` for `AT_frame_base` is DWARF-3;
   Tiger's `dsymutil` asserts on it. Switched PPC to `DW_OP_reg31`.
3. Tcc's `AT_high_pc` was emitted as offset (DWARF-3+ form);
   under DWARF-2 (which is the default) the value should be an
   absolute end-VA. Pre-fix, only the first function per `.o`
   showed up in the consolidated `.dSYM`. Switched PPC to
   `low_pc + size` with a text-section reloc.

After all three, the session-077 demo's dsymutil step produces a
`.dSYM` with `__debug_pubnames` listing every function from every
loaded `.o`. The user-visible benefit on Tiger is modest (gdb
already worked directly out of `LC_SYMTAB` post-v0.2.51, and from
`__DWARF` segments post-v0.2.38) — but `.dSYM` is the canonical way
to distribute debug info separately from a stripped exe, and that
workflow is now unblocked at the consumer level. The remaining
gaps for end-to-end strip-then-debug are tcc's missing `LC_UUID`
emission and the `__DWARF`-after-`__LINKEDIT` layout that confuses
`strip` — both pre-existing, both tracked above.

Next session: [docs/sessions/077-n-oso-dsymutil-2026-05-13/HANDOFF.md](docs/sessions/077-n-oso-dsymutil-2026-05-13/HANDOFF.md)
