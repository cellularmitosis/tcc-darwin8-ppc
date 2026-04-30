# 000 — Source survey

Pick a TCC base to start from, and figure out roughly what we're
signing up for.

## Arrival state

Empty repo. Just a freshly-drafted [CLAUDE.md](../../../CLAUDE.md) and
this `docs/sessions/000-source-survey/` dir. No source pulled, no
roadmap, no decisions made.

The premise going in was:

> "we will be implementing a darwin8 ppc backend for tcc, the tinycc
> compiler"

with three candidate bases on the table:

1. TCC 0.9.25 — Feb 2009, Bellard's last release.
2. TCC 0.9.27 — Dec 2017, last formal community release.
3. mob branch — current community development tip on repo.or.cz.

User flagged a licensing concern up front: contributors had historically
added backends under different licenses, and there was a relicensing
push (possibly to MIT) with some holdouts.

## What was done

Pulled all three trees into `~/tmp/tcc-survey/{bellard,v0927,mob}`
(originally `/tmp/`, moved mid-session so they'd survive reboot).
Compared them along six axes — see [findings.md](findings.md) for the
full report.

Headline discoveries:

- **TCC has never had a PPC backend.** None of the three trees contain
  `ppc-gen.c` or `ppc-link.c`. The `configure` script has a vestigial
  `ppc` cpu-detection branch but no code generator behind it. This
  reframes the project: we are *writing* a backend from scratch, not
  *porting* one.
- **`tccmacho.c` exists in mob (2,476 lines)** but is hardcoded for
  x86_64 and arm64. Adding 32-bit PPC Mach-O emission is a
  significant — but bounded — chunk of work on top of the codegen.
- **Licensing is workable.** mob has a `RELICENSING` file listing 27
  contributors who agreed to MIT-compatible relicensing. New code we
  write can be MIT. Caveat: `arm-gen.c` (Daniel Glöckner) is an
  LGPL-only holdout and must not be copied from. `arm64-gen.c` (Edmund
  Grimley Evans, MIT-relicensed) is the safe stylistic reference.
- **Closest precedent for adding a backend: `riscv64-gen.c`** — the
  most recent backend, written after the per-arch modularization
  (`*-gen.c` / `*-link.c` / `*-asm.c` / `*-tok.h`).
- **No prior tcc-on-darwin-PPC art** in mob's git log or public web
  searches.

## Decisions

- **Base: mob branch.** Reasons: only tree with the per-arch modular
  layout, only tree with a real `tccmacho.c`, only tree with
  `--targetos=Darwin` build support. The "8 years stale" concern that
  applied to 0.9.27 doesn't apply here — mob is the active head. And
  since we're pulling in-tree (no patch-set workflow), volatility is a
  one-time snapshot cost.
- **No `patches/` workflow.** Confirmed by absence of any base that
  would make patch-rebasing useful.

## Exit state

- Three TCC trees parked at `~/tmp/tcc-survey/` for ongoing reference;
  not in this repo.
- [findings.md](findings.md) captures the full survey with concrete
  file paths and line counts.
- Repo still empty of TCC source. Next session pulls mob in-tree.

## Next session

Session 001 — pull mob source into `tcc/`, write `docs/roadmap.md`,
get a baseline build of unmodified mob working on uranium so we know
the ground truth before any PPC work begins.
