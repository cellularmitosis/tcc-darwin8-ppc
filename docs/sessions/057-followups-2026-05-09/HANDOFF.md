# Handoff — end of session 057 (2026-05-09)

## TL;DR

Two lines of work landed: (1) handoff item #5 — delete
dead `VT_LLONG` store-to-global paths in `ppc-gen.c`,
verified on imacg3 alongside the running csmith campaigns;
(2) handoff item #4 — set up **ibookg33** as a second
Tiger PPC test host and ran lua testes there. The "files.lua
line 84" finding from session 055 didn't reproduce — both
tcc-built and gcc-built lua pass line 84; the only failure
in files.lua is at line 762 (Tiger popen quirk, both
compilers, correctly skipped by the existing harness).
Updated `lua_testes.sh` to filter three flavors of test
non-determinism. Final lua signal: **21 PASS, 0 FAIL, 1
SKIP**. Item #4 closed.

* HEAD: `f58c4f2` — local; **NOT pushed** (along with the
  three earlier unpushed: `3fd2e0a`, `ee1f1e0`, plus
  `78637d5`/`0aa4690`/`b1b421c` from session 056, and tag
  `v0.2.46-g3`).
* csmith campaigns A/B/C still running on imacg3, started
  18:33:12 — A was at seed-1346/1500 (~28 min in) when this
  session ended at ~19:02. ETA late evening. `QUEUE_DONE`
  flag will appear at `/Users/macuser/tmp/campaigns-056/`
  when finished.
* **ibookg33 is now set up** as a second PPC build/test
  host. Has tcc built and tests2 passing. See "ibookg33
  setup" below.

## What landed

| Tag | Commits | Headline |
|---|---|---|
| (no tag this session) | `3fd2e0a` (1) | **Drop dead VT_LLONG store-to-global branches in `ppc-gen.c::store()`.** The three `case VT_LLONG:` arms inside the `(v == VT_CONST && (sv->r & VT_SYM))` block — PIC, non-PIC addend!=0, non-PIC addend==0 — were unreachable. `vstore()` (`tccgen.c:4104-4143`) rewrites `vtop[-1].type.t` to `VT_INT` (= `VT_PTRDIFF_T` on PPC32) for any `USING_TWO_WORDS` destination *before* calling `store()` twice (once per word). The other `store()` callsite, `save_reg_upstack`, only emits `VT_LOCAL` stores — never `VT_SYM`. PPC has no target-specific `asm.c` calling `store()`. Replaced the three arms with `default:` fall-through; existing `tcc_error("not yet supported")` catches any future regression. Diff: 7 insertions, 39 deletions. tests2 111/111, abitest-cc/tcc 48/48, FIXPOINT holds, demos v0.2.32/.33/.40/.45/.46 all PASS. |
| (no tag this session) | `f58c4f2` (1) | **Lua testes investigation + harness fix on ibookg33.** Ran `lua_testes.sh` end-to-end against tcc-built and gcc-4.0-built lua-5.4.7 on ibookg33. The handoff item #4 lead — "files.lua line 84 fails on tcc-built lua" — didn't reproduce; line 84 (`assert(io.output():seek() == 0)`) passes under both. The only files.lua failure is at line 762 (popen/pclose comparison test) and it fails the *same way* under both compilers due to Tiger 10.4 popen semantics. The script's existing `if [ "$g_exit" != "0" ]; then SKIP` arm correctly handles it. The first run also flagged math.lua, sort.lua, and constructs.lua as FAIL, but inspection showed pure non-determinism (`os.time()`-seeded `math.random`, wall-clock msec timings, randomized quicksort pivot). Added a per-test `diff -I <pat>` map so the harness ignores those stable patterns. Final signal: **21 PASS, 0 FAIL, 1 SKIP** (files.lua infra). Lua-5.4.7 testes passes on tcc-built lua to the same fidelity as gcc-built lua. |

## Open work for next session

### 1. Push tags + commits to origin (needs user OK)

There are now 6 unpushed commits on `main`:

* `78637d5` — tccgen: PPC float-to-int const-fold (s056)
* `0aa4690` — demos + roadmap: v0.2.46 (s056)
* `b1b421c` — docs/sessions/056 (s056)
* `3fd2e0a` — ppc-gen: drop dead VT_LLONG store-to-global (s057)
* `ee1f1e0` — docs/sessions/057 (initial)
* `f58c4f2` — lua_testes.sh: ignore non-deterministic per-test output (s057)

Plus the unpushed tag `v0.2.46-g3` (points at `0aa4690`).

If parity with prior sessions is desired:

```sh
git push origin main
git push origin v0.2.46-g3
```

No new tag for `3fd2e0a` or `f58c4f2` — both are structural
cleanup, not user-facing capabilities. Current convention is
"tag at user-facing milestones only."

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

### 4. Lua testes — closed this session

See "What landed" / `f58c4f2`. Re-run any time with:

```sh
ssh ibookg33 '/opt/tigersh-deps-0.1/bin/bash \
    ~/tcc-darwin8-ppc/docs/sessions/055-bughunt-2026-05-09/lua_testes.sh \
    | tail -28'
```

Expect `PASS=21 FAIL=0 SKIP=1`.

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

## ibookg33 setup (new this session)

ibookg33 is now a usable second Tiger PPC test host, alongside
imacg3. Specs: PowerBook4,3 (iBook G3 800 MHz, 256 MB RAM,
Tiger 10.4.11, 12 GB free, runs idle by default).

Setup that landed:

* `tiger.sh make-4.3` and `tiger.sh git-2.35.1` installed.
* Repo synced from uranium via `rsync` (origin doesn't yet
  have the unpushed commits).
* tcc built cleanly with **`AR=/usr/bin/ar`** override —
  `/usr/local/bin/ar` is symlinked to
  `/opt/cctools-667.3/bin/ar`, which crashes with
  `dyld: incompatible cpu-subtype` on this G3 (probably a
  G4/AltiVec-only build of cctools shipped in tigersh-deps).
* `tests2`: 111/111 PASS.

Caveats:

* `git` binary is broken: `dyld` reports
  `libiconv.2.dylib provides version 5.0.0` but git wants
  `9.0.0`. Not blocking — uranium remains the source of
  truth and rsync handles transport. To fix later, may need
  reinstalling `libiconv-1.16` over the existing one or
  installing a tigersh-deps libiconv that matches.

When to use ibookg33 vs imacg3:

* **imacg3 free** → use it; it's faster.
* **imacg3 busy (e.g. csmith campaign)** → use ibookg33
  for tcc tests, lua testes, etc. that don't depend on
  imacg3's data.
* **Need parallelism** → run independent things on both.

## Closing notes

Two commits this session, both opportunistic given imacg3 was
saturated by the running csmith campaigns from session 056.
The dead-code cleanup makes `ppc-gen.c::store()` easier to
reason about (single contract: USING_TWO_WORDS types arrive
pre-split, never as one two-word store). The lua testes work
turned a vague "open since 055" into a verified-clean signal,
plus a permanent harness improvement.

Next session: [docs/sessions/057-followups-2026-05-09/HANDOFF.md](docs/sessions/057-followups-2026-05-09/HANDOFF.md)
