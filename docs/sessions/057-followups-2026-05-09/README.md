# Session 057 — handoff follow-ups (2026-05-09)

Picking up from session 056's HANDOFF. Three csmith campaigns
were already running on imacg3 when this session opened
(started 18:33:12, ~25 minutes in at session start; expected
to complete late evening / overnight).

## Arrival state

* uranium HEAD: `b1b421c` (session-056 docs commit).
* `v0.2.46-g3` tag exists locally; not pushed to origin.
* imacg3 working tree at `62511c4` (session-055) with
  uncommitted v0.2.46 source changes (matches uranium
  commit `78637d5`); tcc binary built 18:16, used for
  campaigns running since 18:33.
* Campaigns A/B/C running per session-056 plan; ETA 4-6h
  total, A only ~25min in at session start.

## Work landed

### `3fd2e0a` — drop dead VT_LLONG store-to-global branches in ppc-gen.c

Handoff item #5. The three `case VT_LLONG:` blocks inside
`store()`'s `(v == VT_CONST && (sv->r & VT_SYM))` path
(PIC, non-PIC addend!=0, non-PIC addend==0) were
unreachable.

**Why dead:** `vstore()` (`tccgen.c:4104-4143`) checks
`USING_TWO_WORDS(dbt)` for any VT_LLONG/VT_LDOUBLE store and
rewrites `vtop[-1].type.t` to `load_type` (VT_PTRDIFF_T =
VT_INT on PPC32, or VT_DOUBLE for VT_LDOUBLE) **before**
calling `store()` twice — once per word — with the rewritten
type. So `store()` never sees `bt == VT_LLONG`.

**Other callsite check:** `save_reg_upstack` is the only
other caller of `store()` from `tccgen.c`. It always passes
`sv.r = VT_LOCAL | VT_LVAL`, never `VT_SYM`. PPC has no
target-specific asm.c calling `store()` (cf. arm-asm.c,
i386-asm.c, riscv64-asm.c which other targets use).

**Replacement:** the three switch arms now fall through to
the existing `default: tcc_error("not yet supported")`. A
short comment in the PIC default arm explains the splitting;
the other two reference back to it.

**Verification (imacg3, incremental rebuild over uranium
HEAD with my edit rsync'd in, while campaigns running on
the same box — rebuild was clean despite shared CPU):**
* `tests2`: 111/111
* `abitest-cc abitest-tcc`: 48 successes, 0 fails
* `bootstrap-tcc-self.sh FIXPOINT=1`: HOLDS
* demos v0.2.32, v0.2.33, v0.2.40, v0.2.45, v0.2.46: all PASS

Diff: 7 insertions, 39 deletions. No semantic change to
reachable code.

## What's still open

Same items as session 056's handoff except #5:

1. **Push tags to origin** — `v0.2.46-g3` and now whatever
   tag (if any) we'd cut from `3fd2e0a`. **Needs user OK**
   before pushing per CLAUDE.md.
2. **Triage csmith campaigns A/B/C** — campaigns running
   throughout this session, expected to finish late evening
   (started 18:33, 4-6h ETA). Look for `QUEUE_DONE`.
3. **`--enable-builtin-kinds`** — needs imacg3 free.
4. **Lua `files.lua` line 84** — needs imacg3 free.
6. **Docs/STAB/AltiVec/bcheck** — out of scope for this
   session.

## Why this session was short

Three of four open items needed imacg3, and imacg3 was
saturated by the campaigns. Item #5 (this session's
contribution) was the only one I could verify cleanly
without disrupting them — incremental ppc-gen.o rebuild
fits in CPU-shared mode without affecting csmith throughput.

The tag-pushing item needs explicit user authorization, so
deferred to next interactive session.

## Notes for future sessions

The unreachability finding is informally documented in the
new comments in `ppc-gen.c::store()`, but the deeper truth
is: on PPC32, the framework path for any USING_TWO_WORDS
store is "type-rewrite to single word, then call `store()`
per word". This is the contract `store()` can rely on — and
which the dead branches were redundantly trying to honor.

If the campaigns surface a divergence, the manual-narrowing
recipe from session 056's `findings.md` still applies:
csmith's per-global checksum trace (`./prog 1`) makes
narrowing fast without creduce.
