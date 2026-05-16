# Session 085 — roadmap staleness audit (2026-05-15)

**Started:** 2026-05-15
**Arrival HEAD:** `5953a5d` (end of session 084).
**Exit HEAD:** see commit log; docs-only update.
**Trigger:** session 084 closed cleanly and the user picked "fix the
stale roadmap" when asked what to do next. The sqlite Heisenbug
line on the roadmap (#10) was the visible symptom; an audit
surfaced two more stale spots in the same file.

## TL;DR

Three stale claims in [`docs/roadmap.md`](../../../docs/roadmap.md)
corrected. No source change; no demo; no tag.

1. **Item #10 ("Real-world program builds")** — said `sqlite3_prepare_v2`
   still hits a Heisenbug from session 041. All three sqlite crashes
   that session 041 deferred were actually fixed across v0.2.16
   (`prepare_v2`), v0.2.22 (CREATE TABLE → SQLITE_CORRUPT, fixed
   incidentally by the relocation-addends work), and v0.2.23 (file-
   based `sqlite3_open` SEGV — stub allocation for data-referenced
   externs). Eight real-world programs build and run end-to-end with
   demos (Lua, zlib, bzip2, cJSON, an HTTP server, sqlite3 file +
   sqlite3-via-dylib, sed, gzip, Python). Updated the row to
   enumerate them, note the one remaining open sub-item from session
   044 (extern *data* references like `int *p = &errno;` still
   resolve to the slot address, no in-tree program hits it), and
   make clear there's no specific next-target gating the roadmap.

2. **Testing methodology #6 ("Upstream `make -k test`")** — listed
   "five passing stages" but enumerated eight, and named "Mach-O
   dylib output" as a still-deferred gating item. Mach-O dylib
   output shipped in v0.2.25 (session 045) and `dlltest` has been
   green since session 045 (confirmed in 046, 047, 048, 049, 053).
   `abitest-cc` and `abitest-tcc` also went green in the v0.2.32 /
   047 timeframe. Updated to list the current pass set
   (version, hello-exe, hello-run, libtest, vla_test-run, pp-dir,
   tests2-dir, memtest, abitest-cc, abitest-tcc, dlltest) and the
   actual gating items (libtest_mt + test3 → JIT heisenbug;
   btest/test1b/tccb → bcheck.c port; cross-test → not on Tiger).

3. **Risk register "No DWARF / no debugger"** — was marked
   `~~retired~~` but trailed with "Remaining gap: `dsymutil` round-
   tripping via an `N_OSO` chain pointing at real .o files — Phase
   2 polish item, deferred to a follow-up." Phase 2 shipped across
   v0.2.56–v0.2.58 (sessions 077, 078, 079) and the cross-reference
   in the same roadmap (item #7.5) already documents the full
   flow. Updated the risk-register entry to enumerate the Phase 2
   work and drop the "deferred to a follow-up" language. The
   workflow now is `tcc -gdwarf-2 ... -o exe; dsymutil exe; strip
   -S exe; gdb exe`.

## Audit method

Three passes over `docs/roadmap.md`:

1. **Concrete factual claims with version refs** — for each "v0.2.X
   does Y" claim, verify against the corresponding session's README
   or the source. The historical version table (lines 9–119) is
   the most-citation-heavy region; nothing stale there because
   every row was anchored to a session at the time it was added.

2. **Open-work claims** — for each item still flagged as open
   (Larger Scope #9/#10/#11, structural items in the "Open work"
   section, items in the Risk Register), search later sessions
   for the affected feature name to see whether the claim still
   holds. This is what surfaced #10 (sqlite) and the DWARF risk-
   register entry.

3. **Snapshot-style claims** ("currently …", "as of …") — these
   are the most decay-prone. The "Testing methodology" upstream-
   test snapshot was last updated in session 042 and the test set
   has improved substantially since then.

Searches used:

```sh
grep -rli "dlltest" docs/sessions/04[5-9]*/README.md \
                    docs/sessions/0[5-8]*/README.md
grep -l -E "sqlite3_open|sqlite3_prepare|SQLITE_CORRUPT" \
      docs/sessions/04[2-9]*/README.md
grep -n "^| \\*\\*#" docs/roadmap.md
```

Plus a read of each session's HANDOFF.md that touched the affected
features.

## What landed

### Source

None.

### Docs

* `docs/roadmap.md` — three edits, all in-place row rewrites.
  No reordering, no row deletions, no new rows.
* `docs/sessions/085-roadmap-staleness-audit-2026-05-15/README.md`
  (this file).
* `docs/sessions/085-roadmap-staleness-audit-2026-05-15/HANDOFF.md`.

### Memory updates

None. This session's findings are factual roadmap edits, not
durable user/feedback/project knowledge.

## What did NOT need updating

Verified-clean (no edit applied):

* The full historical version table (v0.2.1 → v0.2.62-g3). Every
  row is anchored to a session at the time it was added; nothing
  drifts because nothing references "current" state.
* Structural items #1 through #7.5 — all carry strikethroughs +
  ✅ markers + version anchors. Verified the v0.2.25 / 045 dylib
  claim ("Done in v0.2.25-g3 (session 045)") matches session 045's
  README.
* Item #8 (DWARF). Already marked done in v0.2.37/.38/.39.
* Item #9 (AltiVec). Still open, no progress to record.
* Item #11 (bcheck.c). Still open, no progress to record.
* "Out of scope" section. Unchanged in scope.
* Risk register bcheck blocker — still gating same set of tests.
* Risk register lwarx/stwcx — still applies, atomics work but
  scaling is mutex-bound for 8-byte ops.
* Risk register Tiger libSystem — unchanged constraint.
* Risk register out-of-tree upstream divergence — unchanged policy.

## Status of session 084's open items

| # | Item | Status |
|---|---|---|
| (084 #1, carried 067 #3) | Sibling r11 watch | unchanged |
| (084 #2, carried) | AltiVec intrinsics | unchanged |
| (084 #3, carried) | bcheck.c port | unchanged |
| (084 #4, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (084 #5, optional, carried) | `rsync` exclude-list polish | unchanged |

## Open work for next session

Same five carried items as session 084. The audit didn't surface
any new actionable work — it surfaced three already-done items the
roadmap hadn't been updated to reflect.

One soft observation: a session that took on a real-world program
build (any of the categories that hasn't been tried yet — common
candidates with portable C: `ncurses`, `expat`, `libpng`, `vim`,
`make`, `git`'s zlib-only subset, `m4`, `awk`) would likely surface
new bugs and produce concrete forward progress. Session 044's
deferred extern-data-reference fix (needs dyld bind-info emission)
is also concrete but currently has no in-tree program that hits it.

## How to pick up

Nothing specific queued. If a future session wants to extend item
#10's program list, the pattern is well established: pick a
portable C program, build it via `tcc -B./tcc -L./tcc -I./tcc/include`
on imacg3, fix whatever codegen / linker bugs surface, ship a
`demos/v0.2.XX-progname.sh` runner, bump the roadmap row.
