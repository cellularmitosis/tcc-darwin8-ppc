# Handoff — end of session 055 (2026-05-09)

## TL;DR

One release shipped this session: **v0.2.45-g3** — BE PowerPC
bitfield ABI fix (tcc was laying out bitfields LSB-first within the
storage container; SysV/AIX PowerPC ABI is MSB-first on big-endian).
Found via csmith differential testing (200 random C programs vs
gcc-4.0); seed 3 reduced to a 16-line repro. tests2 still 111/111;
abitest-cc/tcc each 22/22; bootstrap fixpoint holds; all 9 demos
still pass.

* HEAD: `ea6a37c` (post-v0.2.45 demo + roadmap update).
* `v0.2.45-g3` tag pushed to `origin/main`.

## What landed

| Tag | Commits | Headline |
|---|---|---|
| v0.2.45-g3 | `fc241f5`..`ea6a37c` (2) | **BE PPC bitfield ABI fix.** Pre-fix, tcc on PPC laid out bitfields LSB-first within their storage container; the SysV/AIX PowerPC ABI uses MSB-first on big-endian. Internally consistent for tcc-only programs (struct write+read round-trips), but ABI-incompatible: `union {uint32_t f0; unsigned f1:14;}; u.f0 = 0x024A1329; read u.f1` gave `0x0A02` (LSB-first byte order) instead of the standard `0x092` (top 14 bits of the BE word). Also affected pointer-cast access. Fix: `#if defined(TCC_TARGET_PPC) && !defined(TCC_TARGET_PPC64)` branches in tccgen.c's three byte-wise bitfield paths (`load_packed_bf`, `store_packed_bf`, `init_putv`'s VT_BITFIELD branch) interpret bit_pos as MSB-first within the container, walk bytes from MSB end (low memory address on BE) toward LSB end, and accumulate result bits MSB-first. struct_decl's bit_pos counter is unchanged. |

## Bug-hunting methodology added

This session brought up **csmith differential testing** as a
systematic bug-finding methodology:

* Csmith 2.3.0 + creduce 2.10.0 installed via brew on uranium.
  Csmith runtime headers shipped to imacg3 at
  `/Users/macuser/tmp/csmith-runtime/`.
* Campaign workflow: generate seeds on uranium with
  `csmith --no-volatiles --no-packed-struct --max-funcs 6 ...`,
  scp to imacg3, compile each with both `gcc-4.0 -O0` and
  `tcc -B... -L...`, diff the printed checksum.
* 200 default-options seeds → 1 bug (BE bitfield, fixed). After fix,
  173 PASS / 27 false-alarm both-timeouts / 0 real bugs.
* 100 aggressive (`--float --builtins`) seeds → many SKIP because
  gcc-4.0 lacks `__builtin_bswap32` etc.; otherwise clean.

The campaign script and analyzer live in
`docs/sessions/055-bughunt-2026-05-09/`. The campaign flagging is
"both gcc and tcc time out → SIGALRM exit 142 → SKIP" so
pathologically slow csmith programs don't surface as fake fails;
the analyzer post-processes `.out` files looking for the
"Alarm clock" string to confirm.

Tiger doesn't ship `seq` or `timeout(1)`; the campaign uses a
while-counter and `perl -e 'alarm; exec'`.

## Investigated and ruled out: LL `addend+4` paranoid-fix from 054 HANDOFF

The session-054 HANDOFF flagged a possible LL store path that emits
`stw hi at addend; stw lo at addend+4` where `addend+4` could
overflow signed-16 when `addend ∈ [0x7ffc, 0x7fff]`. On inspection,
this path is dead: the framework splits VT_LLONG stores into two
VT_INT calls, each going through `ppc_adjust_extra_off` with its
own (correctly-bumped) offset. Repro at
`docs/sessions/055-bughunt-2026-05-09/repro_ll_addend4.c` shows
the disasm has two separate PIC indirections, each correctly
adjusted.

Could clean up the dead VT_LLONG branch in `ppc-gen.c::store()` as
a follow-up but not urgent.

## Open work for next session

### 1. Csmith --float --builtins seed 92 — open bug

Aggressive csmith options (`--float --builtins`) found one
divergence in 100 seeds: seed-92's `g_99` (a plain `int32_t`
global) ends with different values in tcc vs gcc-4.0. The program
uses lots of float arithmetic, `__builtin_clzll`, `__builtin_popcountll`,
several `safe_*` helpers. Builtins themselves match (verified by
`docs/sessions/055-bughunt-2026-05-09/repro_builtin.c`), so the
bug is somewhere else in the float / safe-helpers / pointer
arithmetic paths.

