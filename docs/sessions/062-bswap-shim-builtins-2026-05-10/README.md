# Session 062 — bswap shim, csmith --builtins, 5 real bugs surfaced

## TL;DR

Built the `bswap_compat.c` shim per session 061's HANDOFF — gcc-4.0
lacks `__builtin_bswap32`, `__builtin_bswap64`, and
`__builtin_ia32_crc32qi`, all three required by csmith `--builtins`.
Both compilers treat them as extern function calls, so the shim is
just a small `.c` file linked alongside each csmith program.

Then ran two parallel campaigns:

* **builtins+bitfields** on ibookg38, seeds 8020-8419: 343 PASS / 9
  FAIL / 48 SKIP. **First campaign with `--builtins` enabled on PPC.**
* **default-opts past seed 1500** on imacg3 (the carry-forward arm
  from session 061's HANDOFF): in progress at session end, ~25%
  through 1000 seeds, 2 FAILs surfaced so far.

Of the 11 total FAILs across both arms:

* **6 are clz(0)/ctz(0) UB false-positives** — tcc's libtcc1.a
  software de-Bruijn impl differs from gcc-PPC's hardware `cntlzw`
  on UB inputs. Authored a `builtin_compat.h` UB-guard shim that
  routes both compilers through the same wrapper, eliminating the
  divergence. Verified all 6 PASS with the guard.
* **5 are real tcc bugs** that survive the UB-guard:
  * **seed-1536** (default-opts) — tcc-built binary SIGSEGVs.
  * **seed-1705** (default-opts) — tcc=`6413675B`, gcc=`989E0C4E`.
  * **seed-8255** (builtins+bitfields) — tcc=`64984FA0`, gcc=`A2955E73`.
  * **seed-8271** (builtins+bitfields) — tcc-built binary SIGSEGVs.
  * **seed-8389** (builtins+bitfields) — tcc-built binary SIGSEGVs.

3 of the 5 real bugs are SEGVs, hinting at a possible shared root
cause (wild-pointer codegen). Reductions started on seed-1536 (imacg3,
~88% line-reduced at session end) and seed-8271 (ibookg38, just
launched); both running in background as of session close.

## Arrival state

* HEAD `2f7a7e6` ("docs/sessions/061: csmith batches 6+7"), `main`,
  clean tree, in sync with origin.
* Tag: `v0.2.47-g3` already on origin.
* Three consecutive clean csmith campaigns post-v0.2.47 (059/060/061
  across 600+ seeds). Existing axis space well-mined per session 061.
* ibookg38 online as a second G3 host.
* Both hosts validated at v0.2.47 baseline.

## What landed

### bswap shim

`bswap_compat.c` provides three extern-callable functions covering the
gaps in gcc-4.0's builtin set. Both gcc-4.0 and tcc emit
`bl ___builtin_bswap32` (etc.) at link time; neither inlines, so a
shared linker-resolved C body works for both.

The `__builtin_ia32_crc32qi` impl is the canonical reflected
Castagnoli (poly `0x82F63B78`) CRC32 step. Bitwise behavior matches
the x86 SSE4.2 instruction's value, but for differential-testing
purposes any deterministic function of `(crc, data)` would do.

### builtin_compat.h shim (clz/ctz UB-guard)

Discovered mid-session that csmith `--builtins` emits patterns like
`__builtin_clz((void*)0 == g_NN)` where `g_NN` is non-NULL — i.e.
`__builtin_clz(0)`, which is explicitly undefined behavior per the
gcc spec. The two compilers handle the UB differently:

| Builtin | gcc-PPC | tcc libtcc1.a | Match? |
|---|---|---|---|
| `clz(0)`   | 32 | 31 | NO |
| `clzl(0)`  | 32 | 31 | NO |
| `clzll(0)` | 64 | 63 | NO |
| `ctz(0)`   | -1 |  0 | NO |
| `ctzl(0)`  | -1 |  0 | NO |
| `ctzll(0)` | 31 |  0 | NO |

gcc inlines `cntlzw` (PowerISA defines it as 32 for input 0) plus
libgcc helpers. tcc's de-Bruijn-multiply software impl falls through
to `table_2_32[0] = 31` for input 0; ctz uses `x & -x` which is 0
for input 0 → table position 0.

`builtin_compat.h` provides `static inline __compat_clz/ctz/...`
wrappers that short-circuit `x == 0` to gcc-PPC's UB-result, then
macro-shadows the builtins to route through the wrappers. The trick
is to define wrapper bodies *before* the macro shadows so the wrapper
bodies still see the un-shadowed builtins. Both compilers compile the
same C wrapper for clz/ctz calls — the divergence vanishes.

### Updated `csmith_campaign.sh`

Added `EXTRA_CC_HDR` env var alongside the existing `EXTRA_CC_SRC`.
When set, `-include $EXTRA_CC_HDR` is added to both compiler
invocations. Local copy in the session dir; both hosts still have the
older harness that only knows `EXTRA_CC_SRC` (the running campaigns
were launched with the older version).

### Two parallel campaigns

The HANDOFF from session 061 recommended running the carry-forward
"default-opts past seed 1500" sweep as a "0-effort parallel arm"
alongside the new builtins work. Did exactly that:

* imacg3 — `csmith --no-volatiles --no-packed-struct
  --max-funcs 6 --max-block-depth 3 --max-array-dim 2
  --max-array-len-per-dim 5`, seeds 1500-2499 (1000 seeds)
* ibookg38 — same recipe + `--bitfields --builtins`, seeds 8020-8419
  (400 seeds, with `EXTRA_CC_SRC=bswap_compat.c`)

ibookg38's campaign finished mid-session: 343 PASS / 9 FAIL / 48
SKIP. imacg3's runs longer (1000 seeds, slower host); still in
progress at session end with 254 done and 2 FAILs.

### Triage of all FAILs

Confirmed all 11 are real tcc-side divergence (gcc-O0 == gcc-O2 in
each case, ruling out csmith's unspecified-eval-order UB family).
Then checked each builtins+bitfields FAIL with the `builtin_compat.h`
UB-guard and split:

| Seed | Source lines | Symptom | Real or UB-FP? |
|---|---|---|---|
| 1536 | 615 | tcc SIGSEGV | REAL |
| 1705 | 419 | tcc=6413675B, gcc=989E0C4E | REAL |
| 8034 | 1248 | output-diff | UB false-positive (clz/ctz) |
| 8068 | 342 | output-diff | UB false-positive (clz/ctz) |
| 8084 | 685 | output-diff | UB false-positive (clz/ctz) |
| 8228 | 1586 | output-diff | UB false-positive (clz/ctz) |
| 8255 | 877 | output-diff | REAL |
| 8271 | 1261 | tcc SIGSEGV | REAL |
| 8312 | ? | output-diff | UB false-positive (clz/ctz) |
| 8356 | ? | output-diff | UB false-positive (clz/ctz) |
| 8389 | 1248 | tcc SIGSEGV | REAL |

The default-opts campaign continues — there may be more FAILs by the
time it finishes (~80 min after session end at observed pace).

### Reductions started

* `seed-1536-repro/` — creduce on imacg3, single-thread,
  ssh-per-iteration. Reduced from 615 → 75 lines (88% by line count,
  8% by byte count) by session end. Predicate: tcc binary
  signal-killed but gcc binary clean exit.
* `seed-8271-repro/` — creduce on ibookg38, single-thread,
  ssh-per-iteration. Just launched at session end; first reduction
  pass barely started (1261 → 176 lines so far). Predicate same as
  1536; uses bswap shim + builtin_compat.h on the compile lines.

Both reductions running as background processes on uranium when the
session ended. They will keep grinding overnight; future session can
pick up the reduced sources from the `*_repro/test.c` files.

## Exit state

* Branch: `main`, no commits in this session yet (docs commit pending).
* Working tree: clean tcc/, new session dir under
  `docs/sessions/062-bswap-shim-builtins-2026-05-10/`.
* Two campaigns running in background:
  * imacg3: 254/1000 default-opts seeds done, 2 FAILs.
  * ibookg38: DONE (343/9/48).
* Two creduce reductions running in background:
  * seed-1536: 75 lines, ~10% reduced.
  * seed-8271: 176 lines, just started.
* tcc binary on both hosts is still v0.2.47 (no source changes this
  session). Sanity-baseline checks all pass.

## Open work for next session

See [HANDOFF.md](HANDOFF.md) for the next-session brief.
