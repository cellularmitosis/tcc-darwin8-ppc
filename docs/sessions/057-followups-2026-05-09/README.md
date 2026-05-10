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

### Lua testes investigation on ibookg33 (handoff item #4)

User suggested ibookg33 as a second Tiger PPC host. Set it
up: tiger.sh installed `make-4.3` and `git-2.35.1`, rsync'd
the repo from uranium, built tcc cleanly. One workaround:
`/usr/local/bin/ar` → `/opt/cctools-667.3/bin/ar` errors
with `dyld: incompatible cpu-subtype` on this iBook G3
(probably a G4/AltiVec build of cctools). Passed
`AR=/usr/bin/ar` to make instead. tests2 111/111 PASS.
The `git` binary is broken (libiconv version mismatch);
not blocking — uranium remains the source of truth and
rsync handles transport.

Then ran `docs/sessions/055-bughunt-2026-05-09/lua_testes.sh`
end-to-end on ibookg33.

**Finding: handoff item #4 was a phantom.** `files.lua`
line 84 (`assert(io.output():seek() == 0)`) passes under
*both* tcc-built and gcc-4.0-built lua on Tiger PPC. The
actual failure in files.lua is at line 762 — the
popen/pclose comparison test — and it fails the same way
under *both* compilers because Tiger 10.4's `popen` /
`pclose` semantics don't return the signal-exit metadata
lua's test expects. The script's existing
`if [ "$g_exit" != "0" ]; then SKIP` arm correctly classifies
this as SKIP ("test infra issue not tcc"). Not a tcc bug.

The first run also flagged `math.lua` and `sort.lua` as
"FAIL (output diff)", but inspection shows pure
non-determinism:

* `math.lua`: prints `random seeds: <os.time()>, ...` and
  statistical samples from `math.random`.
* `sort.lua`: prints wall-clock `msec` timings and
  `comparisons` counts (quicksort with a randomized pivot).
* `constructs.lua`: prints `testing short-circuit
  optimizations (X)` where `X = math.random(0, 1)`
  (this is the non-determinism session 055's handoff
  flagged).

**Patch:** small per-test `diff -I` ignore-pattern map in
`lua_testes.sh`'s compare phase. Re-run after the patch:
**21 PASS, 0 FAIL, 1 SKIP** (files.lua infra issue). Clean
signal.

Net: lua-5.4.7's official testes suite (22 selected
language tests) passes against tcc-built lua to the same
fidelity as gcc-4.0-built lua, modulo Tiger's popen
quirks. **Item #4 closed.**

## What's still open

1. **Push tags to origin** — `v0.2.46-g3` plus 4 unpushed
   commits on main. **Needs user OK** per CLAUDE.md.
2. **Triage csmith campaigns A/B/C** — running on imacg3
   (started 18:33, 4-6h ETA), look for `QUEUE_DONE` flag at
   `/Users/macuser/tmp/campaigns-056/`.
3. **`--enable-builtin-kinds`** — needs csmith binary; not
   installed on either PPC host (only pre-generated seed
   files are). Deferred until csmith is built.
4. ~~Lua testes~~ — closed this session.
5. ~~Dead VT_LLONG store path~~ — closed earlier this
   session.
6. **Docs/STAB/AltiVec/bcheck** — out of scope.

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
