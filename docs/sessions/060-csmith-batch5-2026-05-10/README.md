# Session 060 — csmith batch 5 (deeper FP shape)

**Date:** 2026-05-10
**Author:** Opus 4.7 (1M context)

## Arrival state

* HEAD: `68f3fca` ("pulling in convos") on `main`, in sync with `origin/main`.
* Last codegen change: v0.2.47 (FP-arg GPR-shadow clobber fix, session 058).
* imacg3 has the v0.2.47 binary built on disk (`tcc -v` →
  `0.9.28rc 2026-05-09 main@62511c4*`); harness
  `/Users/macuser/tmp/csmith_campaign.sh` is intact from session 058.
* Session 059 ran a validation pass against batch4's input shape
  (3000–3300, `--float`) and surfaced **zero new bugs** (1 expected
  FAIL = the documented `(uint32_t)(-2.25)` UB shape, seed 3259).
* Per session 059's HANDOFF the next productive campaign is to
  **widen the shape**, not re-run the same input. Three candidates
  were proposed; this session picks #1.

## Plan

Run a csmith campaign with shape:

```
--no-volatiles --no-packed-struct --float \
--max-funcs 12 --max-block-depth 5 \
--max-array-dim 2 --max-array-len-per-dim 5
```

Seeds 4000–4300 (301 seeds), output dir `csmith-out-batch5`.

This is the union of:
* session 058's campaign B shape (`--max-funcs 12 --max-block-depth 5`,
  which originally surfaced seed 2187 — gcc-4.0 fold), and
* batch4's `--float` emphasis (which drove v0.2.47's FP-shadow fix).

The hypothesis is that deeper programs with FP arguments will
exercise more FP-arg-passing edge cases beyond what batch4 reached.
Wall time expected ~25–35 min on the G3.

After the run, triage every FAIL:
* Reproduce on uranium if possible (cross-compile or copy artifacts).
* Determine if it's a new tcc bug, a known UB shape, or a
  csmith/gcc-4.0 quirk.
* For any new tcc bugs: minimize, file in `findings.md`, decide if
  it warrants a v0.2.48 fix this session.

## What happened

Generated 301 seeds locally on uranium (csmith 2.3.0 lives there,
not on imacg3) with the shape above; rsync'd them to
`imacg3:/Users/macuser/tmp/csmith-batch5/`. Kicked the existing
harness (`csmith_campaign.sh 4000 4300 …`) in nohup; ~32 min wall
on the G3, slightly past the predicted 25–35 min window —
expected, since deeper programs spend more time in gcc-4.0's
optimizer/codegen.

### Result

```
PASS=258  FAIL=0  SKIP=43
```

* **0 FAILs.** The v0.2.47 fix held; no new arg-passing,
  expression-depth, or FP-codegen divergence surfaced in 258 fully
  validated seeds.
* All 43 SKIPs were `(gcc-timeout)` — the gcc-built binary ran past
  the 15s wall budget. None were `(gcc-compile-fail)`,
  `(tcc-timeout)`, or `(tcc-compile-fail)`. csmith occasionally
  produces pathologically slow programs at this shape; the SKIP
  rate (14.3%) is up from session 059's 12% as expected for
  deeper programs.
* Spot-checked the harness contract: 80-line read of the harness
  confirmed PASS = both compilers compiled, both binaries ran to
  completion within 15s, and `diff -q` of stdout matched
  byte-for-byte. PASS artifacts are cleaned up post-success;
  SKIP/FAIL artifacts remain on disk for triage.

This is the **second consecutive quiet validation campaign** after
v0.2.47, and the **fifth csmith campaign overall** in the
differential-testing arc:

| # | Session | Shape | Seeds | Result | Outcome |
|---|---|---|---|---|---|
| 1 | 055 | default | 1000–1500 | found nothing post-v0.2.45 | clean |
| 2 | 056 | `--float --builtins` | seed 92 | 1 FAIL (BE bitfield) | → v0.2.45 fix |
| 3 | 058 | `--float` | 3000–3300 | 3 FAILs (1 fold + 2 FP-shadow) | → v0.2.46 + v0.2.47 fixes |
| 4 | 059 | `--float` re-run | 3000–3300 | 1 FAIL (documented UB seed 3259) | clean |
| 5 | **060** | `--float --max-funcs 12 --max-block-depth 5` | 4000–4300 | **0 FAILs** | clean |

(Sessions 058 also ran campaigns A=`default 1000–1500` and
B=`--max-funcs 12 --max-block-depth 5 2000–2300`; B surfaced seed
2187, the gcc-4.0 fold.)

The signal is: the union shape of batch4 (`--float`) and 058 B
(`--max-funcs 12 --max-block-depth 5`) does **not** uncover a new
bug class — both component shapes are already covered by prior
fixes. To find the next bug, the campaign needs a genuinely new
axis (see HANDOFF).

## Exit state

* HEAD unchanged at session start (`68f3fca`); this session adds
  one docs-only commit (`docs/sessions/060-csmith-batch5/`).
* No source changes. No demos. No release.
* imacg3 state preserved:
  * `/Users/macuser/tmp/csmith-batch5/seed-4000.c …
    seed-4300.c` — 301 generated programs, kept for re-runs.
  * `/Users/macuser/tmp/csmith-out-batch5/SUMMARY.txt` plus the
    43 SKIP-seed binaries/outputs.
  * `/Users/macuser/tmp/campaigns-060/batch5.log` — single line:
    `PASS=258 FAIL=0 SKIP=43`.
* Local copy of SUMMARY.txt at
  [`SUMMARY.txt`](SUMMARY.txt) for offline inspection.

## Files in this session dir

* `README.md` — this narrative.
* `SUMMARY.txt` — verbatim copy of the imacg3 campaign summary.
* `commits.md` — the one docs commit landed.
* `HANDOFF.md` — pickup notes for the next session.

(No `findings.md` — there's nothing durable to record. The
fctiwz call from 059 covers what would have gone there;
"another quiet campaign" is narrative, not a finding.)
