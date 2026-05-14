# Session 079 — dsymutil `.debug_info` consolidation (v0.2.58-g3)

**Dates:** 2026-05-13 evening → 2026-05-14
**Build hosts:** uranium ⇄ imacg3 (rsync + ssh)
**Shipped:** v0.2.58-g3 (tag pushed)

## TL;DR

Closes the source-line-debugging-via-`.dSYM` gap that session 078's
handoff flagged as the v0.2.58 task: post-`strip -S` `gdb list main`
/ `gdb break <file>:<line>` against the companion `.dSYM` now works
end-to-end on Tiger. **Fix is one line in `tccelf.c`**: save and
clear `s->do_debug` around the link-time keymgr-stub
`tcc_compile_string` call. After this, the linker's `.debug_*`
sections stay empty in a two-step build, `macho_output_exe` drops
the `__DWARF` segment entirely (gcc-4.0 parity), and `dsymutil`
produces a single-`__DWARF` `.dSYM` whose contents Tiger gdb 6.3 /
dwarfdump read cleanly.

## Arrival state

* HEAD: `a4da4c2` — v0.2.57-g3 (session 078) + handoff.
* The v0.2.57 demo's strip-and-debug-via-`.dSYM` flow worked for
  symbol-lookup (`info functions main`, `break *main`, `print main`)
  but not source-line ops (`list main`, `break <file>:<line>` failed
  with "No line number known for main"). The empty `.dSYM`
  `.debug_info` was a known limitation flagged for this session.
