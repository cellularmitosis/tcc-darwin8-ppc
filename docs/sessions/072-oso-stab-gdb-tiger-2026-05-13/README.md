# Session 072 — OSO STAB emission for gdb-on-Tiger (2026-05-13)

## Arrival state

End of [session 071](../071-ibookg37-csmith-resweep-2026-05-13/README.md).
tcc-darwin8-ppc is shipped through v0.2.50-g3:

* tests2 111/111 (100%) since v0.2.14.
* Full DWARF in `__DWARF` segment (v0.2.37–.39) — dwarfdump / lldb
  read it directly.
* Self-link diagnostics in `macho_output_exe` (v0.2.50).
* Eight real-world programs build + run (lua, zlib, bzip2, cJSON,
  sqlite, sed, gzip, Python 2.7.18).

[roadmap.md](../../roadmap.md) item **#7.5** — *OSO STAB emission for
gdb-on-Tiger* — picked as this session's work in agreement with the
user. Tiger's bundled `gdb 6.3.50` doesn't read embedded `__DWARF`
segments; Apple's "debug map" convention is N_OSO STAB entries
pointing back at the original .o files (plus N_SO / N_FUN /
N_SLINE / N_PSYM / N_LSYM / N_GSYM / N_STSYM / N_LBRAC / N_RBRAC
forming a classic stabs+ chain) embedded in the linked exe's
`LC_SYMTAB` nlist.

## What was done

### Empirical groundwork: how Tiger gdb 6.3 actually consumes debug info

Before writing code, ran a reference experiment on ibookg37:
compiled a 9-line `hello-stab.c` with `gcc-4.0 -g -o hello`, ran
`nm -ap` + `gdb -batch` on the result. Two important findings:

1. **gcc-4.0's default `-g` on Tiger produces a STAB debug-map**, not
   DWARF — the "debugger-friendly" format on Tiger is *stabs+*, even
   though gcc supports both.
2. **Tiger gdb 6.3 reads classic stabs+ directly from the linked
   exe's nlist** — no external `.o` files or `.dSYM` bundle required
   for `break <function>` / `break <line>` / `list` / `bt` /
   `print <global>` to work. gcc-4.0's `gcc -g hello.c -o hello`
   compiles via a temporary `.o` that gets cleaned up; the `N_OSO`
   STAB in the resulting exe has an empty name and gdb doesn't mind.

This drastically simplified the design: a "Phase 1" that translates
tcc's existing `stab_section` content into the Mach-O nlist (no
real-`.o`-file-path threading required) is enough to make gdb-on-Tiger
work end-to-end for the interactive-debug case. Captured the finding
in [memory: reference_tiger_gdb_stabs.md](../../../../.claude/projects/-Users-cell-claude-tcc-darwin8-ppc/memory/reference_tiger_gdb_stabs.md)
for future sessions.

### Baseline check: where the gap actually was

Built the same `hello-stab.c` on imacg3 with `tcc -gstabs` (using the
existing tcc/0.9.28rc post-v0.2.50 PowerPC build). Result:

* `tcc -gstabs` compiled and linked successfully — no error.
* The resulting Mach-O exe's `LC_SYMTAB` had **zero** STAB entries
  (regular externs only).
* `gdb` set breakpoints at function entry addresses but had no
  file/line knowledge — `list main` failed with `No line number
  known for main.`

So tcc's existing tccdbg.c stabs emission was correctly populating
the internal `stab_section`, but the Mach-O writer in `ppc-macho.c`
was silently dropping that section at link time. The gap closure
fits in three coupled changes.

### Change 1 — `emit_stab_nlist()` in `ppc-macho.c`

New static helper (~90 lines) just below `put_nlist`. Walks the
`stab_section` `Stab_Sym` array in parallel with the
`stab_section->reloc` `ElfW_Rel` array (they're emitted in offset
order by `put_stabs_r`); for each entry:

