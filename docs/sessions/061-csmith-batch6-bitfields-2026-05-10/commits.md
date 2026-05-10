# Session 061 commits

| Commit | Headline |
|---|---|
| (this session's docs commit) | **docs/sessions/061: csmith batches 6+7 (bitfields, two-host parallel).** README + findings + SUMMARYs + seed-6240 + commits + HANDOFF. No source changes. Two campaigns run in parallel: batch 6 on imacg3 (default depth + bitfields, seeds 5000–5300, ~23 min, PASS=253/FAIL=0/SKIP=48) and batch 7 on ibookg38 (deep + bitfields, seeds 6000–6300, ~50 min, PASS=245/FAIL=1/SKIP=55). Net 600 seeds, **0 real tcc bugs**. The single FAIL (seed 6240) verified as csmith-generated unspecified-eval-order UB — gcc-4.0 -O0 vs -O2 also disagree. Bonus: this session bootstrapped ibookg38 (second G3 test host) from bare-metal — captured the G4-vs-G3 cctools quirk requiring `PATH=/usr/bin:...` for builds. |
