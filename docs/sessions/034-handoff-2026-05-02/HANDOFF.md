# Handoff — 2026-05-02

A note from a Claude session that's about to run out of context.
The next session (Claude or human) should be able to pick up cold
from this doc.

## TL;DR — where things are right now

- HEAD: `9a5dc3f EXE PIC reloc: handle forward-defined and aliased symbols`
- Latest release: **v0.2.2-g3**, published on GitHub with tarball asset
  (https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.2-g3)
- Three GitHub releases live: v0.1.0-g3, v0.2.0-g3, v0.2.1-g3, v0.2.2-g3
  (v0.2.2 is `Latest`)
- tests2 baseline: **77 / 121 pass (63.6%)**
  (started this run at 70 / 122 = 57.4%)
- Self-host fixpoint: **STILL HOLDS**
  (verify with `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` on ibookg37)

The user's directive that started this run: *"please continue with
those unsupervised. document everything, cut releases as appropriate.
drop progress in the chat. keep working until I stop."* — referring
to the post-v0.2.0 roadmap items (struct-by-value, fixpoint
regression, long-long shift, bound checking, dylib loading).

## What landed in this session run

Listed in order of impact, not time:

| Commit | What | Impact |
|---|---|---|
| `b359d62` | **FP shadow for variadic calls** (printf-with-`%f` etc.) | +5 tests, math programs now correct |
| `dd4acf9` | `macho_load_dll` no-op (`-lm` etc. now LINK) | unblocks math programs |
| `82f12ff` | Auto-link libtcc1.a for PPC EXE + 3 more helpers | +2 tests, easier UX |
| `6958ef3` | `>8-arg functions`, `TOK_PDIV`, `stddef.h` guards | sqlite advances 53K lines |
| `9a5dc3f` | EXE PIC reloc handles forward-defined / `__attribute__((alias))` | +1 test (17_enum) |
| `35c7312` | Partial fix for 64-bit const-fold sign-ext bug | improves gcc-built tcc |
| `df110b4` | Track `tcc/conftest.c` (was gitignored) | clean builds from tag now work |

Three patch releases:
- **v0.2.1-g3** = first 6 of the above (everything through `b359d62`)
- **v0.2.2-g3** = +`82f12ff` (helpers + auto-link)
- **(unreleased after `9a5dc3f`)** — could be a v0.2.3 if you want, but the change is small

