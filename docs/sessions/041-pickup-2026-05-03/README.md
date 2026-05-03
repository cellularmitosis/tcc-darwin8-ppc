# Session 041 — pickup 2026-05-03 (unsupervised)

## Arrival state

- HEAD: `7f4ab95` (matches `origin/main`).
- Latest release: **v0.2.13-g3**.
- tests2: 110 / 111 (99.1%) under `-o exe` path.
- Lua 5.4.7 builds and runs. sqlite3 builds, `-version` works,
  `select 1+1` crashes.
- Bootstrap fixpoint holds.

The handoff doc from 040 lists the open work in priority order:
1. **PPC32 shift-count >= 32 hint** — quick experiment, may or may
   not be the same bug surfacing in sqlite + bitfield.
2. **96_nodata_wanted BE bitfield bug** — only failing test;
   high-confidence localized fix in `tccgen.c::struct_layout`.
3. **sqlite3_open crash** — high impact, likely a single codegen
   bug. Diagnosis scaffolding in place.

## Goals

- Continue toward "robust self-hosted tcc on darwin8/ppc".
- Cut releases as features land (with demos/benchmarks where
  meaningful).
- Document everything; keep READMEs current.

## Plan (will evolve)

1. Verify bootstrap + tests2 on ibookg37 (sanity).
2. Probe the PPC32 shift-count hypothesis in isolation. Cheap.
3. Pick the more tractable of (96 bitfield) vs (sqlite3_open) and
   land a fix. Re-bench, demo, release.
4. Loop.
