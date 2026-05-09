# Session 055 — bug-hunt unsupervised (2026-05-09)

## Charter

User said: "let's focus on finding and fixing bugs and making this as
rock solid as possible, bringing in additional test suites or testing
methodologies as needed. Proceed unsupervised. Document everything,
cut releases as appropriate, include a demo in each release if it
makes sense to do so."

## Arrival state

- HEAD: `3402965` (session 054 HANDOFF). Eight working real-world
  programs (lua, zlib, bzip2, cJSON, sqlite, GNU sed, gzip, Python 2.7).
- tests2: 111/111. Bootstrap fixpoint holds.
- imacg3 local checkout was at 57ea9f0 with uncommitted changes; on
  inspection those changes were the already-committed v0.2.40..v0.2.44
  diffs (probably a soft-reset that was never re-committed). Stashed
  to `session-055-precaution-2026-05-09` and reset --hard to FETCH_HEAD.

## Plan (priority order)

1. **Sanity baseline** — fresh build, tests2, demos, fixpoint, upstream tests.
2. **The known LLONG `addend+4` paranoid-fix** filed in 054 HANDOFF —
   write a targeted reproducer first; if real, fix; ship release.
3. **csmith-class differential testing** — generate random C programs,
   compile each with both tcc-on-PPC and gcc-4.0, compare runtime
   output. Reduce mismatches to minimal repros, fix, ship.
4. **Real-world test suites for already-working programs** —
   lua's `testes/` suite, gzip integration tests, sqlite's TCL tests
   if feasible. Each tends to surface bugs the program build alone
   doesn't.
5. **More real-world program builds** — each new program tends to
   expose one or two codegen edges (the v0.2.41 extra_off truncation
   was found by gzip; the v0.2.40 SECTDIFF-merged was found by sed;
   v0.2.44 -g cross-TU was found by Python -g). Candidates: less,
   coreutils, busybox, openssl static lib, perl 5.x.

Session log lives in this README; durable lessons in `findings.md`;
commit one-liners in `commits.md`.

## Work log

### 2026-05-09 evening — kickoff

- Imacg3 stale state cleaned up (stash + reset to FETCH_HEAD).
- Sanity baseline GREEN: tests2 111/111, fixpoint holds, all
  demos PASS, abitest-cc/tcc 22/22, libtest+dlltest pass.
- libtest_mt fails at the multi-threaded JIT stage with
  "RUNTIME ERROR: invalid memory access" — pre-existing, deferred.

### Investigated LL `addend+4` paranoid-fix from 054 HANDOFF

- Wrote a 16-byte-extern-struct repro at offset 0x7ffc → store
  via PIC.
- Confirmed dead code: tcc framework splits VT_LLONG stores
  into two VT_INT calls, each with its own `ppc_adjust_extra_off`.
  Disassembly shows two PIC indirections; second one has
  `addis r12,r12,0x1` to bump anchor for the high half.
- Notes in [`findings.md`](findings.md). No fix needed.

### Set up csmith differential testing

- Installed `csmith 2.3.0` and `creduce 2.10.0` via brew on
  uranium. Csmith runtime headers shipped to imacg3 at
  `/Users/macuser/tmp/csmith-runtime/`.
- Campaign script (`csmith_campaign.sh`) generates seeds on
  uranium, scp's to imacg3, compiles+runs each with both
  `gcc-4.0 -O0` and `tcc -B... -L...`, diffs the checksum
  output.
- Tiger doesn't ship `seq` or `timeout(1)`; the script uses a
  while-counter loop and `perl -e 'alarm; exec'` for timeouts.
- One known false-alarm class: csmith occasionally generates
  pathologically slow programs where both gcc-built and
  tcc-built binaries time out; the perl alarm wrapper produces
  different PIDs in stderr, causing a spurious "output diff".
  Improved the script to treat exit 142 (SIGALRM) as timeout,
  matching gcc-timeout. (The improvement was applied for the
  next campaign; the in-flight 1–200 was not interrupted.)

### Found and fixed: BE PPC bitfield ABI

- Csmith 1–30: 28 PASS, 2 FAIL.
  - Seed 3: real bug (output diff: `g_55.f1` differs).
  - Seed 20: gcc+tcc both time out — false alarm.
- Csmith 31–200 (still old tcc): 142 PASS, 28 FAIL.
- Located divergence by running seed-3 with `print_hash_value=1`
  argument: first divergence at `g_55.f1` (a 14-bit bitfield in a
  union with overlapping `uint32_t f0`). Reduced to 16 lines.
- Hypothesis confirmed via probe (12 different storage values):
  tcc reads top 14 bits of the BE 32-bit word interpreted as
  little-endian (= byte-swapped bytes 0..1), while gcc reads them
  as straight BE. The pre-fix tcc layout was LSB-first within the
  storage container; the SysV/AIX PowerPC ABI is MSB-first on BE.
- Fix in tccgen.c (98 lines, `#if defined(TCC_TARGET_PPC) &&
  !defined(TCC_TARGET_PPC64)` branches in `load_packed_bf`,
  `store_packed_bf`, `init_putv`'s VT_BITFIELD path).
- Verified:
  - Repros now match gcc (struct, union, ptr-cast)
  - Bootstrap fixpoint holds
  - tests2: 111/111
  - abitest-cc/tcc: 22/22 each
  - sed, gzip, BE-bitfield demos PASS
- Shipped as **v0.2.45-g3** (commits `fc241f5` + `ea6a37c`,
  tag pushed to origin).

### Csmith re-run with fix in progress…
