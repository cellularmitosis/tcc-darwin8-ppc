# Handoff — end of session 079 (2026-05-14)

## TL;DR

**v0.2.58-g3 closes the source-line-debug-via-`.dSYM` gap flagged
in session 078's handoff item #1.** `tcc -gdwarf-2 -c ... && tcc
-gdwarf-2 ... -o exe && dsymutil exe -o exe.dSYM && strip -S exe &&
gdb exe` now works as a single workflow on Tiger with FULL source-
line debug: `list`, `break <file>:<line>`, `info functions`,
`print` all resolve from the `.dSYM`. The fix turned out to be a
**layout** bug, not the `N_SO source-path` content bug suggested
in the session-078 handoff: tcc was emitting a `__DWARF` segment
on the linked exe (carrying only the keymgr-stub `<string>` CU);
`dsymutil` preserved it AND appended a consolidated one;
`dwarfdump` / gdb iterated segments in load-command order and
stopped at the first (empty) `__DWARF`. Fix is one line in
`tccelf.c` — suppress `s->do_debug` around the link-time keymgr-
stub `tcc_compile_string` call — and the linker's `.debug_*`
sections stay empty in a two-step build, so `macho_output_exe`
drops the `__DWARF` segment entirely (gcc-4.0 parity).

* **HEAD at session start:** `a4da4c2` (end of session 078 / v0.2.57-g3 + handoff).
* **HEAD at session end:** v0.2.58-g3, tag pushed.
* **Source changes:** one file
  ([tccelf.c](../../../tcc/tccelf.c)).
* **Demo added:**
  [`demos/v0.2.58-dsym-source-lines.{c,sh}`](../../../demos/) plus
  a helpers TU.
* **Demo updated:**
  [`demos/v0.2.57-strip-uuid-dsym.sh`](../../../demos/v0.2.57-strip-uuid-dsym.sh)
  (its `__DWARF before __LINKEDIT` check now accepts either
  the v0.2.57 layout or the v0.2.58 layout).
* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint
  holds; all twelve v0.2.37–v0.2.58 debug demos green.**

## What landed

### Files edited

* [`tcc/tccelf.c`](../../../tcc/tccelf.c) — save and clear
  `s->do_debug` around the keymgr-stub `tcc_compile_string` call
  inside `tcc_output_file`. Restore after. 35-line block (the
  bulk is the comment block explaining the layout bug, the gcc-
  parity rationale, and the two-step-vs-single-step split).

* [`demos/v0.2.57-strip-uuid-dsym.sh`](../../../demos/v0.2.57-strip-uuid-dsym.sh)
  — the `[layout] __DWARF appears before __LINKEDIT` check now
  accepts two outcomes: `"DWARF LINKEDIT"` (v0.2.57 layout) or
  `"LINKEDIT"` alone with no `__DWARF` at all (v0.2.58 layout).
  Comment explains why. Other assertions unchanged.

### Files added

* [`demos/v0.2.58-dsym-source-lines.c`](../../../demos/v0.2.58-dsym-source-lines.c)
  — main TU; same `helper_add` + `helper_mul` shape as v0.2.57
  so the OSO chain has more than one entry.
* [`demos/v0.2.58-dsym-source-lines-helpers.c`](../../../demos/v0.2.58-dsym-source-lines-helpers.c)
  — helpers TU.
* [`demos/v0.2.58-dsym-source-lines.sh`](../../../demos/v0.2.58-dsym-source-lines.sh)
  — driver. Asserts:
  * No `__DWARF` segment in the linked exe.
  * `dsymutil exe -o exe.dSYM` succeeds.
  * The `.dSYM` has exactly one `__DWARF` segment.
  * `dwarfdump` reads `TAG_compile_unit` from the `.dSYM`'s
    `.debug_info` (i.e. non-empty).
  * Both source files appear as `AT_name(...src...)` (one CU per
    `.o`, matching by suffix because tcc records the absolute path).
  * `strip -S exe` exits 0; stripped exe still runs.
  * Scripted `gdb -batch` does `list main`, `list helper_add`,
    `break <file>:<line>` against the `.dSYM` post-strip.

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped
  ("v0.2.57-g3" → "v0.2.58-g3", "Thirty-five" → "Thirty-six");
  v0.2.58-g3 row prepended; #7.5 row marked done across
  v0.2.51..v0.2.58 (Phase 2 fully closed) with v0.2.58's exact
  scope summarized in sub-paragraph (c).
* [`demos/README.md`](../../../demos/README.md) — v0.2.58 row
  added above v0.2.57; v0.2.57 row updated to reflect the
  either-layout check.
* `docs/sessions/079-dsymutil-debug-info-2026-05-14/README.md`
* This `HANDOFF.md`.

### Memory updates

None. The dsymutil-layout finding (the exe's `__DWARF` segment
gets preserved AND a second is appended; dwarfdump/gdb read the
first) is captured in the source comment of `tccelf.c`'s
keymgr-stub block and in the session README's "Findings worth
remembering". The fix's behavior (two-step suppresses `__DWARF`,
single-step keeps it) is in the source comment and the v0.2.58
demo's header.

## Status of session 078's open items

| # | Item | Status |
|---|---|---|
| (078 #1) | dsymutil `.debug_info` consolidation (v0.2.58 task) | **landed this session (v0.2.58)** |
| (078 #2) | `<string>` compile-unit at link time | **resolved by v0.2.58** — keymgr stub no longer emits a CU; the `<string>` AT_name no longer appears in linked exes. |
| (078 #3 carried) | self-link diagnostics → `macho_output_dylib` (068 #2) | unchanged |
| (078 #3 carried) | Post-write linter (otool/nm) over emitted bytes (068 #3) | unchanged |
| (078 #3 carried) | `-vv` diagnostic for reader's silent dropped relocs (068 #4) | unchanged |
| (078 #3 carried) | Sibling r11 watch (067 #3) | unchanged |
| (078 #3 carried) | AltiVec intrinsics | unchanged |
| (078 #3 carried) | bcheck.c port | unchanged |
| (078 #4) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more clean rebuild cycle) |
| (078 #5) | ibookg37 `--builtins+bitfields` generator-host validation | unchanged |

## Open work for next session

Roadmap #7.5 (Phase 2) is now fully done. The natural next-arc
candidates are the carried items from session 078's #3 — each is
its own multi-session arc, pick one with the user:

### 1. (carried, 068 #2) Self-link diagnostics for `macho_output_dylib`

v0.2.50 added four pre-write sanity checks to `macho_output_exe`
that turn cryptic dyld errors into tcc-level diagnostics. The
parallel `macho_output_dylib` function does not yet carry the
equivalent checks. A dylib gone wrong (LC_DYSYMTAB miscounting,
__nl_symbol_ptr slot not paired with its stub, etc.) still
surfaces only as `dlopen("...") returned NULL` with no useful
context. Port the four invariants.

### 2. (carried, 068 #3) Post-write linter (otool / nm sanity)

Run `otool -lv` / `nm` over `macho_output_exe`'s output before
returning success, parse for shapes the writer is supposed to
produce (LC_LOAD_DYLIB count == nb_dlls + 1, LC_DYSYMTAB extreloff
+ nextrel within file, LC_SYMTAB nsyms == n_local + n_extdef +
n_undef, etc.). Backstop for the kind of bug the v0.2.57 strip
work surfaced (where the writer's intent didn't match the bytes
it produced).

### 3. (carried, 068 #4) `-vv` diagnostic for reader's silent dropped relocs

When `macho_load_object_file` skips a section or drops a reloc
(e.g. SECTDIFF target lookup miss; debug section skip), it does
so silently. A `-vv` flag enabled at link time should log every
such decision to stderr with `file:section:reloc-index reason`.
Would have made the v0.2.40 sed-bug and the v0.2.44 SECTDIFF
shadow-section bug easier to triage.

### 4. (carried, 067 #3) Sibling r11 watch

Sibling-register concern to v0.2.48's r12 fix — r11 is hardcoded
scratch in the rare large-stack-offset struct-arg path, while
still in the int allocator pool. csmith hasn't surfaced a repro
yet. If a future seed does, the symmetric fix is `reg_classes[8]
= 0`. Just keep an eye on it.

### 5. (carried) AltiVec intrinsics

PowerPC SIMD. Would unlock vector-heavy programs (libpng's
filter loops, openssl's AES-NI fallback paths on PPC, gimp's
pixel ops). New parser front-end work + codegen.

### 6. (carried) `bcheck.c` port

PowerPC port of tcc's bounds-check runtime. Currently stubbed
out via `bcheck-ppc.c` (no-op shims). Real port would need to
intercept allocations, maintain a redzone map, and emit per-
load/store bounds-check calls.

### 7. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after a few more
confirmed rebuild cycles on imacg3 — session 079 counts as one
more.

### 8. (optional, low priority) `rsync` exclude-list polish

`tcc/c2str.exe` (built on uranium for testing, arm64) is not in
my current rsync exclude list, so it ships to imacg3 and breaks
the build. Worked around manually in this session (extended
exclude list, then deleted on imacg3). If sync becomes a recurring
pain point, the right move is a `scripts/sync-to-imacg3.sh`
wrapper with the canonical exclude list. Not urgent.

## How to pick up

### Verify the v0.2.58 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.58-dsym-source-lines.sh
```

