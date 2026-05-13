# Handoff — end of session 069 (2026-05-13)

## TL;DR

Retired the clz/ctz half of `builtin_compat.h`. Empirically
confirmed (no FAIL count change, no PASS-set change) that the
six UB-wrapping macros for `__builtin_clz / clzl / clzll / ctz /
ctzl / ctzll` were dead weight after v0.2.49-g3's libtcc1.a
short-circuits made tcc match gcc-PPC's observed UB behavior
directly.

* **HEAD at session start:** `c264906` (end of session 068).
* **HEAD at session end:** (this session's docs commit).
* **No tcc source change. No version bump. No new demo.**
* **New artifact:** [`bswap_compat.h`](../../../scripts/csmith/bswap_compat.h) — the
  trimmed successor to session 062's `builtin_compat.h`. Just
  three `extern` prototypes (bswap32, bswap64, ia32_crc32qi); no
  clz/ctz wrapping. Future csmith `--builtins` campaigns
  `-include` this instead.

**Regression suite:** all green on a from-scratch rebuild on imacg3.
* tests2: 111/111.
* abitest cc + tcc: all `success`.
* Sampled demos: `v0.2.50-self-link-diagnostics`,
  `v0.2.49-clz-ctz-ub`, `v0.2.48-r12-clobber`, `v0.2.42-python` —
  all PASS.

**Csmith re-sweep:** 400 seeds (8020-8419, `--builtins+bitfields`
corpus from session 065/066) with the trimmed shim:
**PASS=352 FAIL=0 SKIP=48** — byte-identical seed-by-seed to
session 066's baseline (also `352 / 0 / 48`). Empirical
confirmation that the clz/ctz shim was no longer load-bearing.

## What landed

* [`bswap_compat.h`](../../../scripts/csmith/bswap_compat.h) —
  trimmed shim header. Three `extern` prototypes for builtins
  gcc-4.0 lacks (or treats as ordinary externs). Comment block
  explains why each is still needed. (Originated in this session
  dir; promoted to `scripts/csmith/` in session 070.)

* [`README.md`](README.md) — full narrative including the
  verification log and the build-infra gotchas that surfaced when
  rebuilding imacg3 from a fresh sync.

* [`HANDOFF.md`](HANDOFF.md) — this file.

* `docs/roadmap.md` — annotation under v0.2.49-g3 noting the
  shim retirement.

Out of git (host state on imacg3):
* `/Users/macuser/tmp/bswap_compat.h` — deployed copy of the
  trimmed shim.
* `/Users/macuser/tmp/csmith-out-069/SUMMARY.txt` — campaign log.
* `/Users/macuser/tcc-darwin8-ppc/` — tree freshly rsync'd from
  uranium and rebuilt with `--config-semlock=no`. The pre-069
  state is at `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` (kept
  for one session as a safety net; can be deleted in 070+ if
  nothing's needed from it).

## Status of session 067 / 068 open items

| # | Item | Status |
|---|---|---|
| (068 #2) | Extend self-link diagnostics to `macho_output_dylib` | unchanged |
| (068 #3) | Post-write linter (otool/nm sanity over emitted bytes) | unchanged |
| (068 #4) | `-vv` diagnostic for reader's silent dropped relocs | unchanged |
| (067 #3) | Sibling r11 watch | unchanged (no surface yet) |
| (067 #4) | **Trim or retire `builtin_compat.h`** | **landed (this session)** |
| (067 #5) | **Real csmith `--builtins` campaign without the shim** | **landed (this session)** |
| (carried) | OSO STAB / AltiVec / bcheck | unchanged |

## Open work for next session

### 1. (optional) Promote the shim to a stable location

`bswap_compat.h` and `bswap_compat.c` live in session dirs (062 /
069). They're reusable test infrastructure, not session-specific
artifacts. Moving them to `scripts/csmith/` (or `tests/csmith/` if
we ever grow a `tests/` dir) would make them less archaeological
and easier to discover. Small docs/move change. Not blocking.

### 2. (optional) Run a default-opts csmith re-sweep on ibookg37

Session 066 also ran a default-opts sweep on ibookg37
(`PASS=873 FAIL=0 SKIP=127`). That sweep didn't use the
`-include` shim at all (csmith without `--builtins` doesn't emit
clz/ctz patterns), so v0.2.49 wouldn't change its results. But
since session 067 built a native csmith at
`/Users/macuser/csmith-2.3.0/bin/csmith` on ibookg37, a small
on-host re-generation campaign would prove ibookg37 is a viable
generator host — not just a corpus-runner. Independent of this
session's work; would be a session 070 kickoff if we want it.

### 3. (carried) OSO STAB / self-link / AltiVec / bcheck

Unchanged. Each is its own multi-session arc; pick one with the
user.

### 4. (optional) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

The pre-069 imacg3 tree (which had a flattened layout from prior
rsync mishaps) is kept as a safety net. Once we're confident the
new layout works (after a couple more rebuild cycles), it can be
deleted. Low priority.

## How to pick up

### Verify the post-069 state on imacg3

```sh
ssh imacg3 '
  cd /Users/macuser/tcc-darwin8-ppc/tcc &&
  ./tcc -v &&
  /opt/tigersh-deps-0.1/bin/bash ../scripts/run-tests2.sh 2>&1 | tail -3
'
```

Expected: tcc PowerPC Darwin banner; `Total: 111 Pass: 111`.

### Reproduce the csmith re-sweep

```sh
ssh imacg3 '
  cd /Users/macuser/tmp &&
  EXTRA_CC_SRC=/Users/macuser/tmp/bswap_compat.c \
  EXTRA_CC_HDR=/Users/macuser/tmp/bswap_compat.h \
  bash csmith_campaign.sh 8020 8419 \
    /Users/macuser/tmp/csmith-builtins-8020 \
    /Users/macuser/tmp/csmith-out-069-rerun
'
```

Expected: `PASS=352 FAIL=0 SKIP=48` after ~40 minutes.

### Rebuild tcc on imacg3 from scratch (in case of broken state)

The two non-obvious bits, both captured in
[`README.md`](README.md):

```sh
ssh imacg3 '
  cd /Users/macuser/tcc-darwin8-ppc/tcc &&
  ./configure --cc=gcc-4.0 --config-semlock=no &&
  rm -f *.o lib/*.o libtcc.a libtcc1.a c2str.exe tcc &&
  /opt/make-4.3/bin/make
'
```

Tiger doesn't have `dispatch/dispatch.h` (libdispatch/GCD is 10.6+),
so the default semlock path won't compile. `--config-semlock=no`
disables it. And the c2str.exe + arm64 .o files in the source
tree are uranium build leftovers that ar/ranlib chokes on; clean
them before building.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

A small cleanup session — no tcc source change, just retiring the
shim half that v0.2.49 had already obsoleted. The cleanest part
of the result is that **session 069's PASS set is identical
seed-by-seed** to session 066's PASS set: not just the same count,
but the same set of seeds. That's strong empirical evidence that
the clz/ctz shim was dead weight and removing it changes nothing.

Worth knowing for next time: rebuilding tcc on a freshly-synced
imacg3 has two non-obvious sharp edges (the `--config-semlock=no`
flag and the stale uranium-build artifacts), both now captured in
the README and in the rebuild incantation above.

Next session: [docs/sessions/069-retire-clz-ctz-shim-2026-05-13/HANDOFF.md](docs/sessions/069-retire-clz-ctz-shim-2026-05-13/HANDOFF.md)
