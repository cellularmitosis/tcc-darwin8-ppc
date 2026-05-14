# Handoff — end of session 078 (2026-05-13 → 2026-05-14)

## TL;DR

**v0.2.57-g3 closes the strip-and-debug-via-dSYM polish flagged
in session 077's handoff.** `tcc -gdwarf-2 ... -o exe; dsymutil
exe; strip -S exe; gdb exe` now works as a single workflow on
Tiger: `strip` accepts the layout, gdb finds the `.dSYM`
automatically via `LC_UUID` match, and resolves symbol lookups
from it. One pre-existing v0.2.56 limitation remains
(dsymutil's `.debug_info` consolidation produces an empty
`.dSYM` — only `.debug_pubnames` populates), so post-strip gdb
can `info functions main` and `break *main` but not
`list main` / `break main:LINE`. That's the v0.2.58 task.

* **HEAD at session start:** `21ad15a` (end of session 077 / v0.2.56-g3 + handoff).
* **HEAD at session end:** v0.2.57-g3, tag pushed.
* **Source changes:** one file
  ([ppc-macho.c](../../../tcc/ppc-macho.c)).
* **Demo added:**
  [`demos/v0.2.57-strip-uuid-dsym.{c,sh}`](../../../demos/) plus a
  helpers TU.
* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint holds;
  all eleven v0.2.37–v0.2.56 debug demos still green; new v0.2.57
  demo green end-to-end.**

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) (+188 / −69):

  * `#define LC_UUID 0x1b` added to the load-command catalog.

  * `macho_output_exe`:

    * `__DWARF` segment file-laid before `__LINKEDIT` (was: after).
      Section file offsets assigned at the `text+data` end; segment
      `filesize` rounded up to the next page so `__LINKEDIT.fileoff`
      lands page-aligned (required for dyld's `__LINKEDIT` mmap —
      otherwise `doBindIndirectSymbolPointers` faults at the
      indirect symbol-table address, which we hit live during
      bring-up). VM layout unchanged: `__DWARF` keeps `vmaddr=0`,
      so `__LINKEDIT.vmaddr` still sits right after `__TEXT+__DATA`.
    * `__LINKEDIT` interior order changed from
      `indirect → symtab → strtab` to
      `symtab → indirect → strtab` (gcc-4.0's order). Tiger's
      `strip` enforces it via the "symbol table out of place"
      assertion. The pad/write block was reordered to match.
    * Sanity check `__LINKEDIT.fileoff == __TEXT+__DATA end`
      updated to add `+ dwarf_seg_filesize`. New check asserts
      `__DWARF.fileoff == __TEXT+__DATA end` so future layout
      regressions surface at link time, not at strip time.
    * `LC_SEGMENT __DWARF` and `LC_SEGMENT __LINKEDIT` write
      blocks physically swapped so the LC table also lists
      `__DWARF` before `__LINKEDIT`.
    * `LC_UUID` (24 bytes total: cmd + cmdsize + 16 UUID bytes)
      always emitted between `LC_DYSYMTAB` and `LC_UNIXTHREAD`.
      `ncmds` and `total_cmds_size` bumped unconditionally.
    * 16 UUID bytes computed (after relocations resolve, before
      serialization) by running two FNV-1a 64-bit hashes with
      different seeds (one forward, one with the buffer folded
      backwards to decorrelate) over the post-relocation bytes
      of `text + rodata + eh_frame + data + init_array +
      fini_array` and every `__DWARF` section. RFC 4122 v4
      marker bits set so otool/dwarfdump/lldb pretty-print it
      as a UUID.

  * `emit_stab_nlist`: per-function bracket gained an `N_SOL`
    record between `N_FUN(name)` and `N_FUN(size)`, carrying the
    `.o`-path string offset (the same string `N_OSO` already
    points at). gcc-4.0 emits this carrying the *source* path;
    we use the `.o` path as a stand-in because we don't yet read
    the `.o`'s DWARF compile-unit `AT_name` at link time. Tiger's
    `dsymutil` accepts the marker structurally (the `.dSYM`
    builds) but full `.debug_info` consolidation needs more (see
    item 1 below).

### Files added

* [`demos/v0.2.57-strip-uuid-dsym.c`](../../../demos/v0.2.57-strip-uuid-dsym.c)
  — main TU; calls `helper_add` + `helper_mul` then prints
  `sum=N prod=N argc=N`.
* [`demos/v0.2.57-strip-uuid-dsym-helpers.c`](../../../demos/v0.2.57-strip-uuid-dsym-helpers.c)
  — helpers TU. Multi-TU build keeps the OSO chain non-trivial.
* [`demos/v0.2.57-strip-uuid-dsym.sh`](../../../demos/v0.2.57-strip-uuid-dsym.sh)
  — driver. Asserts:
  * `__DWARF` appears before `__LINKEDIT` in the LC table
  * `__LINKEDIT` covers the end of the file
  * `LC_UUID` present, non-zero, deterministic across rebuilds
  * `dsymutil` produces a `.dSYM` carrying the same `LC_UUID`
  * `N_SOL` records appear in the OSO chain (one per function)
  * `strip -S` exits 0; stripped exe still runs; `LC_UUID`
    survives the strip
  * Tiger `gdb` pairs the stripped exe with its `.dSYM` via
    UUID and resolves `info functions main` from it.

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped
  ("v0.2.56-g3" → "v0.2.57-g3", "Thirty-four" → "Thirty-five");
  v0.2.57-g3 row prepended; #7.5 row gets a new (c) sub-paragraph
  describing the strip-and-distribute-`.dSYM`-separately flow and
  flagging the dsymutil consolidation gap as v0.2.58 work.
* [`demos/README.md`](../../../demos/README.md) — v0.2.57 row
  added above v0.2.56.
* `docs/sessions/078-strip-and-debug-polish-2026-05-13/README.md`
* This `HANDOFF.md`.

### Memory updates

None. The Tiger-specific gotchas surfaced this session
(strip's three layout invariants, dyld's `__LINKEDIT`
page-alignment requirement, `LC_UUID` propagation through
`dsymutil`) are all captured in the source comments of
`macho_output_exe` and the session README's "Findings worth
remembering" block — codebase-derivable. The dsymutil
consolidation gap is described in detail in the README and
again under "Open work for next session" below.

## Status of session 077's open items

| # | Item | Status |
|---|---|---|
| (077 #1) | Strip-and-debug-via-dSYM polish (LC_UUID + segment ordering) | **landed this session (v0.2.57)** |
| (077 #2) | `<string>` compile-unit at link time | unchanged |
| (077 #3 carried) | self-link diagnostics → `macho_output_dylib` (068 #2) | unchanged |
| (077 #3 carried) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (077 #3 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (077 #3 carried) | Sibling r11 watch (067 #3) | unchanged |
| (077 #3 carried) | AltiVec intrinsics | unchanged |
| (077 #3 carried) | bcheck.c port | unchanged |
| (077 #4) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more clean rebuild cycle) |
| (077 #5) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

### 1. (NEW, optional) dsymutil `.debug_info` consolidation

This is the polish item that v0.2.57 does **not** close. After
running `dsymutil exe -o exe.dSYM` on a tcc-built `-gdwarf-2`
exe, the resulting `.dSYM` bundle contains:

* `.debug_pubnames` — populated, lists every function. So
  `gdb info functions main`, `gdb break *main`, `gdb print main`
  all work.
* `.debug_info`, `.debug_abbrev`, `.debug_line`, `.debug_str` —
  empty. So `gdb list main`, `gdb break main:LINE`, `step` source
  view all fail with `No line number known for main`.

For a gcc-4.0-built reference exe, `dsymutil` populates all
sections — gdb gets full source-line debug from the `.dSYM`.

Pre-strip, this gap doesn't matter (gdb reads stabs straight
from the exe's `LC_SYMTAB` since v0.2.51). Post-strip it does:
the source-line debug story falls back to symbol lookup only.

What we already tried this session (both unsuccessful):

* Hex-patching `N_SO` to point at the source path instead of the
  `.o` path — `.dSYM` still empty.
* Adding `N_SOL` to each function's bracket (gcc-4.0 chain
  parity) — `.dSYM` still empty.

Likely root causes to investigate next:

* dsymutil may need `N_SOL` (or `N_SO`) to carry the **source**
  path, not the `.o` path. We currently emit the `.o` path
  because we don't have the source path at link time. Reading
  the source path from the `.o`'s `__DWARF,__debug_info` would
  require parsing the DWARF compile-unit DIE: skip the abbrev
  code (1 byte), skip `AT_producer` (`DW_FORM_strp` = 4 bytes),
  skip `AT_language` (`DW_FORM_data1` = 1 byte), then read
  `AT_name` (`DW_FORM_line_strp` = 4 bytes, an offset into
  `__debug_str` since DWARF<5 fallbacks line-strp to str).
  Alternative: have tcc emit an N_SO stab into the `.o` even
  in `-gdwarf-2` mode (compile-time change, `tccdbg.c`-side),
  then read it at link time.
* Some other property of tcc's DWARF that gcc-4.0 satisfies and
  Tiger's `dwarf_utilities-42` requires (line table format?
  abbrev-table layout? `AT_comp_dir` resolution?). A diff
  against a gcc-built `.o` of the same source would isolate.

If the `N_SO`-source-path hypothesis pans out, the fix is small.
If it doesn't, this could be a multi-half-session arc to
characterize what dsymutil wants and serve it.

Best entry point: build a gcc-4.0 `.o` and a tcc `.o` of the
same trivial source, run `dsymutil` on each, diff the resulting
`.dSYM` `__debug_info` byte streams to see what's surviving in
gcc's case and not ours.

### 2. (carried) `<string>` compile-unit at link time

When tcc emits its own DWARF compile unit at link time (for the
linker's own bookkeeping symbols), `AT_name` is `<string>`
rather than a real source file. Doesn't affect dsymutil's `.dSYM`
flow (dsymutil reads `.o` DWARF directly), but if anyone reads
DWARF from the linked exe, it's a sharp edge. Suppress the
link-time CU when emitting an OSO chain.

### 3. (carried) Multi-session arcs

Unchanged from session 077's handoff:

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
confirmed rebuild cycles on imacg3 — session 078 counts as one
more (multiple full clean-builds, full test run, full demo run).

### 5. (optional) ibookg37 `--builtins+bitfields` generator-host validation

From session 071's handoff. Lower priority — the default-opts
sweep there already validated ibookg37 as a generator host.

## How to pick up

### Verify the v0.2.57 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.57-strip-uuid-dsym.sh
```

Expected last line:

```
OK: __DWARF reorder + page-padding makes strip -S work; LC_UUID
    round-trips through dsymutil, survives strip, and lets gdb
    pair the stripped exe with its .dSYM.
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

### Re-run the v0.2.37–v0.2.57 debug demos

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    for d in v0.2.37-dwarf-obj v0.2.38-dwarf-exe v0.2.39-eh-frame \
             v0.2.44-gdebug-link v0.2.51-gstabs-oso-stab \
             v0.2.52-gstabs-locals v0.2.53-bnsym-ensym \
             v0.2.54-stabs-roundtrip v0.2.55-g-default-stabs \
             v0.2.56-n-oso-dsymutil v0.2.57-strip-uuid-dsym; do
        printf "%s: " "$d"; bash demos/$d.sh 2>&1 | tail -1
    done'
```

Each line should end with `PASS` / `OK: ...` / a one-line
success summary.

### Tag + push status

v0.2.57-g3 is tagged. To push (push authority granted per
`memory/feedback_push_authority.md`):

```sh
git push origin main
git push origin v0.2.57-g3
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

The handoff that led into this session ("strip + LC_UUID polish,
half-session of focused work") was largely accurate in scope.
What surfaced en route:

1. The `__DWARF`-before-`__LINKEDIT` reorder needed a page-pad
   of `__DWARF.filesize` so `__LINKEDIT.fileoff` lands on a page
   boundary. Without the pad, dyld faulted at runtime in
   `doBindIndirectSymbolPointers` (the indirect-table address
   wasn't covered by a valid mmap'd page). Hit live during
   bring-up; the fix is one extra rounding op.

2. `__LINKEDIT` interior order also matters: cctools `strip`
   enforces gcc-4.0's canonical `symtab → indirect → strtab`
   sequence, with the assertion text "symbol table out of
   place". We were emitting `indirect → symtab → strtab`, which
   passed dyld but tripped strip. The fix is one swap.

3. dsymutil's full `.debug_info` consolidation is independent of
   `LC_UUID` and predates v0.2.57 — gcc-4.0-built exes consolidate
   fully, tcc-built exes get only `.debug_pubnames`. Adding
   `N_SOL` (gcc parity for the chain shape) didn't fix it; nor
   did patching `N_SO` to point at a real source path. The most
   plausible next investigation is whether `N_SOL` (or `N_SO`)
   needs to carry the *source* path (which we don't currently
   know at link time) versus the `.o` path (which we do).
   Documented as the v0.2.58 task.

The user-visible benefit of v0.2.57 alone: `strip -S exe` works
(producing a slim production exe), and `gdb exe` automatically
finds `exe.dSYM` via `LC_UUID` and resolves symbol names from
it. Source-line debugging from a stripped-exe + .dSYM remains
a v0.2.58 follow-up.

Next session: [docs/sessions/078-strip-and-debug-polish-2026-05-13/HANDOFF.md](docs/sessions/078-strip-and-debug-polish-2026-05-13/HANDOFF.md)
