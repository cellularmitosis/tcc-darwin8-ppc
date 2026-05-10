# Session 059 — csmith batch 4 + fctiwz design note (2026-05-09)

Picking up from session 058's HANDOFF. Open work items roughly
ranked by yield:

1. (cleared on arrival) Push commits + tag — already on origin.
2. Run another csmith campaign (`--float`, same shape as 058's
   campaign C) to look for the next layer of arg-passing /
   register-pressure bugs.
3. Document the design call on the seed 3259 float-to-uint32
   divergence so future sessions don't re-litigate it.
4. Maintenance: factor `gfunc_call`'s clobber-protection — not
   urgent, deferred again.

## Arrival state

* HEAD on uranium: `ef191d4` (session 058 docs commit), already
  on `origin/main`. Tag `v0.2.47-g3` (`72ad0cc`) on origin.
* imacg3 working tree: still based on stale commit `62511c4`
  but the in-WT modifications (`tcc/ppc-gen.c`, `tcc/tccgen.c`)
  md5-match uranium HEAD byte-for-byte. The on-disk tcc binary
  (`May  9 20:18`) is built from the v0.2.47 fix. **No sync
  needed** — git history on imacg3 is stale but the source it
  builds from is current.
* Pre-staged seeds in `/Users/macuser/tmp/csmith-batch4/`
  (seed-3000.c through seed-3300.c, 301 files), shape
  `--no-volatiles --no-packed-struct --float --max-funcs 6
  --max-block-depth 3 --max-array-dim 2 --max-array-len-per-dim 5`.
  Same shape as session 058's campaign C — these are session 058's
  input files, never executed against the v0.2.47 binary.
* Stale `bash 5062` from session 058 still wait-looping for a
  long-dead PID; killed at session start.

## What landed

No source changes. Two doc artifacts:

* [`findings.md`](findings.md) — durable design-call note for the
  `fctiwz` / `(uint32_t)(negative_float)` const-fold semantics.
  Closes session 058 HANDOFF item #3.
* [`commits.md`](commits.md) — session-doc commit log.

### Campaign run: csmith batch 4 (post-v0.2.47, `--float`)

* Seeds 3000–3300 (301 seeds), shape
  `--no-volatiles --no-packed-struct --float --max-funcs 6
  --max-block-depth 3 --max-array-dim 2 --max-array-len-per-dim 5`.
* Output dir on imacg3: `/Users/macuser/tmp/csmith-out-batch4/`.
* Runtime: ~21 minutes wall clock on the G3 (start 21:10, end
  21:31).
* Result: **PASS=264, FAIL=1, SKIP=36**.
  * The single FAIL is **seed 3259** — confirmed identical to
    session 058's triage: source contains `g_231 = (-0x1.2p+1)`
    (i.e. `(uint32_t)(-2.25)`), divergence is the documented UB
    fold (gcc-4.0 → 0; tcc → `0xFFFFFFFE`). Not a regression,
    not a new bug.
  * SKIPs are all `gcc-timeout` on programs gcc-4.0 itself
    miscompiles into infinite loops — same pattern as prior
    campaigns.
  * **Validation of v0.2.47:** seeds 3228 and 3265 (the FP-arg
    GPR-shadow clobber FAILs from session 058) both PASS this
    campaign. The save_reg fix holds.

### Campaign vs. session 058 results, same input set

| | 058 (pre-v0.2.47) | 059 (post-v0.2.47) |
|---|---|---|
| PASS | 262 | 264 (+2: seeds 3228, 3265) |
| FAIL | 3 (3228, 3259, 3265) | 1 (3259) |
| SKIP | 36 | 36 |

So the only post-v0.2.47 FAIL on this input set is the
documented UB shape. No new codegen bugs surfaced.

## Exit state

* HEAD on uranium: `ef191d4` (will advance with this session's
  doc commit). Already on origin/main at session start.
* imacg3 working tree: still based on stale commit `62511c4`,
  WT mod files md5-match uranium HEAD; tcc binary built
  May 9 20:18 from the v0.2.47 source. Stale `bash 5062`
  killed.
* `/Users/macuser/tmp/csmith-out-batch4/` retained on imacg3
  (3556 bytes SUMMARY.txt + per-FAIL artifacts) for any future
  re-triage.
* No tag changes. v0.2.47-g3 unchanged.