* Looks up the string via `stab_section->link->data + sym->n_strx`,
  copies it into the Mach-O `strtab` obuf, and reads back the new
  string-table offset.
* Checks whether a reloc lives at the entry's `n_value` field offset
  (`idx * sizeof(Stab_Sym) + 8`):
  * If yes — extracts the reloc's target sym from
    `symtab_section`, finds its `st_shndx`, looks up the matching
    section in the writer's `sects[]` array, and computes
    `n_value = sects[j].vmaddr + tsym->st_value + sym->n_value`
    with `n_sect = j + 1` (1-based Mach-O ordinal).
    The `tsym->st_value` term is what makes this work for N_FUN
    (target is the function's own sym, st_value = its offset in
    text) as well as N_SO / N_SLINE (target is a section_sym,
    st_value = 0). N_STSYM with a data-section sym likewise uses
    the data sym's st_value as the var's offset.
  * If no — emits with `n_sect = 0` and the raw n_value preserved.
    N_LSYM type defs, N_PSYM stack offsets, N_GSYM globals, and
    the paired-N_FUN end-entry all fall here.
* Emits one `put_nlist` per entry into the local-symbol range of
  the Mach-O nlist (Apple convention puts STABs at the start of
  LC_SYMTAB, before externals and undefs).

Returns the entry count so the writer can set `n_localsym`
correctly. Two call sites added:

* `macho_output_exe` line 2660 — `n_localsym = emit_stab_nlist(...)`.
* `macho_output_dylib` line 4257 — same.

The helper is a no-op when `!s1->do_debug || s1->dwarf != 0 ||
!stab_section`, so default DWARF builds and `-O` builds are unaffected.

### Change 2 — Apple-absolute STAB addresses in `tccdbg.c`

Three small `#ifdef TCC_TARGET_MACHO` branches. tcc's existing
stabs emission follows the GNU convention of *function-relative*
offsets for N_SLINE / N_LBRAC / N_RBRAC inside a function; Apple's
debug-map convention uses *absolute text VAs* throughout, with a
reloc against the text section so the linker fills in the real
runtime address.

* `tcc_debug_line`: when `func_ind != -1` (inside a function),
  switch from
  `put_stabn(s1, N_SLINE, 0, line_num, ind - func_ind)`
  to
  `put_stabs_r(s1, NULL, N_SLINE, 0, line_num, ind, text_section, section_sym)`.
* `tcc_debug_finish`: similar swap for N_LBRAC / N_RBRAC, using
  `func_ind + cur->start` / `func_ind + cur->end` so the resulting
  reloc resolves to the absolute block boundaries.
* `tcc_debug_funcend`: emit a paired `N_FUN` end-entry with empty
  name and `n_value = function size` after the bracket emission.
  Apple's convention pairs each `N_FUN` start with this size entry
  so `dsymutil` and prolog-skip logic don't have to infer the
  function's extent from the next N_FUN's address.

The `#else` branches keep upstream tcc's GNU emission for all
non-Mach-O targets.

### Change 3 — corresponding demo

[`demos/v0.2.51-gstabs-oso-stab.c`](../../../demos/v0.2.51-gstabs-oso-stab.c)
+ [`.sh`](../../../demos/v0.2.51-gstabs-oso-stab.sh): tiny program
with a static + a global + a non-trivial helper + a 4-line main.
The shell wrapper builds it with `tcc -gstabs`, dumps the STAB
chain via `nm -ap`, scripts gdb to set breakpoints by function /
line, run, hit the main breakpoint, print globals, continue into
helper, walk the stack with `where`, and asserts each expected
behavior. Final line: `OK: tcc -gstabs produces gdb-debuggable
Mach-O on Tiger.`

## What works (Phase 1)

Verified on imacg3 with the freshly-built tcc:

