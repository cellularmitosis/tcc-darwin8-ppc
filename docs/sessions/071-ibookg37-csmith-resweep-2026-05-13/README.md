# Session 071 — ibookg37 default-opts csmith re-sweep + 064 link cleanup (2026-05-13)

## Arrival state

[Session 070](../070-promote-csmith-shim-2026-05-13/README.md) promoted
the csmith differential-testing harness out of session dirs into
[`scripts/csmith/`](../../../scripts/csmith/). The handoff left three
open items:

1. (optional) Default-opts csmith re-sweep on **ibookg37** — proves
   it's a viable *generator* host now that it has a native
   `/Users/macuser/csmith-2.3.0/bin/csmith` (built session 067),
   not just a corpus-runner.
2. (carried) OSO STAB / self-link diagnostics / AltiVec / bcheck
   multi-session arcs.
3. (low priority) Delete the imacg3 `tcc-darwin8-ppc.bak-pre-069/`
   backup tree.

Plus one latent issue surfaced from session 070 itself: the
pre-/post-move grep that scoped only sessions 062 + 069 + roadmap
missed [session 064](../064-bswap-shim-prototypes-2026-05-10/), whose
README and HANDOFF both link to `bswap_compat.c` and
`csmith_campaign.sh` at their pre-move paths in 062's dir — those
became dangling after 070's `git mv`. The 070 HANDOFF's own smoke-check
incantation (`grep -rn '\](.*bswap_compat\|\](.*csmith_campaign' docs/
| grep -v convos/`) would have flagged it, but the smoke-check wasn't
run as part of 070's wrap-up.

## What was done

### Part 1 — 064 dangling-link cleanup (uranium-side, no test host)

Three markdown link targets in session 064 repointed from
`../062-bswap-shim-builtins-2026-05-10/` to `../../../scripts/csmith/`:

| File | Line | Target |
|---|---|---|
| [064/HANDOFF.md](../064-bswap-shim-prototypes-2026-05-10/HANDOFF.md) | 13 (was 7) | `bswap_compat.c` |
| [064/README.md](../064-bswap-shim-prototypes-2026-05-10/README.md) | 35 (was 25) | `csmith_campaign.sh` |
| [064/README.md](../064-bswap-shim-prototypes-2026-05-10/README.md) | 70 (was 58) | `bswap_compat.c` |

Forward-pointer banners (matching the pattern session 070 added to
[062/README](../062-bswap-shim-builtins-2026-05-10/README.md) and
[069/README](../069-retire-clz-ctz-shim-2026-05-13/README.md)) added to
both [064/README.md](../064-bswap-shim-prototypes-2026-05-10/README.md)
and [064/HANDOFF.md](../064-bswap-shim-prototypes-2026-05-10/HANDOFF.md).

Post-fix `grep -rn '\](.*bswap_compat\|\](.*csmith_campaign' docs/ |
grep -v convos/` now reports all hrefs pointing into either
`scripts/csmith/` (the canonical post-move location) or session 062's
dir (only for `builtin_compat.h`, which stayed in 062 as a retired
historical artifact). The one remaining `070/README.md:76` match is a
backtick-quoted code sample showing the *pre-move* link text in
narrative, not a live hyperlink.

Not in scope: session 062's HANDOFF and findings reference the old
paths only in prose and inside scp-command code blocks (not as
markdown links), so they aren't navigation hazards; the
already-present session-070 banner on 062's README covers those
readers.

### Part 2 — ibookg37 default-opts csmith re-sweep

ibookg37's state at session start:

* tcc binary: `/Users/macuser/tcc-darwin8-ppc/tcc/tcc` — `tcc version
  0.9.28rc (PowerPC Darwin)`, built 2026-05-11 21:25 (post-v0.2.49-g3,
  pre-session-069 csmith re-sweep).
* csmith binary: `/Users/macuser/csmith-2.3.0/bin/csmith` — built
  session 067 from source against `/opt/gcc-4.9.4` libstdc++.
