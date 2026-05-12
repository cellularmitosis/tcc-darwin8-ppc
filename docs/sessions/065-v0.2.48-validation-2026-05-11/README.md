# Session 065 — v0.2.48 validation campaign (2026-05-11)

## Arrival state

Session 064 closed the last of session 062's 5 csmith bugs (the
seed-8255 shim-UB issue), leaving 4 carry-forward open items in the
HANDOFF:

1. Tag v0.2.48-g3 to mark "all 5 csmith bugs closed".
2. Run a fresh 400-seed `--builtins+bitfields` campaign + a 1000-seed
   `default-opts` campaign as cumulative-fix confirmation.
3. Patch libtcc1.a clz/ctz to match gcc-PPC (lets `--builtins` seeds
   pass without `-include`).
4. Older items (OSO STAB, AltiVec, bcheck, self-link diagnostics).

This session picked up #2 (the full validation campaigns) and #1
(tagging on a clean result).

The post-063 tcc binary on ibookg38 was already validated by session
064's 100-seed appendix (82 PASS / 0 FAIL / 18 gcc-timeouts SKIP).
This session widens that to a full 400-seed `--builtins+bitfields`
sweep plus a 1000-seed `default-opts` sweep, on a third PowerPC host
(**ibookg37**, brought into the fleet for this work).

## Host setup detour — ibookg38 went flaky

Mid-session ibookg38 (the host that had the seed corpus + only
csmith binary in the fleet) became unreachable mid-transfer:

```
ssh: connect to host ibookg38 port 22: Operation timed out
ssh: connect to host ibookg38 port 22: No route to host
```

The user offered **ibookg37** (idle PowerPC G3, Mac OS X 10.4.11)
as a replacement and confirmed pulling whatever we needed from
ibookg38 would be fine.

The trickier discovery: neither ibookg37, imacg3, nor the main Mac
repo had a csmith binary or source tarball locally. The only
csmith binary in the fleet was on ibookg38 (now offline).

**Resolution**: csmith 2.3.0 is installed on uranium (the main Mac)
via Homebrew at `/opt/homebrew/bin/csmith`. csmith is deterministic
given identical options + seed (auto-creates `platform.info` with
`integer size = 4`, `pointer size = 8`), so we can regenerate the
session-062 corpus bit-for-bit body identity on uranium and ship.
Verified by regenerating seed-8255 locally and diffing against the
in-repo `fails/seed-8255.c`:

```
$ diff /tmp/csmith-test/seed-8255.c \
       docs/sessions/062-bswap-shim-builtins-2026-05-10/fails/seed-8255.c
6c6
<  * Options:   --no-volatiles --no-packed-struct --bitfields --builtins ... -o seed-8255.c
---
>  * Options:   --no-volatiles --no-packed-struct --bitfields --builtins ... -o /tmp/csmith-test/seed-8255.c
```

Only the `-o` filename differs in the comment header. Program body
identical.

### Setup steps on ibookg37

1. Backed up ibookg37's existing tcc + libtcc1.a (older May-6 build):
   ```
   cp -p tcc tcc.pre-065.bak
   cp -p libtcc1.a libtcc1.a.pre-065.bak
   ```
2. Streamed the post-063 tcc + libtcc1.a from ibookg38 to ibookg37
   while ibookg38 was briefly reachable (~5 min window before it
   dropped again):
   ```
   ssh ibookg38 'cd /Users/macuser/tcc-darwin8-ppc/tcc && tar czf - tcc libtcc1.a' \
     | ssh ibookg37 'cd /Users/macuser/tcc-darwin8-ppc/tcc && tar xzf -'
   ```
3. Pushed harness files from the repo to ibookg37:
   - `builtin_compat.h` (session-064 version with bswap/crc32 prototypes)
   - `bswap_compat.c`
   - `csmith_campaign.sh` (session-062, EXTRA_CC_HDR-aware)
