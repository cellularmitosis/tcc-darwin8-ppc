# Handoff — end of session 066 (2026-05-11)

## TL;DR

Session 065's lone fail (seed-732 default-opts) **closed** by a
small ppc-gen.c register-allocator fix. The bug was a hardcoded-
scratch / allocator collision on `r12`: tcc's `lis r12, ha(sym)`
two-instruction global-load sequence assumes r12 is free, but r12
was simultaneously in tcc's int reg pool. Under high register
pressure the allocator parked a live vstack value in r12, then the
next `lis r12, ha` silently destroyed it.

* HEAD at session start: `05544a4` (session 065 validation docs).
* HEAD at session end: `4bb47be` (creduce-post setup). Session
  commits in order: `1f32055` (the fix), `1d5c818` (docs), then
  three follow-ups (`dc50403`, `90abcf1`, `cefea61`, `4bb47be`).

**Fix:** one-line change in `tcc/ppc-gen.c` — set `reg_classes[9] = 0`
instead of `RC_INT | RC_R(9)`. Tcc's int pool drops from 10 slots
(r3..r12) to 9 (r3..r11). r12 is now true scratch.

**Regression suite:** all green (fixpoint, tests2 111/111, abitest
24/24, demos/v0.2.47).

**Csmith re-sweep (two hosts, parallel):**
* ibookg37 default-opts 1-1000: **873 PASS / 0 FAIL / 127 SKIP**,
  clean. Better than session 065's run (872 / 1 / 127) by exactly
  the one fix. Summary:
  `/Users/macuser/tmp/csmith-out-066-default/SUMMARY.txt`.
* imacg3 --builtins+bitfields 8020-8419: **352 PASS / 0 FAIL /
  48 SKIP**, matches session 065 byte-for-byte. Summary:
  `/Users/macuser/tmp/csmith-out-066-builtins/SUMMARY.txt`.

**v0.2.48-g3 tag:** **created (local only, not pushed)** on
commit `1f32055`. Subject:
`v0.2.48-g3: r12 reg-allocator clobber fix (closes seed-732)`.

## What landed

* `tcc/ppc-gen.c` — `reg_classes[9] = 0` (was `RC_INT | RC_R(9)`).
  See commit message on `1f32055` for the full why.

* `docs/sessions/066-seed-732-2026-05-11/`:
  * `README.md` — narrative.
  * `seed-732-sentinel-block.dis` — 315-instruction disassembly
    between sentinel markers, with the 9-instruction r12-clobber
    sequence isolated.
  * `repro_v1..v8.c` — progressively-larger hand-built reproducers.
    **None triggered the bug**: even a 9-pointer assignment chain
    (`repro_v8.c`) doesn't push tcc's allocator into r12, because
    tcc picks the lowest-numbered free slot and 9 vstack values
    fit in r3..r11. seed-732's bug-triggering shape needs a
    tighter set of simultaneously-live values plus a specific
    allocation order that csmith produced but I couldn't
    reverse-engineer by hand. Kept for the record; future
    creduce passes can start from seed-732 itself.
  * `creduce/` — first-attempt creduce setup with a single-tcc
    predicate (gcc-vs-tcc disagrees). Stopped at 73 lines / 39 KB
    when the fix landed and the predicate stopped reproducing.
    `test.c` is the partial reduction at kill-time (~13% smaller
    than the 922-line original `test.c.orig`); `test.sh` is the
    predicate.
  * `creduce-post/` — second-attempt creduce with a strict 3-way
    predicate (gcc-and-post-fix-tcc agree AND pre-fix-tcc
    disagrees). Stalled at 735 lines / 39 KB (~14% reduction)
    — every meaningful reduction killed the register-pressure
    that triggers the bug. Setup kept for future reduction
    attempts; `test.c` reset to the 922-line starting point.

* `demos/v0.2.48-r12-clobber.{c,sh}` — the v0.2.48 milestone
  demo. The .c is the unmodified seed-732 program; the .sh
  compiles + runs it and asserts the end-of-run checksum is
  `76F5DB56` (gcc-4.0 -O0 reference). Verified on imacg3 and
  ibookg37; both PASS. Requires the csmith runtime headers
  at `CSMITH_INC` (default `/Users/macuser/tmp/csmith`).

* `demos/README.md` — table row for v0.2.48-r12-clobber.

## Status of session 065's open items

| # | Item | Status |
|---|---|---|
| 1 | seed-732 fix | landed (this session) |
| 2 | v0.2.48-g3 tag | **created locally**; push pending user sign-off |
| 3 | (optional) v0.2.48 demo | **landed** (demos/v0.2.48-r12-clobber.{c,sh}) — seed-732 vendored verbatim |
| 4 | libtcc1.a clz/ctz to match gcc-PPC | unchanged |
| 5 | csmith building on a PPC host | unchanged |
| 6 | ibookg38 — back online or written off | unchanged |
| 7 | OSO STAB / self-link / AltiVec / bcheck | unchanged |

## Open work for next session

### 1. Push v0.2.48-g3 tag (user confirmation required)

The tag exists locally on `1f32055`. Pushing it makes the release
public:

```sh
git push origin v0.2.48-g3
```

