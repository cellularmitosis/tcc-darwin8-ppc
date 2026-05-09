# Handoff — end of session 056 (2026-05-09)

## TL;DR

Shipped **v0.2.46-g3**: tcc's compile-time float-to-int
constant-fold now emulates PPC's `fctiwz` saturation, matching
the runtime path. Pre-fix, `int x = (int)1e10f` folded to
`1410065408` (low 32 bits of `(int64_t)1e10`) while runtime tcc
emitted `fctiwz` and got `0x7FFFFFFF` (INT_MAX) — same C source,
two different answers. gcc-4.0 saturates in both paths.

* HEAD: `0aa4690` (demos + roadmap commit).
* `v0.2.46-g3` tag exists locally; **NOT yet pushed to origin**.
* Three csmith campaigns running on imacg3 (background) for
  post-fix validation; will finish over the next 4-6 hours.

## What landed

| Tag | Commits | Headline |
|---|---|---|
| v0.2.46-g3 | `78637d5`..`0aa4690` (2) | **Float-to-int const-fold matches PPC fctiwz semantics.** Pre-fix, the const folder used host `(int64_t)long_double` (libgcc `__fix*di`), which doesn't saturate to int32 range. So `int x = (int)1e10f` folded to `1410065408` while runtime emitted `fctiwz` → `0x7FFFFFFF`. Same source, different answers. Surfaced via csmith differential testing — `--float --builtins` seed-92 had a chained `*(int*)p = (..., huge_float_const)` in func_65 where `g_99` ended at `-1` under tcc but `INT_MAX` under gcc-4.0. Fix: in tccgen.c's `gen_cast` constant-fold branch, on PPC target, emulate fctiwz for byte/short/int targets — saturate `+overflow` to `0x7FFFFFFF`, `-overflow` / NaN to `0x80000000`, else round-toward-zero. VT_LLONG keeps existing path (runtime there matches host helper). tests2 111/111, abitest-cc/tcc 22/22, fixpoint holds, all 9 demos pass. |

## Bug-hunt method

This session continued csmith differential testing (started in
055) but used **manual narrowing** instead of creduce. csmith
programs have built-in tracing (`./prog 1` prints
intermediate per-global checksums), so the divergent global
shows up by diff, then a single round of "instrument each
function's return with `printf(g_99)`" localizes the bug to a
single function call. Manual narrowing took ~5 ssh round-trips
to find the divergent line; creduce was running 4-way parallel
in the background and had reduced 21% → 27% by bytes in the
same time. Different problems suit different tools — for
csmith-shaped programs, the per-global tracing makes manual
faster. (creduce is still the right tool for unstructured
test-case minimization.)

## Open work for next session

### 1. Push tags to origin (if intended)

`v0.2.46-g3` tag is local-only. Prior session pushed
`v0.2.45-g3` to `origin/main`. If parity is desired:

```sh
git push origin main
git push origin v0.2.46-g3
```

### 2. Triage the three queued csmith campaigns

Running on imacg3 in `/Users/macuser/tmp/campaigns-056/`:

| File | Options | Seeds | Purpose |
|---|---|---|---|
| `A-default.txt` | csmith defaults | 1000-1500 (501) | Re-validate same option set that found v0.2.45 |
| `B-complex.txt` | `--max-funcs 12 --max-block-depth 5` | 2000-2300 (301) | Larger / deeper programs |
| `C-float.txt` | `--float` (no --builtins) | 3000-3300 (301) | Re-target float paths since v0.2.46 |

Estimated total runtime: 4-6 hours after session-056 ends.

```sh
# Status check:
ssh imacg3 'tail -3 /Users/macuser/tmp/campaigns-056/progress.log
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
# this session (see findings.md for the recipe), or bring up
# creduce with /tmp/seed92-reduce2/test.sh as a template.
```

### 3. Csmith with --enable-builtin-kinds (still open from 055)

The `--builtins` option set didn't fully run because gcc-4.0
lacks `__builtin_bswap32`/`__builtin_bswap64`/
`__builtin_ia32_crc32qi`. Two paths to unlock that option set:

* Try `--enable-builtin-kinds k1,k2` to filter to PPC-only.
  Csmith strings show kinds tagged `x86`, `clang`, `ppc` —
  worth experimenting with `csmith --builtins
  --enable-builtin-kinds <kind>` to find the right one. (This
  session tried `x86` and got 0 builtins; the kind names may
  not be the platform tags.)
* OR write a `<bswap_compat.h>` that defines the missing
  builtins as inline functions for gcc-4.0 only, and pass it
  via `-include`. The shim would need a portable byteswap
  implementation (just a few lines for bswap32/64).

### 4. Lua testes (still open from 055)

`files.lua` line 84 `assert(io.output():seek() == 0)` fails on
tcc-built lua. Not investigated this session. Worth pulling up
when csmith hits a steady state.

### 5. Dead VT_LLONG store path (still open from 055)

`ppc-gen.c::store()` `case VT_LLONG:` blocks at lines ~1342 and
~1401 are unreachable (framework splits to VT_INT calls). Could
delete + verify with tests2. Not done this session.

### 6. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
git fetch https://github.com/cellularmitosis/tcc-darwin8-ppc.git main
# OR if the local tag is what you want and it's not pushed yet,
# pick it up from uranium directly:
#   git fetch /Users/cell/claude/tcc-darwin8-ppc main
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
./demos/v0.2.46-float-int-const-fold.sh       # PASS  (NEW)
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. Single-thread csmith bug-hunt: sanity baseline,
seed-92 reproduce, manual narrowing, root-cause analysis, fix,
test-suite verification, demo, commit + tag, queue follow-up
campaigns.

## Closing notes

The v0.2.46 fix is structurally similar to v0.2.45 in that both
fix divergence between two tcc paths that should be giving the
same answer:

* v0.2.45: bitfield write (LSB-first byte order) vs read of the
  same bits via gcc-laid-out struct (MSB-first). Internally
  consistent if you only use tcc; ABI-incompatible with the
  outside world.
* v0.2.46: const-fold path (uses host libgcc) vs runtime path
  (uses PPC `fctiwz`). Same C source, different answers
  depending on whether tcc folds at compile time. **Internally
  inconsistent**, even within a single tcc-only program.

Both were found by csmith differential testing in single-digit
seed counts. The pattern argues for keeping csmith in the
regular regression flow (it's already cheap to run a few
hundred seeds in the background overnight; integrating into a
nightly check would be inexpensive).

Next session: [docs/sessions/056-csmith-2026-05-09/HANDOFF.md](docs/sessions/056-csmith-2026-05-09/HANDOFF.md)