The `9a5dc3f` PIC-reloc fix landed AFTER v0.2.2 was tagged. Three
untracked tarballs sit in repo root: `tcc-darwin8-ppc-v0.2.{0,1,2}-g3.tar.gz`
(don't commit; `.gitignore`-worthy).

## Where to pick up: the recommended next steps, in priority order

### 1. Decide whether to cut v0.2.3 with the PIC-reloc fix

`9a5dc3f` is small but it's a real bug fix that landed after
v0.2.2 was tagged. Either:

a. Roll a v0.2.3 patch release just for it (one commit). Quick
   `./scripts/build-release-tarball.sh` with `VERSION=v0.2.3-g3`
   then `gh release create v0.2.3-g3 ...`
b. Roll it together with the next batch of fixes into v0.2.3 or
   v0.3.0.

Either is fine. The user is OK with frequent small patch releases
(they explicitly asked me to back-fill v0.1.0-g3 as a release
when I noticed it had been left dangling).

### 2. Struct-by-value parameters / returns — biggest remaining gap

This is THE highest-impact remaining item. Unblocks ~7 tests2
failures (73, 109, 121, 130, 131, 135, 137) AND sqlite3
amalgamation past line 122853 AND a huge amount of real-world C
code. Estimated 200-500 lines in `tcc/ppc-gen.c::gfunc_call` and
`gfunc_prolog`.

**I tried this in this session and backed off.** Apple PPC ABI
for struct args is moderately complex:
- Pass-by-value: struct copied to caller's parameter area, GPRs
  r3-r10 hold the first 32 bytes of that area (each 4-byte chunk
  in successive GPRs). Structs > 32 bytes spill the remainder to
  stack.
- Return: ≤4 bytes in r3, 5-8 bytes in r3+r4, larger via hidden
  first-arg pointer (caller passes pointer to result buffer).
- Edge cases: char[] structs, alignment, struct-containing-floats.

Suggested approach for next session:
1. Start with the simplest case: struct of size ≤4 bytes by value.
   Caller emits `lwz` from struct address to one GPR; callee
   reads the GPR back to its stack slot.
2. Get one test passing (e.g. modify `73_arm64.c` to test only
   `s1`/`s2`/`s4` cases and see what works).
3. Extend to medium structs (8 bytes, 2 GPRs).
4. Then larger structs (stack copy).
5. Then struct returns.

The relevant call site to extend:
`tcc/ppc-gen.c:1216`  — `if (bt == VT_STRUCT) tcc_error(...)`.
Replace with the actual struct handling.

### 3. The 32-byte fixpoint regression / long-long const-fold bug

**Diagnosed in detail** in `docs/sessions/031-fixpoint-investigation/README.md`.
**Partially fixed** in `35c7312` — the gcc-built tcc now does the
right thing for 64-bit binary ops on values with bit 31 set in
the low half. But tcc-self (built by gcc-built tcc) still has the
bug, because the bug is in the COMPILED gen_opic / gv path, not
the source.

The smoking gun is `tcc/tccgen.c::gv` around line 1968 — the
old `vpushi(ll >> 32)` cast through `int` and sign-extended for
high bits set. We replaced with `vpush64(VT_INT|VT_UNSIGNED, ...)`
but the bug persists in tcc-self, suggesting the upstream c.i is
already corrupted by `gen_opic` BEFORE this point.

To fully fix: either (a) bisect with gdb to find what step in
tcc-self produces wrong c.i, or (b) more aggressively mask the
result in gen_opic's c.i write (line 2485). Don't promote without
testing because it could regress other targets.

The pre-existing `tcc.o` vs `tcc-self.o` 32-byte diff still
exists at HEAD. This doesn't block any v0.2.x functionality —
the tcc-self → tcc-self2 → tcc-self3 fixpoint still holds — only
parity with gcc-built tcc.o is broken.

### 4. Bound checking (`-bcheck`) for PPC

Substantial new feature. Need to:
- Add `bcheck.o` to `OBJ-ppc-osx` in `tcc/lib/Makefile` (one line)
- Verify bcheck.c compiles cleanly for PPC
- Implement `-b`/`-bcheck` flag in PPC backend (instrumentation in
  `gen_op` etc.)

4 of the remaining test failures need it (112, 114, 115, 116, 117,
126, 132 — that's actually 7).

Estimated 200-500 lines.

### 5. Smaller per-test fixes

Each is a small individual investigation. Some leads from this
session:

- **`18_include`**: `tcc-self` SIGBUS-crashes compiling a `__has_include`-
  using file. Worth investigating; might be a small parser bug.
- **`33_ternary_op`, `60_errors_and_warnings`, `102_alignas`,
  `70_floating_point_literals`**: warning-text drift between our
  tcc and what `.expect` files expect. Might just need `.expect`
  updates or warning format alignment.
- **`91_ptr_longlong_arith32`**: tests pointer-int arithmetic
  semantics; not clear if the test EXPECTS PPC-specific or is
  test-was-only-checked-on-x86_64. Worth comparing to gcc on PPC
  to see if even gcc-PPC matches the expected output.
- **`95_bitfields`, `95_bitfields_ms`**: bitfield codegen issues;
  could be tcc PPC backend not handling some bitfield case.
- **`110_average`**: I added the `__floatdidf` helper that was
  missing; it MAY now pass with v0.2.2 build but my test counter
  may have been measuring stale .test files.
- **VLAs (78, 79, 114, 116, 122, 123)**: tcc PPC backend doesn't
  support VLAs. Listed in roadmap as substantial new feature.

### 6. Documentation polish

- The roadmap is current (last updated by 027 follow-up).
- Each session has a README and the README status table is
  current through 033.
- README mentions v0.2.1 in the top "Status:" line but should be
  updated to v0.2.2-g3 (small).

## Important state / gotchas the next session needs to know

### Build environment

- Primary dev host: **`ibookg37`** (PowerBook G4 600MHz, Tiger
  10.4.11). Repo at `~/tcc-darwin8-ppc/`. `gcc-4.0` available.
  `/opt/make-4.3/bin/make` (system make is GNU 3.80, lacks
  `$(or)`).
- `imacg3` is the SECONDARY host; iBookG37 is faster. The Sonnet
  agent in 026 wandered off to imacg3 and got stuck — be
  intentional about which host you use.
- Local repo on uranium (this Mac, arm64 macOS): `/Users/cell/claude/tcc-darwin8-ppc/`
- Sync via `rsync -av <local> ibookg37:tcc-darwin8-ppc/`
- **Always force a clean configure on ibookg37** — uranium's
  `tcc/config.mak` (set up for `clang -arch arm64`) syncs over
  and breaks the PPC build. Do
  `ssh ibookg37 'cd ~/tcc-darwin8-ppc/tcc && rm -f config.mak && cd .. && ./scripts/build-tiger.sh configure'`
  every time you sync.

### Test infrastructure

- `./scripts/run-tests2.sh` — one-command runner for tests2 with
  `NORUN=true` (forces `-o exe + ./exe` path; tcc `-run` isn't
  fully wired on PPC because of `create_plt_entry`). Captures
  pass/fail summary.
- `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` — runs the canonical
  tcc-self2 → tcc-self3 byte-identical fixpoint chain. Run it
  before declaring any tcc/tccgen.c change "done".
- The test counter in `run-tests2.sh` is a bit fragile; it parses
  the make log line-by-line. If the total drops by 1 (e.g. 122 →
  121), that's likely a test that errored before producing a
  "Test:" line, not actually skipped. Investigate but don't panic.

### Codegen quirks burned into tcc-self

These are real tcc-PPC-backend bugs that we've **bypassed** in
upstream tcc.c rather than fix in the backend, because the right
fix-place isn't always obvious. Documented for the record:

1. **`put_nlist` narrow-int args** — fixed by inlining the byte
   writes in `tcc/ppc-macho.c::put_nlist`. Don't revert to
   `put8()` calls in this function until the underlying backend
   bug is found.
2. **Warning emission via fprintf+fflush** — fixed by switching
   to direct `write(2)` syscall in `tcc/libtcc.c::_tcc_error_noabort`.
   Don't revert to fprintf/fflush.
3. **64-bit const-fold sign-ext** — partially worked around in
   `tcc/tccgen.c::gv` (vpushi → vpush64). The deeper fix is the
   open question in priority #3 above.

### Release process

`./scripts/build-release-tarball.sh` does steps 1-4: build,
bootstrap+fixpoint, stage, tar. To bump the version, edit the
`VERSION="${VERSION:-vX.Y.Z-g3}"` default at the top, OR pass
`VERSION=...` env var (preferred for one-off rebuilds).

After tarball is built on ibookg37, scp it back to uranium
(`/Users/cell/claude/tcc-darwin8-ppc/`), `git tag`, push, and
`gh release create vX.Y.Z-g3 tcc-darwin8-ppc-vX.Y.Z-g3.tar.gz
--title ... --notes-file /tmp/notes.md`. The tarballs are NOT
committed to the repo.

After creating, do `gh release edit vX.Y.Z-g3 --latest` if
needed (sometimes GitHub auto-promotes whichever release was
*published* most recently as Latest, regardless of semver).

### The user's preferences

- Likes incremental commits with detailed commit bodies (the
  "why" matters more than the "what").
- Likes per-session READMEs with categorized findings, plausible
  fix sketches for things you couldn't fix, and an explicit
  hand-off pointer for the next session.
- OK with patch-level releases for small fixes.
- Asked back-filling of v0.1.0-g3 release when I noticed the tag
  was published but no release object existed; willing to spend a
  few minutes on housekeeping like that.
- "Unsupervised mode" rules are in `CLAUDE.md`: don't stop to
  ask, document judgment calls, only block on truly unreasonable
  actions. PowerPC fleet has high risk tolerance (machines are
  reinstallable), uranium has low risk tolerance.

## Files most likely to need attention in next session

| File | Why |
|---|---|
| `tcc/ppc-gen.c` | Struct-by-value, bound-check codegen, any new tcc backend feature |
| `tcc/ppc-macho.c` | Mach-O writer changes (more relocs, struct-return ABI) |
| `tcc/lib/lib-ppc.c` | Add new libgcc helpers as needed; bound-check helpers if implementing -bcheck |
| `tcc/lib/Makefile` | `OBJ-ppc-osx +=` for new libtcc1 components |
| `tcc/tccgen.c` | If you go after the deeper const-fold fix |
| `tcc/include/stddef.h` | Already guarded; only re-touch if more header conflicts surface |

## Where to find context, in order of usefulness

1. **`docs/sessions/`** — every session has a README. Read in
   reverse-chronological order, stop when you have what you need.
   The 027 README is the longest and richest (the four-bug self-
   link debugging arc). The 031 README has the most detailed
   diagnosis of an unfixed bug.
2. **`docs/roadmap.md`** — current as of 027 follow-up; the
   numbered work items 1-3 are done and 4-6 are still open
   (testsuite is done as 029; sqlite as 030; v0.2.0 release as
   028 — the roadmap hasn't been re-numbered for the v0.2.x patch
   releases yet).
3. **`README.md` status table** — the canonical "what works /
   what doesn't" view.
4. **`docs/notes/linker-design.md`** — explanatory background on
   why tcc on PPC needs to do its own linking instead of relying
   on `gcc-4.0 ld`. Useful when reasoning about Mach-O layout
   decisions.
5. **`tcc/.UPSTREAM.md`** — the snapshot commit of upstream tcc
   that we forked from (`mob @ b39da9f6`, 2026-04-30). Useful when
   wondering whether a quirk is upstream's or ours.
6. **The git log itself** — every commit message is a
   self-contained explanation. `git log -p tcc/ppc-gen.c` is a
   good way to learn the PPC backend.

## Closing notes

- When the user says "keep working", they DO mean keep working,
  but also use judgment about when to ship vs when to iterate.
  Three patch releases in this run was a good cadence; two would
  also have been fine.
- Every change to `tcc/tccgen.c` or other shared files needs a
  fixpoint verification. The `FIXPOINT=1` chain catches a lot.
- When you hit a bug that requires "tcc has been miscompiled by
  tcc" reasoning, the workaround in upstream code (avoid the
  pattern) is often easier and more robust than fixing the actual
  backend bug. The codegen-quirks list above documents the three
  workarounds in place.
- Documentation pays for itself almost immediately. Spend the 10
  minutes to write the session README; the next session will
  thank you.

Good luck.
