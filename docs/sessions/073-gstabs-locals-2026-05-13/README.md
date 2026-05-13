# Session 073 — gstabs N_PSYM / N_LSYM stack offsets (2026-05-13)

## Arrival state

End of [session 072](../072-oso-stab-gdb-tiger-2026-05-13/README.md).
tcc-darwin8-ppc is shipped through **v0.2.51-g3**:

* tests2 111/111 (100%) since v0.2.14.
* Full DWARF in `__DWARF` segment (v0.2.37–.39) — dwarfdump / lldb
  read it directly.
* `tcc -gstabs` produces an interactively-debuggable Mach-O on
  Tiger (v0.2.51): file:line breakpoints, source listing, stack
  unwinding with file:line per frame, `print <global>` all work
  end-to-end on Tiger gdb 6.3.

Open Phase 2 polish items from session 072's HANDOFF:
  1a. align tcc's `N_PSYM` / `N_LSYM` stack offsets with the PPC
      backend's actual frame layout — the highest-value item, since
      v0.2.51 without it is "you can step but you can't see your
      values."
  1b. emit `N_OSO` pointing at a real `.o` file path (enables
      `dsymutil` round-tripping).
  1c. emit `N_BNSYM` / `N_ENSYM` markers around function bodies
      (cosmetic).
  1d. decide whether to default `-g` to stabs on Darwin.

This session tackles **item 1a**.

## What was done

### Empirical groundwork: how gcc-4.0 vs tcc emit PSYM/LSYM today

Before writing code, ran a reference experiment on imacg3:
compiled the same 13-line C file with `gcc-4.0 -gstabs` and
`tcc -gstabs`, dumped `nm -ap` on each, scripted gdb to
`break helper`, `print x`, `print y`, `info frame`,
`info registers r1 r31`.

For a function `int helper(int x, int y)`:

| | gcc-4.0 | tcc (pre-fix) |
|---|---|---|
| `PSYM x` value | `0x58` (= 88) | `0x18` (= 24) |
| `PSYM y` value | `0x5c` (= 92) | `0x1c` (= 28) |
| `LSYM prod` value | `0x18` (= 24) | `0xfffffff0` (= -16) |
| gdb `print x` | `1` ✓ | garbage |

Cross-checked the gdb interpretation:
* gcc binary: r1 = `0xbffffaa0`, "Arglist at 0xbffffaa0". gdb reads
  `PSYM x` as r1 + 88. Caller's SP was at `0xbffffae0`; caller's
  parameter save area (where the incoming r3 spill lives) was at
  `caller_SP + 24 = 0xbffffaf8 = r1 + 88`. ✓ gdb reads x correctly.
* tcc binary: r1 = `0xbffffa20`, r31 = `0xbffffab0`, frame_size = `0x90`
  (= 144). r31 = r1 + 144 = OLD_SP, per our prolog
  (`addi r31, r1, frame_size`). tcc stores `PSYM x = 24` = the
  r31-relative offset; gdb interprets it as r1 + 24 which lands in
  tcc's own outgoing parameter area. Garbage.

**Finding:** Tiger gdb 6.3 on PPC interprets stabs `N_PSYM` /
`N_LSYM` offsets as **r1 (post-prolog SP) relative**, matching the
gcc-4.0 convention. tcc emits them **r31 (FP) relative**, matching
the PPC frame layout described in `ppc-gen.c::gfunc_prolog`'s
comment block but disagreeing with gdb's interpretation.

Since the PPC prolog sets `r31 = r1 + frame_size`, the conversion
is exact and trivial: add frame_size to every `N_PSYM` /
`N_LSYM` value before emission.

### Change: shift PSYM/LSYM values by frame_size in tccdbg.c

Modified `tccdbg.c::tcc_debug_finish` (the non-DWARF branch).
`tcc_debug_finish` is called from `tcc_debug_funcend`, which
runs **after** `gfunc_epilog` (see tccgen.c's `gen_function`
ordering: `tcc_debug_end_scope` populates `debug_info->sym` →
`tcc_debug_prolog_epilog` → `gfunc_epilog` (sets
`ppc_last_frame_size`) → `tcc_debug_funcend` (emits the stored
entries)). So by the time we emit, `ppc_last_frame_size` is the
finalized frame size for this function.

The new code, gated by `#if defined(TCC_TARGET_PPC) &&
defined(TCC_TARGET_MACHO)`:

```c
unsigned long emit_value = s->value;
if (s->type == N_PSYM ||
    (s->type == N_LSYM && s->value != 0))
    emit_value += ppc_last_frame_size;
```

