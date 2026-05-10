# Session 058 commits

| SHA | Headline |
|---|---|
| `679f137` | **ppc-gen: save GPR shadow regs before FP-arg shadow lwz.** `gfunc_call`'s FP-arg variadic GPR-shadow code (`lwz r{gslot+3}, 16(r1)` for float, similar for VT_DOUBLE both-in-GPR / straddle) now calls `save_reg(gslot)` (and `gslot+1` for VT_DOUBLE both-in-GPR) before the load. Pre-fix, an earlier-computed arg whose value happened to live in the target GPR was clobbered, and pass 2's later `mr r3, r{shadow}` moved float bits into the int arg's register. Mirrors the existing LL straddle / both-in-GPR `save_reg` pattern. Surfaced via csmith --float seed 3228; minimal repro is `f((x &= helper(1,2,3,4)), 5.0f)` with `f` having signature `(uint32_t, float)`. Tests2 111/111, abitest clean, bootstrap fixpoint holds, demos pass; csmith C-float seeds 3228 + 3265 now agree with gcc-4.0. |
| `a8a6dfb` | **demos + roadmap: v0.2.47.** New demo `v0.2.47-fp-arg-shadow.{c,sh}` covers 8 call shapes (compound assignment with helper-call int arg × float / double / two-float arg2). Roadmap `Where we are` bumped from v0.2.46 to v0.2.47, adds the v0.2.47 row. demos/README.md gets a row in the table. |
| (tag) | `v0.2.47-g3` annotated tag at `a8a6dfb` — "FP-arg GPR-shadow save_reg fix". |