Per session convention, **do not push without explicit user
sign-off**. The HANDOFF for session 065 (and prior sessions) all
explicitly say so.

### 2. (LANDED) v0.2.48 demo

Shipped this session: `demos/v0.2.48-r12-clobber.{c,sh}`. The .c
is seed-732 verbatim. The .sh compiles + runs it and asserts
the end-of-run checksum is `76F5DB56` (gcc-4.0 -O0 reference).
Verified on imacg3 and ibookg37.

Two creduce attempts ran during the session; neither produced
a minimal hand-built repro:
* First attempt (`creduce/`): reduced 922 → 73 lines under a
  loose "gcc-vs-tcc disagree" predicate, but the result was
  still essentially seed-732 minus comments + unused globals,
  not a different shape.
* Second attempt (`creduce-post/`): strict 3-way predicate
  (`gcc=post-fix-tcc AND pre-fix-tcc disagrees`). Stalled at
  735 lines — every meaningful reduction killed the
  register-pressure trigger. The bug is genuinely sensitive
  enough to the specific allocation order csmith produces that
  it doesn't shrink usefully.

A reduced repro is no longer blocking. If a future session
wants one, restart creduce-post/ from `test.c.orig` and give it
hours, not minutes.

### 3. Sibling r11 — keep an eye on it

r11 is in the same boat as r12 (slot 8 of reg_classes, marked
"scratch" in the comment, hardcoded at ppc-gen.c:1659-1661 in the
rare large-stack-offset path of gfunc_call's struct-arg passing).
That path hasn't surfaced in csmith — it would need a struct arg
spilling to a stack offset >= 32KB, which csmith's small
`--max-funcs 6 --max-array-dim 2 --max-array-len-per-dim 5`
parameters don't produce. If a future seed surfaces it, the same
fix (`reg_classes[8] = 0`) would close it, at the cost of dropping
the int pool to 8.

### 4. (carried from 065 #3) v0.2.48 demo

See #2 above.

### 5. (carried from 065 #4) libtcc1.a clz/ctz to match gcc-PPC

Unchanged. The `builtin_compat.h` shim still papers over this.

### 6. (carried from 065 #5) Get csmith building on at least one PPC host

Unchanged. uranium remains the single source.

### 7. (carried from 065 #6) ibookg38 — back online or write off

Unchanged.

### 8. (carried from 065 #7) Older items, unchanged

OSO STAB / self-link / AltiVec / bcheck.

## How to pick up

### Reproduce this session's smoke

The pre-fix bug (for reference):

```sh
ssh imacg3 '
INC=/Users/macuser/tmp/csmith-runtime
SRC=/Users/macuser/tmp/seed-732.c     # already on host from session 065
TCC=/Users/macuser/tcc-darwin8-ppc/tcc/tcc
/usr/bin/gcc-4.0 -O0 -w -I$INC $SRC -o /tmp/g
$TCC -B/Users/macuser/tcc-darwin8-ppc/tcc -I/Users/macuser/tcc-darwin8-ppc/tcc/include -I$INC $SRC -o /tmp/t
/tmp/g 1 | grep g_359.f0
/tmp/t 1 | grep g_359.f0
'
```

Both lines should now show `...checksum after hashing g_359.f0 : 3B71BCAC`.

### Watch the in-flight re-sweep

```sh
watch -n 60 'ssh ibookg37 "wc -l /Users/macuser/tmp/csmith-out-066-default/SUMMARY.txt
grep -c \"^FAIL \" /Users/macuser/tmp/csmith-out-066-default/SUMMARY.txt
tail -3 /Users/macuser/tmp/csmith-out-066-default/SUMMARY.txt"'
```

### Bisect the bug-class if a similar miscompile resurfaces

The r12 (and r11) hardcoded-scratch sites in ppc-gen.c:

* `ppc_load_fp_const` — `lis r12, ha(literal)` for FP-const load
  from rodata.
* `load()` VT_CONST|VT_SYM path: `addr_gpr = 12`.
* `load()` VT_CONST|!VT_SYM (literal addr) path: `lis r12, ha`.
* `store()` mirror of the above.
* `gfunc_call` struct-arg pass: `mr r12, addr_gpr` then
  `lwz r{slot+3}, off(r12)`.
* `gfunc_call` large-stack-offset path: `lis r11, hi; ori r11, ...;
  stwx r0, r1, r11`.

For r12 the fix is now landed. For r11, if it surfaces, set
`reg_classes[8] = 0`.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

The flavor of this bug — `r12` simultaneously in the allocator and
hardcoded as scratch — is a clean architectural fault, not a
codegen oversight in any one path. Fixing it at the source
(removing r12 from the allocator) is more durable than spreading
`save_reg(TREG_R12)` calls across every hardcoded use site, which
would have closed the same bug but left every new use of r12 as a
fresh tripwire.

The cost is one int register. Tcc's PPC int pool is now r3..r11
(9 slots). For unoptimized code that's almost never tight; if
register pressure ever becomes a perf concern, the right
intervention is hoisting (constant-pool the symbol address once
per function) rather than putting r12 back in the pool.

Next session: [docs/sessions/066-seed-732-2026-05-11/HANDOFF.md](docs/sessions/066-seed-732-2026-05-11/HANDOFF.md)
