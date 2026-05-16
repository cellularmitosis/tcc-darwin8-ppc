# Handoff — end of session 085 (2026-05-15)

## TL;DR

Docs-only session. Three stale claims in [`docs/roadmap.md`](../../../docs/roadmap.md)
corrected:

1. **Item #10** ("Real-world program builds") — said
   `sqlite3_prepare_v2` still hits a Heisenbug deferred from
   session 041. All three sqlite crashes were fixed across
   v0.2.16 / v0.2.22 / v0.2.23; eight real-world programs build
   end-to-end now. Updated to enumerate them and surface the one
   genuinely-deferred sub-item (extern data references like
   `int *p = &errno;`, needs dyld bind-info emission, no in-tree
   program hits it).

2. **Testing methodology #6** — listed "five passing stages" but
   enumerated eight, and named "Mach-O dylib output" as
   still-deferred. Mach-O dylib output shipped v0.2.25; `dlltest`
   has been green since session 045. Updated the pass list to the
   current state (11 stages, 3 still failing on libtest_mt/test3
   JIT heisenbug, bcheck.c port, and the cross-test out-of-scope
   item).

3. **Risk register "No DWARF / no debugger"** — was marked
   retired but trailed with "Remaining gap: `dsymutil` round-
   tripping via an `N_OSO` chain pointing at real .o files —
   Phase 2 polish item, deferred to a follow-up." Phase 2 shipped
   across v0.2.56–v0.2.58. Updated to enumerate Phase 2 and drop
   the "deferred" language; full workflow is now `tcc -gdwarf-2
   ... -o exe; dsymutil exe; strip -S exe; gdb exe`.

* **HEAD at session start:** `5953a5d` (end of session 084).
* **HEAD at session end:** see commit log.
* **Source changes:** none.
* **Tag:** none — docs-only.

## What landed

### Source

None.

### Docs

* [`docs/roadmap.md`](../../../docs/roadmap.md) — three in-place
  row rewrites: item #10, Testing methodology bullet #6, Risk
  register DWARF entry.
* [`docs/sessions/085-roadmap-staleness-audit-2026-05-15/README.md`](README.md) —
  full audit writeup with method, verified-clean checklist, and
  the open-work carryover.
* This `HANDOFF.md`.

### Memory updates

None.

## Status of session 084's open items

| # | Item | Status |
|---|---|---|
| (084 #1, carried 067 #3) | Sibling r11 watch | unchanged |
| (084 #2, carried) | AltiVec intrinsics | unchanged |
| (084 #3, carried) | bcheck.c port | unchanged |
| (084 #4, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (084 #5, optional, carried) | `rsync` exclude-list polish | unchanged |

## Open work for next session

Same five carried items. No new work surfaced by the audit — all
three updates documented things that had already shipped.

### 1. Sibling r11 watch (carried, 067 #3)

PPC sibling-register concern to v0.2.48's r12 fix. csmith hasn't
surfaced a repro yet. If a future seed does, the symmetric fix is
`reg_classes[8] = 0`.

### 2. AltiVec intrinsics (carried)

PowerPC SIMD. Multi-session lift. New parser front-end work + codegen.

### 3. `bcheck.c` port (carried)

PowerPC port of tcc's bounds-check runtime. Currently stubbed via
`bcheck-ppc.c`. Multi-session lift.

### 4. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3

Inherited from session 069. Safe to drop.

### 5. (optional, low priority) `rsync` exclude-list polish

Sidestepped again this session (docs-only, no source sync).

### Possible new direction (not queued)

A real-world program build session — pick a candidate not yet in
the demos list (ncurses, expat, libpng, vim, make, m4, awk, etc.),
try the build, fix what surfaces, ship a `demos/v0.2.XX-prog.sh`
runner. That's the most-likely-productive open direction now that
the deferred sqlite work is fully closed.

Session 044's deferred extern-data-reference fix (`int *p = &errno;`)
is also concrete but currently has no in-tree program hitting it.
Would unblock a class of programs that statically initialize
pointers to libSystem data symbols.

## How to pick up

Nothing specific queued — the next session is a "pick a direction"
moment. The audit method is documented in this session's README
under "Audit method" if a future roadmap audit is warranted (it
doesn't need to be a routine workflow; this one was triggered by
session 084 leaving no concrete carry-forward task).

## Subagent log

None this session — direct edits to a known file, single-pass
audit, no exploration delegated.

## Closing notes

The audit confirms a pattern: rows in the roadmap that anchor to
specific version tags + session links stay accurate because the
underlying claims are frozen at write-time. Rows that use
"currently …" or "remaining gap: …" wording decay fastest. The
three updates this session all targeted the second kind; the
first kind (every row in the historical version table) was
verified-clean.

Next session: [docs/sessions/085-roadmap-staleness-audit-2026-05-15/HANDOFF.md](docs/sessions/085-roadmap-staleness-audit-2026-05-15/HANDOFF.md)
