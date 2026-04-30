# 001 — Fork comparison

Deeper dive into how the three TCC trees diverge architecturally and
behaviorally, beyond just "newer." The 000 survey picked mob; this
session is about *understanding what mob actually is* before we start
hacking on it.

## Arrival state

After 000, mob was chosen as the base but we knew very little about
its character beyond "has `tccmacho.c`, has per-arch modular layout."
The trees still parked at `~/tmp/tcc-survey/{bellard,v0927,mob}`.
No source pulled in-tree yet.

## What was done

Delegated a wide-scope comparison across six axes (architecture,
features, performance, maintenance, community perspective, before/after
diffs) to a subagent. Findings written to [findings.md](findings.md)
(688 lines). Highlights below.

### Each fork's personality

- **Bellard 0.9.25 (Feb 2009)** — lean single-pass compiler, monolithic
  layout, x86/ARM/C67 only, 6 tests, optimized for compile speed over
  feature breadth.
- **v0927 0.9.27 (Dec 2017)** — community consolidation. Major
  modularization (`*-gen.c` / `*-link.c` / `*-asm.c`), TinyAlloc fast
  allocator, ARM64 added, RISC-V scaffolding, VLA + C99 features,
  237 tests. Retains Bellard's speed priorities.
- **mob 0.9.28-dev (active, last commit Apr 2026)** — modern community
  tip. Native Mach-O (`tccmacho.c`, 2,476 lines), RISC-V64, DWARF
  debug, bounds-check backtraces, partial C11 (`_Static_assert`,
  `_Generic`, `__attribute__((cleanup))`), 328 tests.

All three remain *architecturally conservative*: single-pass, simple
symbol tables, no IR. Nobody added an optimizer. The "TCC is fast" DNA
is intact.

### Architectural diff highlights

- **Per-arch file layout solidified between Bellard and v0927.** What
  was one `*-gen.c` per arch became up to five files (`-gen`, `-link`,
  `-asm`, `-tok.h`, `-asm.h`). mob continued this trend — RISC-V
  shipped with the full split from day one. **This is the template
  for our PPC work.**
- **`SymAttr` was extracted from `Sym` in v0927** — visibility,
  alignment, binding metadata moved out of the core symbol struct.
  Useful for ABI work (calling convention, alignment markup).
- **`tccelf.c` lost responsibility for per-arch relocations** — they
  moved into `*-link.c`. Same pattern we'll need for `ppc-link.c`.
- **DWARF support is mob-only** (`tccdbg.c`). Optional for our v1.
- **Header growth: `tcc.h` 766 → 1,660 → 2,016 lines.** Most of the
  growth is feature flags and per-arch token decls.

### Performance picture

No evidence of regression. The README slogans ("7x faster than gcc -O0"
in Bellard, "10x" in mob) reflect GCC's `-O0` getting slower over time,
not TCC getting faster. Subagent's verdict, with which I agree: compile
speed flat to <5% slower; codegen quality flat (no optimizer was ever
added or expanded); memory usage flat to +10% (debug info).

No published head-to-head benchmarks exist between the three.

### Maintenance signals

- mob is *alive* — last commit Apr 28, 2026. Mailing list active,
  patches landing.
- FIXME density flat across all three (~1 per 315 LOC) despite 2.2x
  growth — debt isn't accumulating faster than it's being addressed.
- Test count grew 6 → 237 → 328. Real regression harness now exists,
  which we can use.
- Distros (Debian, Arch, Alpine) ship 0.9.27 as their default —
  **mob is "dev tip," not "stable release."** We accept that.

## Decisions

- **No change to base.** mob remains the right choice; the comparison
  reinforces it.
- **Reference style: `arm64-gen.c` for license-clean stylistic copying;
  `riscv64-*.c` for "what does a freshly-added backend look like."**
  Avoid `arm-gen.c` (Glöckner LGPL holdout, can't copy from).
- **DWARF debug support is out of scope for v1.** Mach-O + working
  codegen is enough for first release; debug info layered later.

## Exit state

- Two-session-deep understanding of what mob is. We now know what kind
  of compiler we're hacking on, where the modularization seams are,
  what's optional vs core, and what doesn't exist (any optimizer, any
  PPC code, any prior darwin-PPC work).
- [findings.md](findings.md) is the canonical reference — anything in
  the README above is a digest.
- Repo still has no TCC source. Next session pulls mob in-tree.

## Next session

Session 002 — pull mob source into `tcc/`, write `docs/roadmap.md`,
and get a baseline build of unmodified mob working on uranium.
Establish ground truth before any PPC-specific work begins.