Expected last line:

```
OK: keymgr-stub debug suppression keeps the linker's .debug_*
    sections empty in a two-step build, so the linked exe has no
    __DWARF segment; dsymutil produces a single-__DWARF .dSYM that
    Tiger gdb can use for list / break file:LINE source-line debug
    of a stripped exe.
```

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    cd tcc/tests && /opt/make-4.3/bin/make abitest && cd ../.. && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0`, 48 abitest successes,
`FIXPOINT HOLDS`.

### Re-run the v0.2.37–v0.2.58 debug demos

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    for d in v0.2.37-dwarf-obj v0.2.38-dwarf-exe v0.2.39-eh-frame \
             v0.2.44-gdebug-link v0.2.51-gstabs-oso-stab \
             v0.2.52-gstabs-locals v0.2.53-bnsym-ensym \
             v0.2.54-stabs-roundtrip v0.2.55-g-default-stabs \
             v0.2.56-n-oso-dsymutil v0.2.57-strip-uuid-dsym \
             v0.2.58-dsym-source-lines; do
        printf "%-32s " "$d"; bash demos/$d.sh 2>&1 | tail -1
    done'
```

Each line should end with a one-line success summary.

### Tag + push status

v0.2.58-g3 is tagged. To push (push authority granted per
`memory/feedback_push_authority.md`):