The nonzero guard on `N_LSYM` is important: type-def stabs
(struct fields, typedefs) emitted by `tcc_get_debug_info` while
inside the function also use `N_LSYM`, with `value == 0` — those
must not be shifted. The guard is safe because real PPC param
offsets start at +24 (the caller's parameter save area, where the
prolog spills r3..r10) and real PPC local offsets start at -12
(loc=-4 reserved for the saved-r31 slot, loc=-8 for the
saved-r30 PIC base), so the value is never zero for a real
stack-allocated entry.

DWARF is untouched — its `DW_OP_fbreg <s->value>` is already
evaluated against a frame-base of `DW_OP_call_frame_cfa`, and
v0.2.39's per-prolog CFI already sets `CFA = r1 + frame_size`.
So DWARF offsets resolve to `r1 + frame_size + s->value` =
`r31 + s->value` automatically.

The change is +27 lines in one function in tccdbg.c. No new
exports; `ppc_last_frame_size` is already in `tcc.h` (introduced
in v0.2.39 for the per-prolog CFI block in the same file).

### Demo

[`demos/v0.2.52-gstabs-locals.{c,sh}`](../../../demos/) — three
functions (`add3`, `with_local_and_global`, `many_locals`) each
with several parameters and several locals computed from them;
the shell wrapper builds with `tcc -gstabs`, runs the program
once for output sanity (`r1=60 r2=310 r3=-83`), then for each
function scripts gdb to break, `print` every param, `next`
through each local-assignment statement, `print` each local, and
asserts the values against hand-computed expectations.

Final assertion: `bt` at a breakpoint inside `add3` shows
`add3 (a=10, b=20, c=30)` AND `main (argc=1, argv=0xbffffbe0)`
— both with real param values, not garbage. (Previously the
backtrace also went wrong because each frame's arguments were
read through the same broken offsets.)

## What works (Phase 2 item 1a)

Verified on imacg3 with the freshly-built tcc:

* `print x` inside a function returns the actual passed value.
* `print local_var` returns the actual stored value (uninitialized
  before the assignment line, correct after stepping past it).
* `bt` / `where` displays argument values for every frame.
* The v0.2.51 demo still passes — and now also gets the bonus of
  real argument values in its backtrace frames.

Tested across:
* Function with three int params and two int locals (`add3`).
* Function with one param using both globals and locals
  (`with_local_and_global`).
* Function with two params and five locals (`many_locals`).
* `main(int argc, char **argv)` — argc + pointer-to-pointer
  shows up correctly in `bt`.

## What doesn't work yet (Phase 2 remaining)

* **`N_OSO` chain pointing at real .o files** for `dsymutil`
  round-tripping. Empty N_OSO (current state) matches gcc-4.0's
  `gcc -g hello.c -o hello` behavior and Tiger gdb is fine with
  it, but a proper N_OSO would let users produce standalone
  `.dSYM` bundles via `dsymutil`. Requires threading source-file →
  object-file paths through tcc's compile/link sequence; the direct
  `tcc -gstabs foo.c -o foo` flow has no intermediate .o on disk.
* **`N_BNSYM` / `N_ENSYM` markers** around function bodies. Apple-
  conventional, optional — would help gdb 6.3's prolog-skip
  heuristic land `break <function>` exactly at the function entry
  rather than a few instructions in. Cosmetic.
* **Defaulting `-g` to stabs on Darwin**? Currently `-g` defaults
  to DWARF-2 (configured in v0.2.39). DWARF gives lldb / later-
  macOS step-debugging; stabs gives Tiger gdb step-debugging.
  Worth a separate session and user-facing announcement.

## Regression check

* tests2: **111 / 111 PASS** (same as v0.2.51 baseline).
* abitest-cc: **24 / 24 success**.
* abitest-tcc: **24 / 24 success**.
* Bootstrap fixpoint (`tcc → tcc-self → tcc-self2 → tcc-self3`
  with `tcc-self2.o == tcc-self3.o`): **HOLDS**.
* Sample release demos (v0.2.39 eh_frame, v0.2.44 gdebug-link,
  v0.2.50 self-link-diagnostics, v0.2.51 gstabs-oso-stab): all PASS.

## Exit state

* HEAD: this session's commit.
* Tree: `tcc/tccdbg.c` (+ one PPC+MACHO branch in
  `tcc_debug_finish`), `demos/v0.2.52-gstabs-locals.{c,sh}`,
  `docs/roadmap.md` (v0.2.52 row + #7.5 / risk register updates),
  `demos/README.md` (v0.2.52 row).
* Version: **v0.2.52-g3** released.

## Open work for next session

See [HANDOFF.md](HANDOFF.md).