* Source: `/tmp/csmith-tcc-aggro/seed-92.c` on uranium (565 lines
  after csmith's safe-math wrappers).
* Reproduction: full source, gcc-4.0 reference at checksum
  7B499BDB, tcc gives 65EE2E44.
* First divergence (per `print_hash_value=1`): `g_99` after
  `func_28()` runs.
* Reduction setup: interestingness test at
  `/tmp/seed92-reduce/test.sh` is ready to drive `creduce`. Each
  iteration scp's the candidate to imacg3 and runs both compilers
  + diffs output. Expected ~hours to reduce on a G3.

Likely candidates worth trying first before creduce:
* `(*l_589) |= func_48(...)` chain at line 101
* `safe_lshift_func_int16_t_s_s` and similar
* float-to-int cast in expression
* The `__builtin_clzll(l_33[2][1])` vs `__builtin_popcountll((*l_589))`
  paths

### 2. Lua testes — `files.lua` fails on tcc-built lua at line 84

`assert(io.output():seek() == 0)` fails on tcc-built lua (and gcc-built
lua reaches a different assertion at line 762 — the test infra has
its own quirks; lua's own test suite never claimed to pass on Tiger).
Worth checking whether tcc miscompiles `io.output()` chaining or
ftell-via-seek; or whether the failure is a test-environment issue.
This was caught by the lua testes script run in this session. The
script (`lua_testes.sh`) builds lua with both compilers, builds the
.so test libs via gcc-4.0, then runs 22 language tests under each
and diffs. 5 of 6 compared tests are identical; constructs.lua
diverges only because `math.random(0, 1)` returns a different value
(non-tcc bug). The full set of 22 tests didn't all complete on the
gcc side before the session ran out of time; resume by running the
script standalone and checking remaining diffs.

### 2. More csmith with adjusted options

The `--float` + `--builtins` aggressive batch is partial; the
`--builtins` half mostly fails because gcc-4.0 lacks several
`__builtin_*` symbols. Next: try `--enable-builtin-kinds` to limit
to ones gcc-4.0 has. Or try larger seed ranges with default
options. Or try `--max-funcs 12 --max-block-depth 5` (more complex
programs).

### 3. Clean up dead VT_LLONG store path

`ppc-gen.c::store()` has a `case VT_LLONG:` block at lines ~1342
and ~1401 that's never reached (framework splits into VT_INT
calls). Could delete with a one-line `tcc_error("dead path")` and
verify tests2 still passes; if so, delete entirely.

### 4. Ongoing: more real-world programs

Following the v0.2.40-44 trail:
* `less` (small pager, well-tested code base)
* `busybox` (bundle of utilities — high coverage)
* `coreutils` (small utility per binary)
* `tcl` (would unblock sqlite TCL test suite)
* `expat` (XML parser)

Each one tends to surface 1-2 codegen edge cases.

### 5. From earlier sessions, still open

* OSO STAB emission for gdb-on-Tiger (roadmap #7.5)
* Self-link diagnostics (roadmap #7)
* AltiVec intrinsics (roadmap #9)
* Real bcheck.c port (roadmap #11)
* LL `addend+4` cleanup (this session, optional)

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
git fetch https://github.com/cellularmitosis/tcc-darwin8-ppc.git main
git reset --hard FETCH_HEAD          # only safe if working tree clean
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
./demos/v0.2.45-be-bitfield-abi.sh            # PASS  (NEW)
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### To re-run csmith

```sh
# From uranium:
cd /Users/cell/claude/tcc-darwin8-ppc/docs/sessions/055-bughunt-2026-05-09
./csmith_gen.sh 1 200            # generates seeds, ships to imacg3
ssh imacg3 '/Users/macuser/tmp/csmith_campaign.sh 1 200'
ssh imacg3 '/Users/macuser/tmp/csmith_analyze.sh'
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. The session was a long single-thread of csmith-driven
bug discovery, root-cause analysis, fix development, and verification.

## Closing notes

The BE bitfield bug fixed this session is a meaningful one for
ABI compatibility:

1. It silently miscompiled any program that read a bitfield via a
   union with a sibling `uintN_t` member, or via a pointer cast
   `((union *)&plain)->bf`. The bitfield read returned a
   byte-swapped value of what gcc-built code would have read.
2. tcc-built and gcc-built objects sharing a struct definition with
   bitfields would disagree on member values. Same for any program
   that consumed BE-ABI-laid-out data from disk or network.
3. Most small programs round-trip via tcc bitfield writes and reads
   only, so they were unaffected. csmith found it on day 1 because
   csmith generates union-overlay patterns aggressively.

After this session, tcc's bitfield codegen is now ABI-compatible
with gcc-4.0 on Tiger PPC.
