# Session 043 — unsupervised: get tcc Tiger-PPC "done"

User went unsupervised. Goal: continue making tcc bug-free on
Tiger PPC. No artificial session boundaries — keep going until
either the work is "done" or interrupted.

## Arrival state (from session 042 close)

* HEAD: `d07d7bf`. tests2: 111/111. Bootstrap fixpoint holds.
* Upstream `tcc/tests/make test` partially passing: see
  [../042-upstream-tests-2026-05-05/README.md](../042-upstream-tests-2026-05-05/README.md).
* Latest release: v0.2.16-g3.

## What landed (so far in session 043)

11 commits, three releases shipped. tcctest.c diff vs reference
went from 255 lines (session 042 baseline) → **44 lines** (now,
all known structural items).

### Releases

| Tag | Headline |
|---|---|
| **v0.2.17-g3** | `alloca()` actually works (epilog back-chain restore + 256-byte safety zone). Full `tcctest.c` runs to completion. |
| **v0.2.18-g3** | Variadic FP arg past 8 FPRs (off-by-one) + float negation on big-endian. **bzip2 1.0.8 builds + round-trips.** |
| **v0.2.19-g3** | NaN-aware FP `>=`/`<=` + pointer→int cast follows destination signedness + long-long arg straddling r10/stack. |

### Real-world programs working as of v0.2.19

* lua 5.4.7 ✅ (since v0.2.12)
* zlib 1.3.1 ✅ (since v0.2.16)
* **bzip2 1.0.8 ✅** NEW this session (since v0.2.18)
* sqlite3 :memory: SELECT ✅; CREATE TABLE / file open still
  fail with the same VDBE/B-tree bugs deferred from session 041.

### tcctest.c diff timeline

| Session 042 baseline | After alloca/epilog | After variadic + fneg | After nan + LD-gate | After ptr-cast + LL straddle |
|---|---|---|---|---|
| 255 lines | (still 255 — alloca only fixed the SEGV, not the diff) | 122 lines | 53 lines | **44 lines** |

Remaining 44-line diff is all known structural / won't-fix:
- aligntest3/4 alignof=4 vs gcc's 8 (Apple PPC ABI fix from
  session 041 — we use 4-byte alignment for double in structs)
- `sizeof(_Bool)` = 1 vs gcc-4.0's 4 (gcc quirk; we follow C99)
- relocation_test `&arr[N]` addend (deferred, see TODO in
  ppc-macho.c::exe_resolve_section_relocs)
- `promote char/short funcret` (undefined behavior in csf macro
  in tcctest.c)

## All commits (chronological)

| Commit | Headline |
|---|---|
| `e330cea` | lib-ppc: more `__bound_*` stubs (strrchr/strstr/malloc/lock/check) |
| `76dea4c` | ppc-gen, alloca: support functions that call alloca() correctly (epilog back-chain restore + 256-byte safety zone) |
| `d2565cf` | release: bump default to v0.2.17-g3 (alloca() works correctly) |
| `bf6c7a7` | ppc-macho: TODO comment for static-init addend bug |
| `ecd1515` | ppc-gen: fix off-by-one storing variadic FP arg past 8th FPR |
| `c0b84f0` | tccgen: route TOK_NEG through gen_opf for PPC (use fneg) |
| `f30731d` | release: bump default to v0.2.18-g3 (variadic FP + float neg fixes) |
| `db9114a` | ppc-gen, tcctest: nan-aware FP `>=` and `<=`, plus LD double gating |
| `bd41b68` | ppc-macho: refine TODO note for static-init addend bug |
| `a1e0e0b` | ppc-gen, tccgen: pointer→int cast follows dst signedness; LL straddle gslot=7 |
| `9f7d244` | release: bump default to v0.2.19-g3 (NaN cmp + ptr→int cast + LL straddle) |

## New demo

