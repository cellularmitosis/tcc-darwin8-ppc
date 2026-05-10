# Handoff — end of session 057 (2026-05-09)

## TL;DR

Short session. Landed handoff item #5 (delete dead
VT_LLONG store-to-global paths in `ppc-gen.c`); verified
green on imacg3 alongside the running csmith campaigns
without disturbing them. Remaining items from session 056's
handoff — push tags, triage campaigns, builtin-kinds, Lua —
are still open and were blocked on either user
authorization (push) or imacg3 being free (the rest).

* HEAD: `3fd2e0a` — local; **NOT pushed** (along with
  `78637d5`, `0aa4690`, `b1b421c` and tag `v0.2.46-g3`).
* csmith campaigns A/B/C still running on imacg3, started
  18:33:12 — A was at seed-1346/1500 (~28 min in) when this
  session ended at ~19:02. ETA late evening. `QUEUE_DONE`
  flag will appear at `/Users/macuser/tmp/campaigns-056/`
  when finished.

## What landed

| Tag | Commits | Headline |
|---|---|---|
| (no tag this session) | `3fd2e0a` (1) | **Drop dead VT_LLONG store-to-global branches in `ppc-gen.c::store()`.** The three `case VT_LLONG:` arms inside the `(v == VT_CONST && (sv->r & VT_SYM))` block — PIC, non-PIC addend!=0, non-PIC addend==0 — were unreachable. `vstore()` (`tccgen.c:4104-4143`) rewrites `vtop[-1].type.t` to `VT_INT` (= `VT_PTRDIFF_T` on PPC32) for any `USING_TWO_WORDS` destination *before* calling `store()` twice (once per word). The other `store()` callsite, `save_reg_upstack`, only emits `VT_LOCAL` stores — never `VT_SYM`. PPC has no target-specific `asm.c` calling `store()`. Replaced the three arms with `default:` fall-through; existing `tcc_error("not yet supported")` catches any future regression. Diff: 7 insertions, 39 deletions. tests2 111/111, abitest-cc/tcc 48/48, FIXPOINT holds, demos v0.2.32/.33/.40/.45/.46 all PASS. |

## Open work for next session

### 1. Push tags + commits to origin (needs user OK)

There are now 4 unpushed commits on `main`:

* `78637d5` — tccgen: PPC float-to-int const-fold (s056)
* `0aa4690` — demos + roadmap: v0.2.46 (s056)
* `b1b421c` — docs/sessions/056 (s056)
* `3fd2e0a` — ppc-gen: drop dead VT_LLONG store-to-global (s057)

Plus the unpushed tag `v0.2.46-g3` (points at `0aa4690`).

If parity with prior sessions is desired:

```sh
git push origin main
git push origin v0.2.46-g3
```

A new tag for `3fd2e0a` is optional — it's structural cleanup,
not a user-facing capability. Current convention seems to be
"tag at user-facing milestones only," so we may want to skip
tagging this one.

### 2. Triage the three queued csmith campaigns

Same as session 056's handoff item #2 — copy verbatim:

Running on imacg3 in `/Users/macuser/tmp/campaigns-056/`:

| File | Options | Seeds | Purpose |
|---|---|---|---|
| `A-default.txt` | csmith defaults | 1000-1500 (501) | Re-validate same option set that found v0.2.45 |
| `B-complex.txt` | `--max-funcs 12 --max-block-depth 5` | 2000-2300 (301) | Larger / deeper programs |
| `C-float.txt` | `--float` (no --builtins) | 3000-3300 (301) | Re-target float paths since v0.2.46 |

```sh
# Status check:
ssh imacg3 'tail -5 /Users/macuser/tmp/campaigns-056/progress.log
            ls /Users/macuser/tmp/campaigns-056/'

# When QUEUE_DONE exists, triage each result file:
ssh imacg3 'for f in /Users/macuser/tmp/campaigns-056/{A,B,C}-*.txt; do
              echo "=== $(basename $f) ==="
              grep -c PASS $f
              grep -c FAIL $f
              grep -c SKIP $f
              grep ^FAIL $f | head
            done'

# For any FAIL: copy seed to uranium, narrow manually as in
# session 056 (see findings.md for the recipe), or bring up
# creduce with /tmp/seed92-reduce2/test.sh as a template.
```

### 3. Csmith with `--enable-builtin-kinds` (still open)

Same as session 056's handoff item #3 — see that handoff for
the full notes on `bswap_compat.h` shim alternative.

### 4. Lua testes (still open)

`files.lua` line 84 `assert(io.output():seek() == 0)` fails
on tcc-built lua. Same as session 056's handoff item #4.

### 5. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

## How to pick up

### Sanity baseline — same as session 056's HANDOFF

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
git fetch /Users/cell/claude/tcc-darwin8-ppc main   # or origin if pushed
git reset --hard FETCH_HEAD
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh   # FIXPOINT HOLDS
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.32-longdouble.sh                 # PASS
./demos/v0.2.33-libtcc-callback.sh            # PASS
./demos/v0.2.40-sed.sh                        # PASS
./demos/v0.2.41-gzip.sh                       # PASS
./demos/v0.2.42-python.sh                     # PASS
./demos/v0.2.43-gdebug-extern.sh              # PASS
./demos/v0.2.44-gdebug-link.sh                # PASS
./demos/v0.2.45-be-bitfield-abi.sh            # PASS
./demos/v0.2.46-float-int-const-fold.sh       # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. One in-session author (Opus 4.7).

## Closing notes

Single-commit session driven by what was tractable while
imacg3 was busy with csmith campaigns. The cleanup commit
should make future modifications to `ppc-gen.c::store()`
easier to reason about: there's now a single contract
(USING_TWO_WORDS types arrive split, never as a single
two-word store) instead of a partly-implemented one.

Next session: [docs/sessions/057-followups-2026-05-09/HANDOFF.md](docs/sessions/057-followups-2026-05-09/HANDOFF.md)
