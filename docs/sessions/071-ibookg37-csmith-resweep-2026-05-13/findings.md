# Findings — session 071 (2026-05-13)

## csmith 2.3.0 generation cost on a G3

ibookg37 (~700 MHz PowerPC G3): **~13.4 s/seed** average for
default-opts csmith generation. 200 seeds took 2682 s (≈44.7 min).
Distribution is bimodal — many seeds produce a sub-second 96-100
line degenerate program, while others produce 1000-3000 line
programs that take 20-30 s each.

For scoping future ibookg37-side generation campaigns:

| Seed count | Wall-clock estimate |
|---|---|
| 50 | ~11 min |
| 100 | ~22 min |
| 200 | ~45 min |
| 500 | ~110 min |
| 1000 | ~3.7 hours |

uranium (M-series Mac running csmith via Homebrew) is roughly
40-50× faster, so for large corpora the "generate on uranium, ship
to PPC" pattern is still right; ibookg37 generation is for "prove
it works locally" runs.

## The csmith differential campaign's skip distribution is 100% gcc-timeout

Across both session 066's baseline (1000 seeds, default opts on
ibookg37 with uranium corpus, SKIP=127) and this session's run (200
seeds, default opts on ibookg37 with ibookg37-native corpus,
SKIP=24), **every single skip was attributed to `gcc-timeout`** —
i.e. the gcc-4.0 -O0 binary took longer than the campaign script's
`RUN_TIMEOUT=15` perl-alarm cap.

What this means in practice:

* The campaign's other skip categories (`gcc-compile-fail`,
  `tcc-timeout`) effectively never trigger at default opts. So
  the skip rate is **purely a function of csmith's tendency to
  generate occasional infinite-loop or near-infinite-loop
  programs**, not a function of compiler quality on either side.
* To reduce the skip rate, the only useful lever is the
  `RUN_TIMEOUT` value (or pre-screening csmith seeds for known
  loop signatures). Tightening compile flags or fixing
  hypothetical gcc-side compile failures wouldn't move the
  needle.
* Conversely, the skip rate carries no diagnostic signal about
  tcc — tcc never times out on default-opts csmith output on a G3.
  (The 1s/seed-ish tcc compile + run pattern is consistent across
  every PASS in both runs.)

This is a useful invariant: when comparing two default-opts
campaign runs, **PASS+SKIP** should roughly equal each other
modulo timing jitter, and any FAIL is a real codegen divergence.