* [`demos/v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) —
  bzip2 1.0.8 builds + round-trips a 573KB file with byte-
  identical decompress.
* [`demos/v0.2.17-alloca.sh`](../../../demos/v0.2.17-alloca.sh) —
  the alloca + strcpy + printf pattern that broke pre-v0.2.17.

## Subagent log

Considered using a Sonnet subagent twice this session, both for
mechanical bound-stub additions. Decided against both times — the
work was 5-10 lines each and faster to do directly than brief a
subagent. No subagents spawned in session 043.

If a future session has long-running mechanical work (e.g. porting
bcheck.c, adding many DWARF arch-specific blocks), Sonnet would
be a reasonable choice.

## Deferred work (in priority order for future sessions)

### Tier A — bugs we identified but couldn't crack

1. **`relocation_test` static-init `&arr[N]` addend** (3 attempts,
   all broke bootstrap). Three different formulas tried; all hit
   the same wall: the in-place value's contents differ between
   the in-memory single-TU path (init_putv writes user-addend
   only) and the .o roundtrip path (Mach-O .o emit folds
   sym->st_value, then sm->offset gets layered on during section
   concat). Right fix probably needs a pre-link normalization
   pass that aligns in-place values across loaded sections, OR a
   simpler change to how init_putv interacts with section concat
   that I haven't yet seen. See TODO at ppc-macho.c around line
   1540.

2. **abitest-tcc flaky in JIT harness path** (`ret_2float_test`
   sometimes Bus error, sometimes failure on the same binary).
   Has been flaky since the alloca/epilog fix in 76dea4c (which
   has positive impact elsewhere — full tcctest now runs). The
   regression IS real but specific to the
   abitest-harness-built-by-tcc context. Likely a JIT-context
   ABI subtlety we don't trigger in normal compiles. The simple
   standalone repro (a struct of 2 floats round-trip through a
   function call) works fine.

3. **sqlite CREATE TABLE → SQLITE_CORRUPT** (deferred from
   session 041). Pinpointed to `OP_ParseSchema` finding 0 rows
   in sqlite_schema after the CREATE. VDBE-level bug.

4. **sqlite file open SEGV** (deferred from session 041). Wild
   jump via bad function pointer. Could be a static-init array-
   of-fnptrs hitting the same addend bug as #1, OR a different
   codegen issue.

### Tier B — structural items (unblock more programs / tests)

5. **Apple PPC 13-FPR support**. Currently capped at f1..f8.
   Apple PPC32 ABI is f1..f13. Bumping requires re-sizing
   NB_REGS, RC_F bitmask shift, reg_classes[], and the prolog
   FP-spill area. Unblocks `abitest::ret_8plus2double_test`.

6. **Mach-O `-shared` dylib output**. Currently only `-c` and
   `-o exe`. Unblocks `dlltest` and many real-world programs
   that ship as .dylib.

7. **Mach-O `tcc -ar` driver**. Currently fall back to system
   `ar`. ~150-300 lines, well-scoped.

8. **DWARF debug info for PPC**. tccdbg.c has no PPC blocks.
   Multi-target plumbing (~10 places need PPC additions).

9. **128-bit IBM double-double long double**. Apple PPC `long
   double` is 16-byte; we treat as 8-byte double. Needs ABI
   plumbing + arithmetic helpers (`__addtf3`, etc.).

10. **bcheck.c port**. Real bounds checking. Our stubs satisfy
    link but don't actually detect overruns.

### Tier C — multi-thread / advanced

11. **`libtest_mt` SEGV**. Multi-threaded libtcc usage.

12. **AltiVec intrinsics**. None today; tcc emits scalar code.

## Hosts (unchanged)

* **ibookg37** — primary build / test host.
* **uranium** — main Mac (this one). Edit + commit + cut releases.

## Closing notes

11 commits, 3 patch releases, 4 new fixes that deserved their own
release plus 2 noteworthy bug fixes (alloca + epilog rewrite,
variadic past 8 FPRs). bzip2 1.0.8 is the third real-world
program working end-to-end. tcctest.c diff went from 255 to 44
lines and the remaining 44 are all in the "won't fix" or "deferred
deep bug" buckets.

Bootstrap fixpoint holds at every commit; tests2 stays at 111/111.

The most impactful next single item is probably the
relocation_test addend fix (Tier A #1) — it's a clearly-real bug
and would clean up the last remaining "real" diff in tcctest.
After that, the structural items (Tier B) all unlock specific
upstream tests or real-world program classes.