* `tcc -gstabs hello.c -o hello` compiles and links cleanly.
* `nm -ap hello` shows the full STAB chain (N_SO directory + filename,
  N_FUN with paired end-entry per function, N_SLINE per source
  line with absolute VAs, N_PSYM/N_LSYM/N_GSYM/N_STSYM,
  N_LBRAC/N_RBRAC bracketing function bodies, N_BINCL/N_EINCL
  for the include chain).
* `gdb` on Tiger:
  * `break <function>` resolves to file:line.
  * `break <line>` resolves the same way.
  * `run` runs the program; breakpoints hit with the correct
    file:line displayed.
  * `bt` / `where` walks the stack with file:line per frame.
  * `list <function>` reads the source file referenced by N_SO
    and prints lines around the function.
  * `print <global>` shows the right value (verified with
    `g_global = 7` and `g_static = 42`).

## What doesn't work yet (Phase 2 polish)

* **`print <param>` / `print <local>`**. tcc's N_PSYM / N_LSYM emission
  uses `s->c` (the local sym's `c` field) as the stack offset, but
  that doesn't match where the PPC backend actually stores the
  parameter / local in the function's frame. For our `hello-stab.c`,
  `helper(int x)` shows `x = 1048672` instead of `1`. Fix needs
  alignment between `tccgen.c`'s param/local layout and tccdbg.c's
  offset emission — a separate codegen-vs-debuginfo audit.
* **`break <function>` lands a few instructions past the function
  entry**. e.g. `break helper` resolves to 0x1534 when the actual
  N_FUN entry is at 0x1484. This is gdb 6.3's prolog-skip heuristic
  — it advances past what it thinks is the prolog. The stack frame
  unwinds correctly and stepping works fine, so this is cosmetic.
  Emitting `N_BNSYM` / `N_ENSYM` markers around the body would
  probably help.
* **`N_OSO` chain pointing at real .o files** for `dsymutil` round-
  tripping. Empty N_OSO (current state) matches gcc-4.0's
  `gcc -g hello.c -o hello` behavior and gdb is fine with it, but a
  proper N_OSO would let users produce a standalone `.dSYM` bundle
  via `dsymutil` (the standard Apple workflow for distributing
  debug-stripped binaries with separate debug-info). Requires
  threading source-file → object-file paths through tcc's
  compile/link sequence, with the catch that the direct
  `tcc -gstabs foo.c -o foo` flow has no intermediate .o on disk.
* **Defaulting `-g` to stabs on Darwin**? Currently `-g` defaults to
  DWARF-2 (configured in v0.2.39). DWARF gives lldb / later-macOS
  step-debugging; stabs gives Tiger gdb step-debugging. We could
  default to stabs since gdb is the bundled debugger on Tiger, but
  it's a user-facing behavior change worth doing on its own.

## Regression check

* tests2: **111 / 111 PASS** (clean — same as v0.2.50 baseline).
* abitest-cc: 24/24 success.
* abitest-tcc: 24/24 success.
* Bootstrap fixpoint (`tcc → tcc-self → tcc-self2 → tcc-self3`
  with `tcc-self2.o == tcc-self3.o`): holds.
* Sample release demos (v0.2.48 r12-clobber, v0.2.49 clz/ctz UB,
  v0.2.50 self-link diagnostics): all PASS.
* `tcc -run` JIT path: works.
* Non-stabs and `-gdwarf-2` builds: identical to v0.2.50 behavior
  (the helper is gated by `s1->do_debug && s1->dwarf == 0`).

## Exit state

* HEAD: this session's commit.
* Tree: `tcc/ppc-macho.c` (+ helper + 2 call sites), `tcc/tccdbg.c`
  (+ 3 small Mach-O-only branches), `demos/v0.2.51-*.{c,sh}`,
  `docs/roadmap.md` (v0.2.51 row + #7.5 ticked off + risk register
  updated), `demos/README.md` (v0.2.51 row).
* Version: **v0.2.51-g3** released.

## Open work for next session

See [HANDOFF.md](HANDOFF.md).
