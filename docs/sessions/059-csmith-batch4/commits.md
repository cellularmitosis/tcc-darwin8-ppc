# Session 059 commits

| SHA | Headline |
|---|---|
| (this commit) | **docs/sessions/059: csmith batch4 + fctiwz design call.** Re-runs session 058's `--float` shape (seeds 3000–3300) against the v0.2.47 binary: 264 PASS / 1 FAIL / 36 SKIP. Only FAIL is seed 3259 (documented UB-shape, gcc-4.0 vs tcc `(uint32_t)(-2.25)` const-fold). Seeds 3228 + 3265 (the FP-arg GPR-shadow clobber repros from session 058) both PASS — v0.2.47 fix holds. No new codegen bugs surfaced. `findings.md` writes up the durable design-call note for the fctiwz / negative-float-to-uint32 const-fold semantics so future sessions don't re-litigate it. |