```sh
git push origin main
git push origin v0.2.58-g3
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

What the session-078 handoff suggested ("`N_SOL` may need to
carry the source path, not the `.o` path", or "tcc could emit an
`N_SO` stab into the `.o` even in `-gdwarf-2` mode") would not
have worked — the bug wasn't in `N_SO` / `N_SOL` at all. The
bug was that the linked exe carried a `__DWARF` segment with the
keymgr `<string>` CU, and `dsymutil` is "duplicate any segment
the exe already has, then append" rather than "merge". The first
`__DWARF` won every reader, the second was unreachable.

The diff-gcc-vs-tcc-on-the-same-source approach the handoff
suggested as the best entry point was the right call: section-
size comparison surfaced the second `__DWARF` segment within ten
minutes. Without it I might have spent a half-session chasing
`N_SO` patches.

User-visible benefit of v0.2.58: the strip-and-distribute-`.dSYM`-
separately flow now gives full source-line debug on Tiger PPC. A
production binary stripped of stabs can be debugged through its
companion `.dSYM` exactly as on modern macOS — file, line, params,
locals, breakpoints. The OSO+stabs+DWARF+dsymutil arc that ran
from session 072 through 079 is fully closed.

Next session: [docs/sessions/079-dsymutil-debug-info-2026-05-14/HANDOFF.md](docs/sessions/079-dsymutil-debug-info-2026-05-14/HANDOFF.md)
