# Session 074 — N_BNSYM / N_ENSYM function-body markers (2026-05-13)

## Arrival state

End of [session 073](../073-gstabs-locals-2026-05-13/README.md).
tcc-darwin8-ppc is shipped through **v0.2.52-g3**:

* tests2 111/111 (100%) since v0.2.14.
* Full DWARF in `__DWARF` segment (v0.2.37–.39) — dwarfdump / lldb
  read it directly.
* `tcc -gstabs` produces an interactively-debuggable Mach-O on
  Tiger (v0.2.51 — file:line breakpoints, source listing, stack
  unwinding, `print <global>`; v0.2.52 — `print <param>` /
  `print <local>` with correct SP-relative offsets, including
  stepped values and `bt` argument display).

Open Phase 2 polish items from session 073's HANDOFF:
  1b. emit `N_OSO` pointing at a real `.o` file path (enables
      `dsymutil` round-tripping).
  1c. emit `N_BNSYM` / `N_ENSYM` markers around function bodies.
  1d. decide whether to default `-g` to stabs on Darwin.

User picked **item 1c** for this session.

## What was done

### Implementation: two `put_stabs_r` calls + two stab.def entries

Apple's `<mach-o/stab.h>` defines:

```
#define N_BNSYM 0x2e    /* begin nsect sym: 0,,n_sect,0,address */
#define N_ENSYM 0x4e    /* end nsect sym:   0,,n_sect,0,address */
```

Both added to `tcc/stab.def` so they're available through the
`enum n_type_t` defined in `tcc/stab.h`.

`tccdbg.c::tcc_debug_funcstart`, in the non-DWARF / Mach-O branch
(right before the opening `N_FUN` entry):

```c
#ifdef TCC_TARGET_MACHO
    put_stabs_r(s1, NULL, N_BNSYM, 0, 0, func_ind,
                text_section, section_sym);
#endif
```

`tccdbg.c::tcc_debug_funcend`, in the Mach-O branch (right after
the closing N_FUN size-entry):

```c
#ifdef TCC_TARGET_MACHO
    put_stabs(s1, NULL, N_FUN, 0, 0, size);
    /* paired N_ENSYM marker */
    put_stabs_r(s1, NULL, N_ENSYM, 0, 0, func_ind + size,
                text_section, section_sym);
#endif
```

Both reloc against `text_section` / `section_sym` (mirroring the
existing `N_LBRAC` / `N_RBRAC` pattern) so `emit_stab_nlist` in
`ppc-macho.c` resolves the addends to actual VAs at link time. The
existing emitter is type-agnostic — it walks the stab table and
forwards `n_type` / `n_other` / `n_desc` / `n_value` through to
the nlist via the parallel reloc table — so no Mach-O emitter
changes were needed.

`func_ind` (the current function's start offset in the text
section) is in scope at both emission points. In `funcstart`,
`gen_function` sets `func_ind = ind` just before invoking
`tcc_debug_funcstart`, so the addend resolves to the function's
first instruction. In `funcend`, the helper is called with
`size = ind - func_ind`, so `func_ind + size = ind` = the
function's end address.

### Empirical A/B comparison vs v0.2.52

Before writing the demo, ran a controlled experiment on imacg3:

```sh
# Build v0.2.52 tcc in /tmp/tcc-v0252-test/ from v0.2.52-g3 source.
# Build v0.2.53 tcc from this session's working tree.
# Compile the same minimal helper(x,y) + main() with each, with -gstabs.

cat > /tmp/bnsym_check.c <<EOF
int helper(int x, int y) {
    int z = x + y;
    return z;
}
int main(void) { return helper(7, 11); }
EOF

$TCC_V52 -gstabs ... /tmp/bnsym_check.c -o /tmp/bnsym_v52
$TCC_V53 -gstabs ... /tmp/bnsym_check.c -o /tmp/bnsym_v53
```

Then scripted gdb against each binary and diffed the outputs.

| Probe | v0.2.52 | v0.2.53 |
|---|---|---|
| `nm -ap | grep BNSYM` | 0 entries | 7 entries (5 user fns + 2 keymgr helpers) |
| `nm -ap | grep ENSYM` | 0 entries | 7 entries |
| `break helper` resolves to | `0x1520: file ..., line 2` | `0x1520: file ..., line 2` |
| `info function helper` | `int helper(int, int);` | `int helper(int, int);` |
| `info address helper` | `0x1484` | `0x1484` |
| `info line helper` | `starts at 0x1484 <helper> and ends at 0x1520 <helper+156>` | (identical) |
| `disas helper` | (full disassembly) | (byte-identical disassembly) |

**Finding:** the markers are emitted at correct addresses (BNSYM
== function start, ENSYM == function end, balanced counts), but
**Tiger gdb 6.3 itself shows no observable behavior change**.
gdb's SLINE-based prolog-skip already finds the body's first
source line cleanly without needing BNSYM/ENSYM.

The session-073 HANDOFF had anticipated `break helper` would
resolve a few instructions past entry pre-v0.2.53 because of a
gdb prolog-skip guess — empirically false on Tiger gdb 6.3 on
PPC. So 1c is convention/correctness work rather than an observable
debugger fix.

### Value of landing 1c anyway