* The session-078 handoff suggested two plausible candidates: hex-
  patching `N_SO` to point at the source path rather than the `.o`
  path (tried and didn't help); or having tcc emit an `N_SO` stab
  into the `.o` even in `-gdwarf-2` mode and reading it at link
  time. Best entry point per the handoff: diff `dsymutil` output
  for a gcc-4.0 `.o` vs a tcc `.o` of the same trivial source.

## Investigation — gcc vs tcc dsymutil diff

Built a trivial single-file source on imacg3, compiled it with
gcc-4.0 (`gcc-4.0 -gdwarf-2 -c hello.c`) and with tcc
(`tcc -gdwarf-2 -c hello.c`), linked each through its own compiler
(`gcc-4.0 hello-gcc.o` and `tcc hello-tcc.o`), ran `dsymutil` on
both, and compared section sizes via `otool -l`.

The gcc-built `.dSYM` looked normal: one `__DWARF` segment with
populated `__debug_abbrev` (0x56), `__debug_aranges` (0x20),
`__debug_frame` (0x5c), `__debug_info` (0x97), `__debug_line`
(0x54), `__debug_pubnames` (0x23), `__debug_str` (0x6a).

The tcc-built `.dSYM` had **two `__DWARF` segments**:

* segment A (load command 3): mirrors the exe's
  `__debug_info`/`__debug_abbrev`/`__debug_line`/`__debug_aranges`/`__debug_str`,
  all with `size = 0`.
* segment B (load command 4): populated `__debug_abbrev` (0x5e),
  `__debug_aranges` (0x20), `__debug_info` (0xb9), `__debug_line`
  (0x71), `__debug_pubnames` (0x23), `__debug_str` (0x51).

`dwarfdump <inner-dsym>` reported `.debug_info contents: < EMPTY >`
— because it iterates segments in load-command order, finds the
first (empty) `__debug_info`, and stops. The consolidated content
in segment B was unreachable.

So the bug was **layout**, not content. The exe was carrying a
`__DWARF` segment that `dsymutil` faithfully preserved, then
appended its own alongside.

## Tracing the source of the exe's `__DWARF`

`otool -l hello-tcc-exe` confirmed the linked exe has exactly one
`__DWARF` segment (5 sections). `dwarfdump hello-tcc-exe` showed
the only CU inside `.debug_info` was:

```
0x0000000b: TAG_compile_unit [1] *
             AT_producer( "tcc 0.9.28rc" )
             AT_language( DW_LANG_C99 )
             AT_name( "<string>" )
             AT_comp_dir( "/Users/macuser/tmp/s079-dsymutil-diff" )
             AT_low_pc( 0x00001628 )
             AT_high_pc( 0x000006e4 )
             AT_stmt_list( 0x00000000 )
```

— the `<string>` CU from session 077 / 078's handoff item #2.
Nothing from `hello.c` — that CU lives in `hello-tcc.o`'s
`__DWARF` and is referenced via the OSO chain (the loader drops
`__DWARF,__debug_*` from loaded `.o` files; see
`macho_section_to_elf` returning 0 for `__DWARF,__debug_*` entries).

So the `<string>` CU is the **only** content in the linked exe's
`__DWARF`. It comes from the link-time keymgr-stub compile in
`tccelf.c`:

```c
if (!find_elf_sym(s->symtab, "__keymgr_dwarf2_register_sections")) {
    static const char keymgr_stub[] = "...";
    if (tcc_compile_string(s, keymgr_stub) != 0)
        return -1;
}
```

`tcc_compile_string(NULL, ...)` → `tcc_open_bf(s1, NULL → "<string>", len)`
→ `tccgen_compile()` → `tcc_debug_start(s1)` writes a CU header
into `dwarf_info_section` with `AT_name = "<string>"`.

In a two-step build, this is the only thing writing to the
linker's `.debug_*` sections at link time. The `.o`'s DWARF is
deliberately dropped (debug flows via OSO), and there's no other
in-process compile.

## Fix

`tcc/tccelf.c` — one save/restore around the keymgr-stub compile:

```c
int saved_do_debug = s->do_debug;
s->do_debug = 0;
if (tcc_compile_string(s, keymgr_stub) != 0)
    return -1;
s->do_debug = saved_do_debug;
```

With `do_debug = 0`, `tcc_debug_start`'s CU-emission block is
skipped (it's gated on `if (s1->do_debug)`), so the keymgr stub
compiles without writing to `.debug_*`. In a two-step build the
linker's `.debug_*` sections then stay empty across the entire
link, and `macho_output_exe`'s pre-existing
`if (s->data_offset == 0) continue;` filter (around line 2274)
drops every `.debug_*` section from the per-section enumeration,
which leaves `n_dwarf_sects = 0` and therefore no `__DWARF`
segment in the output exe at all — gcc-4.0 parity.

## Result

Two-step `tcc -gdwarf-2 -c ... && tcc -gdwarf-2 ... -o exe`:

* **Linked exe**: zero `__DWARF` segments. Segments are
  `__PAGEZERO`, `__TEXT`, `__DATA`, `__LINKEDIT` — clean.
* **`dsymutil exe`**: produces a `.dSYM` with **exactly one**
  `__DWARF` segment carrying the consolidated content.
* **`dwarfdump <dSYM>`**: reads `TAG_compile_unit` for each `.o`'s
  CU, types, parameters, locals, lexical blocks, line table.
* **Post-`strip -S` `gdb`**:
  * `list main` → prints the source.
  * `list helper_add` → prints the source.
  * `break <file>:<line>` → "Breakpoint at 0xNNN: file <src>, line N."
  * `info breakpoints` → resolves to the right function in the
    right TU.

Single-step `tcc -gdwarf-2 hello.c -o exe`:

* **Linked exe**: still has the user CU in a `__DWARF` segment
  (the in-process compile writes directly to the linker's
  `.debug_*`; my fix only suppresses the keymgr stub).
* `dwarfdump exe` reads the embedded user CU.
* `dsymutil` on a single-step exe is not the intended flow
  (there's no OSO chain to consolidate from); behavior unchanged.

This split matches reality: the dsymutil flow is a two-step
workflow, single-step is for quick `dwarfdump exe` tests.

## Findings worth remembering

* **dsymutil mirrors the exe's segment layout into the `.dSYM`.**
  If the exe has a `__DWARF` segment, the `.dSYM` will too —
  with the same section names and size 0, even if dsymutil also
  appends its own consolidated `__DWARF`. dwarfdump and gdb both
  iterate segments in load-command order and stop at the first
  match, so the appended-second `__DWARF` is unreachable. The
  contract is: **don't ship a `__DWARF` in a linked exe that's
  destined for `dsymutil` consumption** (gcc-4.0's exe doesn't,
  for the same reason).
* **The keymgr stub is the only link-time `tcc_compile_string`
  call on Darwin.** Suppressing `do_debug` around it is enough to
  zero out the link-time `.debug_*` sections in a two-step build.
  No other link-time path writes debug info.
* **The session-078 handoff's `<string>` CU was item #2** (a
  "sharp edge if anyone reads DWARF from the linked exe") and its
  effect on the dsymutil flow was assumed harmless. It wasn't —
  it was the entire cause of the empty `.dSYM`. The lesson:
  carry-over items can hide load-bearing bugs.

## Files touched

* [`tcc/tccelf.c`](../../../tcc/tccelf.c) — 35-line block
  inserted around the keymgr-stub `tcc_compile_string`. Saves
  `s->do_debug`, clears it, restores after. The comment block
  explains why (the dsymutil layout issue), what (no DWARF for
  the stub), and what the consequence is (no `__DWARF` segment
  in two-step linked exes).
* [`demos/v0.2.57-strip-uuid-dsym.sh`](../../../demos/v0.2.57-strip-uuid-dsym.sh)
  — updated the `__DWARF before __LINKEDIT` check to accept
  either layout. Pre-v0.2.58: `__DWARF` before `__LINKEDIT`.
  Post-v0.2.58: no `__DWARF` at all. Both satisfy strip's
  end-of-file invariant. Other assertions unchanged.

## Files added

* [`demos/v0.2.58-dsym-source-lines.c`](../../../demos/v0.2.58-dsym-source-lines.c)
  — main TU; calls `helper_add` + `helper_mul` then prints
  `sum=N prod=N argc=N`.
* [`demos/v0.2.58-dsym-source-lines-helpers.c`](../../../demos/v0.2.58-dsym-source-lines-helpers.c)
  — helpers TU.
* [`demos/v0.2.58-dsym-source-lines.sh`](../../../demos/v0.2.58-dsym-source-lines.sh)
  — driver. Builds two-step, asserts no `__DWARF` in exe, runs
  dsymutil, asserts the `.dSYM` has exactly one `__DWARF`,
  asserts dwarfdump reads `TAG_compile_unit`, asserts both
  source paths appear as `AT_name`, runs `strip -S`, smoke-runs
  the stripped exe, scripts gdb to `list main` / `list
  helper_add` / `break <file>:<line>` against the `.dSYM`.

## Regression

* tests2: **111 / 111** (NORUN, -o exe path).
* abitest cc+tcc: **48 / 48 successes**.
* Bootstrap fixpoint (`FIXPOINT=1 scripts/bootstrap-tcc-self.sh`):
  **HOLDS**.
* v0.2.37–v0.2.58 debug demos (all twelve): green.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

None.

## Open items

The session-078 handoff still has a few carried items not touched
here:

* (068 #2) Extend self-link diagnostics to `macho_output_dylib`.
* (068 #3) Post-write linter (`otool` / `nm` sanity over emitted
  bytes).
* (068 #4) `-vv` diagnostic for the reader's silent dropped
  relocs.
* (067 #3) Sibling r11 watch (no surface yet).
* AltiVec intrinsics.
* bcheck.c port.
* (071) ibookg37 `--builtins+bitfields` generator-host validation
  (low priority — the default-opts sweep already validated it).
* (069) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on
  imacg3 (this session counted as one more clean rebuild cycle).

Roadmap #7.5 (Phase 2) is now fully done. The natural next-arc
candidates are listed in the session-078 handoff #3.

## Side note — rsync exclude list

While syncing changes to imacg3, I noticed my `rsync --exclude`
list (`tcc/tcc`, `tcc/*.o`, `tcc/libtcc.a`, etc., copied from the
session-039 handoff) didn't exclude `tcc/c2str.exe`. uranium's
working tree has an arm64 build of `c2str.exe` (built locally for
testing on macOS), and rsync shipped it to imacg3 where it
overwrote the PowerPC build artifact and made the next `make`
fail with "cannot execute binary file". Re-ran with the better
exclude list (`tcc/*.exe`, `tcc/tccdefs_.h`, `tcc/lib/*.o`,
`tcc/lib/**/*.o`) and the build went through. This is a sync
script polish opportunity if/when it becomes a recurring pain —
not load-bearing for now.
