# Session 056 — csmith bug-hunt continuation (2026-05-09)

## Arrival state

Picking up from session 055's open work:

* Bug reduction setup ready at `/tmp/seed92-reduce/`: csmith
  `--float --builtins` seed-92 found a divergence (gcc=7B499BDB,
  tcc=65EE2E44 on `g_99`); creduce interestingness test was wired
  but never run.
* Default-options csmith on imacg3 was clean (173 PASS / 27
  both-timeout / 0 real bugs after the v0.2.45 BE-bitfield fix).
* `--float --builtins` half-used because gcc-4.0 lacks
  `__builtin_bswap32`/`__builtin_bswap64`/`__builtin_ia32_crc32qi`.

## Plan

1. Update imacg3 to HEAD `62511c4` and re-verify baseline.
2. Kick off creduce on seed-92 with parallel workers.
3. While creduce churns (probably hours): generate larger
   default-options seed batch + try `--max-funcs 12` for more
   complex programs.
4. Once seed-92 reduced, root-cause and fix.
5. If time remaining: build builtin compat shim so `--builtins`
   campaigns aren't gated on bswap.

## Live notes

* imacg3 baseline OK: tests2 111/111 at HEAD `62511c4`.
* creduce launched against `/tmp/seed92-reduce2/seed-92.c` with
  4 parallel workers. PID-isolated remote tempfiles. Pace: ~1
  iter/s on 4 workers. Killed once root cause identified —
  manual narrowing was faster on this program (csmith outputs
  function `reads`/`writes` lists + harness has built-in
  per-global hash tracing, so the divergent function was found
  by 2 ssh round-trips and the divergent line by reading the
  source).
* Bug isolated: `func_65` line 379, the assignment chain
  `*l_108 = (..., 0x7.C870D1p+91)` where `*l_108 = &g_99` (an
  `int*`). The float constant `0x7.C870D1p+91` is far beyond
  INT32 range, so the assignment requires float→int conversion.
* Minimal repro (no comma op needed): `int x = (int)0x7.C870D1p+91f;`
  Pre-fix: tcc gives `-1` (0xFFFFFFFF). Post-fix: tcc gives
  `2147483647` (0x7FFFFFFF), matching gcc-4.0.
* See `findings.md` for root cause and fix details.

## Outcome

Shipped **v0.2.46-g3** (HEAD `0aa4690`):

* `tccgen.c::gen_cast` constant-fold branch now emulates PPC
  `fctiwz` saturation when targeting PPC. Saturate positive
  overflow to `0x7FFFFFFF`, negative / NaN to `0x80000000`,
  otherwise round-toward-zero. VT_LLONG keeps existing path
  because runtime there matches host helper already.
* Demo: [`v0.2.46-float-int-const-fold.sh`](../../../demos/v0.2.46-float-int-const-fold.sh).
* tests2 111/111, abitest-cc/tcc 22/22, bootstrap fixpoint
  holds, all 9 demos pass.
* Three csmith campaigns queued sequentially on imacg3
  (background) for post-fix validation across diverse option
  sets. See `Campaigns in flight` below.

## Campaigns in flight (post-fix validation)

Started end-of-session, running on imacg3 in
`/Users/macuser/tmp/campaigns-056/`. Each summary lands in
its own file; check back later:

| File | Options | Seed range | Count | Purpose |
|---|---|---|---|---|
| `A-default.txt` | csmith defaults (`--no-volatiles --no-packed-struct --max-funcs 6 --max-block-depth 3`) | 1000-1500 | 501 | Re-validate the same option set that found v0.2.45 (BE bitfield) |
| `B-complex.txt` | `--max-funcs 12 --max-block-depth 5` | 2000-2300 | 301 | Larger / deeper programs — different codegen mix |
| `C-float.txt` | `--float` (no `--builtins` to avoid gcc-4.0 missing-bswap) | 3000-3300 | 301 | Re-target float paths since v0.2.46 fixed one |

Estimated runtime: 4-6 hours total on a single G3 (G3 is 1
CPU; gcc-4.0 dominates each iteration at ~7s).

To check progress / final results:

```sh
ssh imacg3 'tail -3 /Users/macuser/tmp/campaigns-056/progress.log
            ls /Users/macuser/tmp/campaigns-056/'
ssh imacg3 'for f in /Users/macuser/tmp/campaigns-056/*.txt; do
              echo "=== $f ==="
              tail -1 $f
            done'
```

After all three finish, `/Users/macuser/tmp/campaigns-056/QUEUE_DONE`
will exist. Triage FAILs with the existing analyzer:

```sh
ssh imacg3 '/Users/macuser/tmp/csmith_analyze.sh'
```