1. **Format alignment with gcc-4.0.** tcc's stabs output on
   Mach-O PPC now has the same shape gcc-4.0 produces on the same
   target: `BNSYM` → opening `FUN name:F type` → `SLINE`s →
   `PSYM`/`LSYM`s → closing `FUN ""` (size) → `ENSYM`. Any tool
   that walks stabs sequentially (newer gdb, custom analysis,
   `nm`) sees the conventional structure.

2. **Infrastructure for Phase 2 item 1b (`N_OSO` + `dsymutil`).**
   `dsymutil` walks the per-function stab subrange to scope debug
   info into the `.dSYM` bundle. The expected sequence on Apple
   tooling is BNSYM-bracketed; without the markers, dsymutil
   would have to infer body extents from adjacent `N_FUN` entries
   and the empty-name `N_FUN` size-entry, which works but is
   fragile. When item 1b lands, BNSYM/ENSYM will be the natural
   anchor.

3. **Future-proofing.** Newer debuggers or static analyzers that
   parse classic stabs may use BNSYM/ENSYM rather than the SLINE
   prolog-skip heuristic. Tiger gdb 6.3 happens not to.

The cost is tiny: 2 `put_stabs_r` calls (one in `funcstart`, one
in `funcend`), 2 lines in `stab.def`, and 2 extra stab entries
per function in the linked exe.

### Demo

[`demos/v0.2.53-bnsym-ensym.{c,sh}`](../../../demos/) — a
4-function test program (`tiny`, `small`, `medium`, `stat_helper`,
plus `main`). The script:

1. Builds with `tcc -gstabs`.
2. Runs the binary (sanity check the math).
3. Dumps `nm -ap` filtered to `BNSYM` / `ENSYM` / `FUN` lines
   covering the user functions.
4. Asserts BNSYM count == ENSYM count and >= 5.
5. Asserts each user function's `BNSYM` address equals its
   opening `N_FUN` address (via an awk-walk over the ordered
   stab table).
6. Re-runs the v0.2.52 PSYM/LSYM gdb workflow on this binary's
   `medium(1,2,3)` call as a regression check — params and four
   stepped locals all read the expected values.

### Files edited

* [`tcc/stab.def`](../../../tcc/stab.def) — two new
  `__define_stab(N_BNSYM, 0x2e, "BNSYM")` /
  `__define_stab(N_ENSYM, 0x4e, "ENSYM")` entries with explanatory
  comments.
* [`tcc/tccdbg.c`](../../../tcc/tccdbg.c) — 17 lines added across
  two emission points (`tcc_debug_funcstart` + `tcc_debug_funcend`),
  both inside `#ifdef TCC_TARGET_MACHO` guards.

### Files added

* [`demos/v0.2.53-bnsym-ensym.c`](../../../demos/v0.2.53-bnsym-ensym.c)
* [`demos/v0.2.53-bnsym-ensym.sh`](../../../demos/v0.2.53-bnsym-ensym.sh)
* [`docs/sessions/074-bnsym-ensym-2026-05-13/README.md`](README.md)
* `HANDOFF.md` (this session's exit doc).

### Files updated (docs only)

* [`docs/roadmap.md`](../../roadmap.md):
  - v0.2.53-g3 row added at the top of the release table.
  - Header: "shipped through v0.2.52-g3" → "v0.2.53-g3";
    "Thirty patch releases" → "Thirty-one".
  - Structural item **#7.5** updated to credit v0.2.53 (Phase 2
    item 1c).
  - "Risk register" entry for *No DWARF / no debugger* updated to
    credit v0.2.53.
* [`demos/README.md`](../../../demos/README.md): v0.2.53 row added
  above v0.2.52.

### Memory updates

None this session.

## Regression suite

Run on imacg3 (Tiger 10.4.11 PowerPC G3) against the freshly
rebuilt tcc:

* `bash scripts/run-tests2.sh`: **Total: 111  Pass: 111  Fail: 0**.
* `FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh`: **FIXPOINT
  HOLDS: tcc-self2.o == tcc-self3.o**.
* `cd tcc/tests && /opt/make-4.3/bin/make abitest`: **48 success
  / 0 fail** (24 cc + 24 tcc).
* `bash demos/v0.2.53-bnsym-ensym.sh`: paired-marker assertions
  pass, v0.2.52 regression workflow passes.

## Exit state

* HEAD at session start: `b2febb0` (v0.2.52-g3).
* HEAD at session end: this session's commit (v0.2.53-g3 release
  + docs).
* tag: **v0.2.53-g3**.
* Source changes: `tcc/stab.def` (+10 lines), `tcc/tccdbg.c`
  (+19 lines split across two emission points).
* Demo: `demos/v0.2.53-bnsym-ensym.{c,sh}`.

## Next session

Pick from the remaining Phase 2 polish items (**1b** — `N_OSO`
real-path threading enables `dsymutil`; **1d** — default `-g` to
stabs on Darwin) or one of the carried multi-session arcs (self-
link diagnostics extension to `macho_output_dylib`, post-write
linter, `-vv` reader diagnostic, sibling r11 watch, AltiVec
intrinsics, bcheck.c port).

1b is the highest-value remaining item — it unlocks standalone
.dSYM bundles for the two-step compile-then-link flow that real
build systems use.