4. Symlinked `/Users/macuser/tmp/csmith-runtime → /Users/macuser/tmp/csmith`
   on ibookg37 (the campaign script's hard-coded RT path).
5. Regenerated and shipped two seed corpora:
   - `csmith-builtins-8020/` — 400 seeds (8020-8419),
     `--no-volatiles --no-packed-struct --bitfields --builtins`
   - `csmith-default-1k/` — 1000 seeds (1-1000), default-opts.

## Smoke test (warmup)

Re-ran the HANDOFF.md's 7-seed smoke recipe plus the 4 ppc-gen-fix
seeds (1536, 1705, 8271, 8389) on ibookg37 using the post-063 tcc
binary. All 10 seeds pass with checksums matching the session-064
HANDOFF table:

| Seed | Source | Status | Checksum |
|---|---|---|---|
| 1536 | default-opts | PASS | 19FE6CAA |
| 1705 | default-opts | PASS | 989E0C4E |
| 8068 | builtins+bf | PASS | 50BFF248 |
| 8084 | builtins+bf | PASS | 5E2FBE5E |
| 8228 | builtins+bf | PASS | 4EC64F4D |
| 8255 | builtins+bf | PASS | A13B597A |
| 8271 | builtins+bf | PASS | 34503DCB |
| 8312 | builtins+bf | PASS | 64962966 |
| 8356 | builtins+bf | PASS | AB8FF434 |
| 8389 | builtins+bf | PASS | 6422824E |

Validates that the post-063 tcc binary on ibookg37 produces
output identical to the session-064 results from ibookg38. The
session-062 codegen fix + session-064 prototype fix are both
operational on a fresh host.

## Validation campaigns

Two campaigns ran sequentially on ibookg37 via
`/Users/macuser/tmp/065-launch.sh`. Total wall time **1h23m**
(18:59:45 → 20:23:11 local).

### Phase 1 — `--builtins+bitfields`, seeds 8020-8419 (400 seeds)

| PASS | FAIL | SKIP | Wall |
|---|---|---|---|
| 352 | **0** | 48 | 28m |

All 48 SKIPs are gcc-timeouts (csmith-generated programs that
exceeded the 15-second `RUN_TIMEOUT` under gcc-4.0 -O0). Zero
tcc-side failures, zero output divergences. This corroborates and
extends session 064's appendix 100-seed run (82 PASS / 0 FAIL / 18
SKIP from the 8020-8119 sub-range).

### Phase 2 — `default-opts`, seeds 1-1000 (1000 seeds)

| PASS | FAIL | SKIP | Wall |
|---|---|---|---|
| 872 | **1** | 127 | 55m |

127 SKIPs (all gcc-timeouts). **One real FAIL**: `seed-732`
output-diff. Captured under `seed-732-fail/` in this directory:

* `seed-732.c` — the csmith program (922 lines, default-opts)
* `seed-732.gcc.out` — gcc-4.0 -O0 per-global checksum stream
* `seed-732.tcc.out` — tcc post-063 per-global checksum stream

### seed-732 first-look triage

Per-global checksums agree on `g_2`, `g_26.f0`, `g_48`, `g_52`,
`g_75`, `g_55`, … `g_356`, then **diverge starting at `g_359.f0`**:

```
< ...checksum after hashing g_359.f0 : 3B71BCAC   (gcc)
> ...checksum after hashing g_359.f0 : F3115DB3   (tcc)
```

All later checksums also differ (the rolling CRC carries forward
the mismatch). The bug is therefore in how `g_359.f0` ends up by
the time `main()` reaches its hashing call.

`union U0 { int8_t f0; }` static `g_359 = {0xA4L}`. The seed
declares one function that takes `union U0` by **value as middle
argument**:

```c
static int32_t * func_8(int8_t p_9, int32_t p_10,
                        union U0 p_11,
                        int32_t * p_12, uint16_t p_13);
```

(Note: this is a **middle** argument, not the last — distinct from
session-063's "struct/union as last arg" fix shape. `func_8` is
called once from `func_1` with `g_359` passed by value as `p_11`.)

`g_359.f0` is written by both `func_1` and `func_5` per the
csmith-emitted r/w-set comments. Multiple sites:

```c
for (g_359.f0 = 0; (g_359.f0 >= 0); g_359.f0 -= 1) ...
... (g_265 ^= ... , g_359.f0), 0L) ...
```

**UB sanity-check passed** (gcc -O0 and -O2 produce identical
checksum 76F5DB56), so this is *not* an "undefined-behavior
choice differs between optimizers" false positive. It looks like
a real tcc codegen bug.

Full root-cause + fix is **deferred to a follow-up session**.
This session's scope is the validation sweep + v0.2.48 milestone
decision; surfacing one new bug doesn't change the closed status
of the original five 062 bugs.

## Exit state

* **All 5 of session-062's surfaced bugs remain closed** (verified
  by the smoke + the 0 FAILs in the builtins+bitfields sweep that
  contains the 062 fail seeds).
* **1 new bug surfaced**: seed-732, default-opts, real
  output-diff (not UB). Carried forward.
* Net validation: 1224/1400 seeds (87.4%) PASS, 175 SKIP, 1 FAIL.
  Of non-SKIP seeds: 1224/1225 ≈ 99.92% PASS.
* No tcc source changes this session.
* ibookg37 is now a fully-provisioned PowerPC test host for the
  fleet.

## Notes for next session

See [`HANDOFF.md`](HANDOFF.md).
