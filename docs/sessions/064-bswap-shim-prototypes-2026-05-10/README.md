# Session 064 — bswap shim prototypes (2026-05-10)

> **Note (session 071):** the `bswap_compat.c` and `csmith_campaign.sh`
> files this session worked with were moved to
> [`scripts/csmith/`](../../../scripts/csmith/) in session 070; markdown
> link targets below have been redirected to the new location.
> `builtin_compat.h` (which this session added the three `extern`
> prototypes to) was retired by session 069 in favour of the trimmed
> [`bswap_compat.h`](../../../scripts/csmith/bswap_compat.h), but the
> original file stays in session 062's dir as a historical artifact.
> The narrative below is unchanged from session-064's exit state.

## Arrival state

Session 063 closed 4 of 5 csmith bugs from session 062 with a single
4-line `save_reg()` fix in `tcc/ppc-gen.c`. The remaining one,
**seed-8255** (a `--builtins`+`--bitfields` output divergence), was
classified as a "different bug class — likely worth its own focused
session" and handed off as the headline open work item.

The handoff also listed:
2. Ship updated harness to both hosts (deferred maintenance).
3. Patch tcc's libtcc1.a clz/ctz to match gcc-PPC.
4. Run a fresh post-fix csmith campaign.
5. Older items: OSO STAB, AltiVec, bcheck, etc.

This session picked up #2 as warmup and #1 as the main bug-hunt. It
turned into a #3-adjacent finding that **the bug was in the test
harness, not in tcc**.

## What was done

### #2 — Shipped updated harness (warmup)

`scp` of [`csmith_campaign.sh`](../../../scripts/csmith/csmith_campaign.sh)
(at the time of this session it lived in `docs/sessions/062-bswap-shim-builtins-2026-05-10/`;
promoted to `scripts/csmith/` in session 070)
to both `imacg3:/Users/macuser/tmp/csmith_campaign.sh` and
`ibookg38:/Users/macuser/tmp/csmith_campaign.sh`. The local copy
gained `EXTRA_CC_HDR` support during session 062; hosts were
out-of-sync.

### Main task — seed-8255 root-caused as a shim/UB issue

Reproduced the divergence on ibookg38 using the post-063 tcc binary:

```
gcc: ...checksum after hashing g_181[i] : 7DF4B4D6
tcc: ...checksum after hashing g_181[i] : 111DC160
```

Exactly matching the handoff. First divergent value is `g_181[1]`
(g_181 is a 2-element `int32_t` array initialized to `{-1L, -1L}`).

Counted writes to `g_181[*]` in the source — 5 sites, one of which
(line 178) involves `__builtin_bswap64`:

```c
g_181[g_88] = __builtin_bswap64(g_181[g_357]);
```

tcc's compile log shows `warning: implicit declaration of function
'__builtin_bswap64'`. Followed that thread.

#### The shim's missing prototypes

[`builtin_compat.h`](../062-bswap-shim-builtins-2026-05-10/builtin_compat.h)
provides clz/ctz UB-guard wrappers but no prototypes for the
companion shim's functions in
[`bswap_compat.c`](../../../scripts/csmith/bswap_compat.c):

```c
unsigned int       __builtin_bswap32(unsigned int x);
unsigned long long __builtin_bswap64(unsigned long long x);
unsigned int       __builtin_ia32_crc32qi(unsigned int crc, unsigned char data);
```

Without prototypes, csmith-emitted call sites fall back to K&R C89
implicit declaration: return `int`, args promoted to `int`. On
PPC32, that means:

| signature | r3 in | r4 in | r3 out | r4 out |
|---|---|---|---|---|
| true: `unsigned long long(unsigned long long)` | arg high | arg low | return high | return low |
| implicit: `int(int)` | arg | (junk) | return | (clobbered, unread) |

So the actual `__builtin_bswap64` reads `r3:r4` as a 64-bit value, but
the caller only set up `r3`. The low 32 bits of the bswap input are
whatever happened to be in `r4`. The byte-swap of that ends up in `r3`
(high half of the result), which the caller then reads as the int32
return value. **Pure undefined behavior, dependent on the surrounding
register state.**

#### Why the bug was hidden

The other six builtins seeds that *passed* the session-062 campaign
(`8034`, `8068`, `8084`, `8228`, `8312`, `8356`) include several with
multiple bswap calls (e.g. `8084` has 5). They passed because gcc-4.0
and tcc happened to leave matching garbage in `r4` at every bswap call
site in those programs. seed-8255 is the first surfaced case where the
two compilers' surrounding codegen leaves *different* garbage in `r4`.

#### Confirmation

Isolated reproducer with implicit-decl: gcc and tcc both produce the
same garbage value (`0x7CFCFFBF`) for `__builtin_bswap64((int32_t)-1L)`.
With explicit prototype: both produce `0xFFFFFFFFFFFFFFFF` (correct).

So the implicit-decl path is *coincidentally* matching in tiny tests
but UB-divergent in complex programs. Adding `extern` prototypes
forces both compilers onto the correct 64-bit ABI, eliminating the UB.

### Fix

Added three lines (plus a comment paragraph) to
[`docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h`](../062-bswap-shim-builtins-2026-05-10/builtin_compat.h):

```c
extern unsigned int       __builtin_bswap32(unsigned int x);
extern unsigned long long __builtin_bswap64(unsigned long long x);
extern unsigned int       __builtin_ia32_crc32qi(unsigned int crc, unsigned char data);
```

Shipped to both hosts:

```
scp builtin_compat.h imacg3:/Users/macuser/tmp/builtin_compat.h
scp builtin_compat.h ibookg38:/Users/macuser/tmp/builtin_compat.h
```

### Validation

All seven builtins seeds from session 062 now produce identical
gcc/tcc checksums:

| Seed | gcc/tcc checksum |
|---|---|
| 8034 | E0F01ED1 |
| 8068 | 50BFF248 |
| 8084 | 5E2FBE5E |
| 8228 | 4EC64F4D |
| **8255** | **A13B597A** |
| 8312 | 64962966 |
| 8356 | AB8FF434 |

(The first six were already in-sync but for accidental reasons; they
remain correct after the prototype fix. seed-8255 is newly closed.)

A 100-seed `--builtins`+`--bitfields` campaign (`8020-8119`) is
running on ibookg38 to confirm zero FAILs across a broader sample;
results will be appended to this README.

## Exit state

* **All 5 of session-062's surfaced bugs are now closed** (4 by the
  session-063 codegen patch, 1 by this session's shim fix).
* No tcc source touched in this session — the change is one extra
  comment + 3 prototypes in `builtin_compat.h`. tcc binaries on both
  hosts remain post-063 (the gfunc_call save_reg fix).
* Tag `v0.2.47-g3` is on origin; v0.2.48 candidate is now the
  session-063 commit + (optional) docs commits from this session.

## Notes for next session

See [`HANDOFF.md`](HANDOFF.md).