* `/Users/macuser/tmp/`: had stale `bswap_compat.c` +
  `builtin_compat.h` + `csmith_campaign.sh` from May 11 (pre-session-069).
  Re-synced the canonical `scripts/csmith/*` (bswap_compat.{c,h} +
  csmith_campaign.sh) at session start so the deployed copy matches
  what's in git.

#### Generation phase

Fresh corpus generated *on ibookg37 itself* (rather than the prior
pattern of generating on uranium and shipping):

```sh
ssh ibookg37
cd /Users/macuser/tmp/csmith-default-9000
export DYLD_LIBRARY_PATH=/opt/gcc-4.9.4/lib
for n in $(jot 200 9000); do
  /Users/macuser/csmith-2.3.0/bin/csmith --seed $n > seed-$n.c 2>>GEN.log
done
```

Seeds 9000-9199 (200 seeds), default opts (no `--builtins`,
`--bitfields`, etc.). Seed range chosen outside any prior corpus
range (sessions 065/066 used 1-1000 for default and 8020-8419 for
builtins) so the run is genuinely fresh, not a re-execution of
shipped seeds.

#### Campaign phase

```sh
ssh ibookg37
cd /Users/macuser/tmp
bash csmith_campaign.sh 9000 9199 \
  /Users/macuser/tmp/csmith-default-9000 \
  /Users/macuser/tmp/csmith-out-071-default
```

No `EXTRA_CC_SRC` / `EXTRA_CC_HDR` — default-opts csmith doesn't
emit `__builtin_bswap*` or `__builtin_ia32_crc32qi` patterns, so the
shim is unused (this is exactly why default-opts sweeps shipped a
pre-v0.2.49 already).

**Result: PASS=176 FAIL=0 SKIP=24** (campaign elapsed 21m26s,
02:37:18 → 02:58:44 CDT).

All 24 skips were `gcc-timeout` — csmith programs that took >15s
to run under gcc-4.0 -O0 (the campaign script's `RUN_TIMEOUT=15`
perl-alarm cap). No `gcc-compile-fail`, no `tcc-compile-fail`,
no `tcc-timeout`, no `output-diff`, no `exit-mismatch`. Zero
divergence between gcc-4.0 and post-v0.2.49 tcc on this 200-seed
sample.

### Comparison to session 066's default-opts baseline

Session 066 ran 1000 default-opts seeds (1-1000) on ibookg37 with
a uranium-generated corpus and got:

* PASS=873 (87.3%)
* FAIL=0
* SKIP=127 (12.7%), **all `gcc-timeout`**

Session 071 (this one), 200 seeds (9000-9199), ibookg37-generated
corpus:

* PASS=176 (88.0%)
* FAIL=0
* SKIP=24 (12.0%), **all `gcc-timeout`**

Same skip-reason distribution (100% gcc-timeout in both),
near-identical pass rate (within 0.7 pp). The 200-seed sample is
quantitatively in line with the 1000-seed baseline — which is the
"this works" signal we wanted.

## Exit state

* Tree: cleanup + session-071 docs committed; no source change.
* HEAD: this session's docs commit.
* ibookg37: validated as a self-contained generator+runner host —
  no uranium dependency for default-opts corpus generation.

## Open work for next session

Items 2 and 3 from session 070's handoff are unchanged:

* (carried multi-session arcs) OSO STAB / self-link diagnostics
  extension to `macho_output_dylib` / post-write linter / `-vv`
  silent-dropped-relocs diagnostic / sibling r11 watch / AltiVec /
  bcheck. Pick one with the user.
* (low-priority janitorial) Delete
  `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3.

ibookg37-side: a `--builtins+bitfields` generator-host validation
would parallel this session's default-opts one. Lower priority — the
default-opts sweep is the more architectural test of csmith's
generation pathway; the builtins sweep mostly exercises the shim
(which is already exercised by imacg3 in session 069's PASS=352
run).
