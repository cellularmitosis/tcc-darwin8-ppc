# Handoff — 2026-05-02 (mid-session)

Picked up where v0.2.4-g3 left off. User unplugged the laptop
mid-test-run for travel; this doc captures state precisely so
the next session (Claude or human) can resume without
re-discovery.

## TL;DR

- HEAD: `d82b12a` (ppc-macho: emit __mod_init_func/__mod_term_func)
- Latest release: **v0.2.5-g3** (long-frame prolog + VLAs;
  101 / 122 tests2 = 82.8%)
- v0.2.5-g3 was tagged + tarballed + uploaded mid-session.
- Bootstrap fixpoint last verified at HEAD `11c9cfc` (v0.2.5
  release) — should still hold at d82b12a but **NOT YET RE-RUN**.
- 108_constructor verified locally as passing (saw
  "constructor\nmain\ndestructor\n") but **full tests2 NOT YET
  RE-RUN** at HEAD d82b12a.

## What landed since the previous handoff (`a0e1bcc`)

| Commit | What | tests2 effect |
|---|---|---|
| `ae882f5` | **Long-frame prolog/epilog** (>32KB stack frames) + long-offset local load/store/address-of + struct word-copy with huge offsets + abs-addr base register fix | +2 (119_random_stuff, 101_cleanup) |
| `11c9cfc` | **VLA support** (gen_vla_alloc/sp_save/sp_restore) | +3 (78_vla_label, 122_vla_reuse, 123_vla_bug) |
| `a25f395` | v0.2.5-g3 release (tarball + tag + GitHub release) | — |
| `d82b12a` | **Constructor/destructor** support in macho_output_exe (.init_array → __mod_init_func; .fini_array → __mod_term_func) | +1 expected (108_constructor) — VERIFIED PASSING but full suite not re-run |

## tests2 baseline trajectory this session

77 → 79 → 83 → 84 → 87 → 89 → 91 → 92 → 93 → 95 → 96 → 98 → 101 → **(probably 102, not confirmed)**

## What's verified at HEAD `d82b12a`

- 108_constructor: PASSES (manually re-tested)
- Compiles cleanly on ibookg37 (gcc-4.0)

## What's NOT verified at HEAD `d82b12a` (next session must do)

1. **`FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh`** —
   bootstrap fixpoint. Should still hold; the change is in
   macho_output_exe and only affects EXE output, not the .o-based
   tcc-self compile chain. But verify.

2. **Full tests2 re-run.** Should give 102/122 = 83.6%.
   Possibly higher if 108 changes interact with anything else.
   Run `./scripts/run-tests2.sh`.

3. **No regressions.** Particularly worth eyeballing programs
   that previously linked successfully (none of them have
   .init_array, so should be unaffected, but verify).

## Where to pick up

### 1. Confirm v0.2.5 fixpoint and re-run tests at HEAD

```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
```

If fixpoint holds and tests2 = 102/122, decide whether v0.2.6-g3
is worth cutting just for the constructor support. (Probably not
on its own — wait for one or two more wins.)

### 2. Investigate remaining failures

After d82b12a the failure list should be ~20 of:

- **VLA edge case (1):** 79_vla_continue — fails address-comparison
  in test1-test4 (test5 passes). Standalone reproductions of test1
  succeed, so it's something about the integrated calling pattern.
- **`-run` / `-dt` (5):** 60_errors_and_warnings, 96_nodata_wanted,
  104_inline, 117_builtins, 128_run_atexit. All need
  `create_plt_entry` and proper -run support in PPC backend.
  Substantial: ~200-500 lines.
- **Real bound checking (5):** 112_backtrace, 113_btdll,
  114_bound_signal, 115_bound_setjmp, 116_bound_setjmp2,
  126_bound_global. Stubs let them link/run but they expect the
  checker to actually fire on overflow — needs the real bcheck.c
  port. Substantial.
- **Real multithreaded atomics (2):** 124_atomic_counter,
  125_atomic_misc. Need real PPC lwarx/stwcx. atomic helpers in
  libtcc1.a (not the no-op stubs). Substantial: ~50-100 lines of
  asm or careful C with embedded asm.
- **Endianness-specific .expect (4):** 90_struct-init,
  91_ptr_longlong_arith32, 95_bitfields, 95_bitfields_ms. Not
  really tcc bugs — upstream tests' .expect files bake in
  little-endian byte-order for `int` printed via byte loop.
  Could fix by shipping per-platform .expect files.
- **Float-literal parsing (1):** 70_floating_point_literals.
  ~5 ULP error in `123.123E12F`-style parsing. Plus a hex-float
  with very long mantissa (`0x1...0p-140`) that overflows tcc's
  parser. Both upstream tcc bugs, surface only on PPC's IEEE
  semantics.
- **HFA-on-PPC (1):** 73_arm64. Test designed for AArch64; PPC
  ABI doesn't have HFA semantics. Might never pass.

### 3. Higher-value next targets

In rough decreasing impact:

a) **`-run` mode** — would unlock 5 tests AND make iterative
   development much more pleasant. The big lift is
   `create_plt_entry` in `ppc-link.c::create_plt_entry` (currently
   `tcc_error_noabort("ppc-link: create_plt_entry not implemented")`).
   Look at i386-link.c for the pattern; the PPC version writes
   PIC stubs that use the GOT.

b) **Real `-bcheck`** — port `tcc/lib/bcheck.c`'s real
   implementation (hash-table region tracking, signal hooks).
   The no-op stubs in lib-ppc.c need to be deleted and replaced
   with the real symbols. Then the codegen instrumentation in
   `ppc-gen.c` is largely already there
   (`func_bound_offset`/`func_bound_ind` are unused statics).

c) **Real PPC atomics** — write `__atomic_*` helpers in PPC
   asm using `lwarx`/`stwcx.` reservation primitives. Not large
   but careful work. Could be done in C with inline asm.

d) **79_vla_continue investigation** — the standalone test
   passes, so something about the integrated test suite is
   different. Could be a small fix or a deep bug.

### 4. Documentation

The session 035 README has been updated several times this
session. After picking up, do:

```
$EDITOR docs/sessions/035-unsupervised-2026-05-02/README.md
```

Add a row for `d82b12a` (constructor exe support) and bump the
"end-of-session" tests2 count once verified.

## Files touched this session (cumulative)

- `tcc/ppc-gen.c` — heavily modified (struct args/returns, VLAs,
  long-frame, long-offset, byte/short BE offset, FP-to-LL swap,
  abs-addr load/store, ggoto, void-deref, FPR>8 spill, VT_BOOL)
- `tcc/ppc-macho.c` — break/continue PIC reloc fix, .init_array
  classification (.o), `__mod_init_func`/`__mod_term_func` (EXE)
- `tcc/lib/lib-ppc.c` — `__eprintf`, no-op bound stubs, atomic
  stubs
- `tcc/libtcc.c` — Tiger realpath workaround
- `tcc/tests/tests2/Makefile` — capture compile-step stderr,
  MAP_ANONYMOUS alias
- `scripts/build-release-tarball.sh` — version bumped multiple
  times
- `README.md` — status table updated multiple times
- `.gitignore` — release tarballs excluded

## Important state / gotchas

### Build environment

- Primary host: **`ibookg37`**. `git` not in default `$PATH`
  there; rsync from uranium then ssh-rebuild is the workflow.
- After every rsync, force a clean configure:
  `ssh ibookg37 'cd ~/tcc-darwin8-ppc/tcc && rm -f config.mak && cd .. && ./scripts/build-tiger.sh configure'`
- Or just `/opt/make-4.3/bin/make -C tcc` — the configure
  artifacts on uranium are for arm64 clang and shouldn't survive
  rsync if the .gitignore is honored.

### Codegen quirks burned into tcc-self (still as documented in
the previous handoff, plus one more from this session)

1. `put_nlist` narrow-int args — bypass via inlined byte writes.
2. Warning emission via `fprintf+fflush` — bypass via direct
   `write(2)` syscall.
3. 64-bit const-fold sign-ext — partial workaround in
   `tccgen.c::gv`. The deeper fix is the open question from
   session 031 (still open).
4. char/short param big-endian offset — the +3/+2 fix landed in
   this session (`b7382c2`); originally backed out due to
   misdiagnosed fixpoint regression.
5. LL-helper return r3<->r4 swap — limited to FP-to-LL helpers
   (`__fixdfdi` / `__fixsfdi` / `__fixunsdfdi` / `__fixunssfdi`)
   in this session. Extending to divide/shift breaks fixpoint
   because tcc's body code uses those helpers and depends on the
   transposed-but-internally-consistent existing behavior.

### Recent commit list (this session, post v0.2.4 handoff)

```
d82b12a ppc-macho: emit __mod_init_func / __mod_term_func in EXE output
a25f395 v0.2.5-g3 release: long-frame prolog + VLAs
11c9cfc ppc-gen: VLA support (sp_save/sp_restore/alloc)
ae882f5 ppc-gen: long-frame prolog/epilog + long-offset load/store
56609d4 docs/sessions/035: refresh end-of-session writeup with later wins
68f930d v0.2.4-g3 release: struct returns + many codegen wins
e59112a ppc-gen: FP args beyond f8 spill to outgoing param area
545edc8 libtcc1 PPC: atomic helper stubs
16e9012 libtcc1 PPC: no-op bound-check stubs
0baab59 ppc-gen: load/store from absolute address
2e51f70 libtcc.c: Tiger realpath workaround for normalized_PATHCMP
650a75e ppc-gen: r3<->r4 swap for FP-to-LL libgcc helpers
b7382c2 ppc-gen: byte/short param big-endian offset
06f2361 ppc-macho: classify .init_array/.fini_array (.o output)
963aeae docs/sessions/035: end-of-session writeup
ad144c0 ppc-gen: struct returns + dynamic param-area + struct load via sym
82a583b README: status to v0.2.3-g3
af615c2 v0.2.3-g3 release
c36279f ppc-gen: struct-by-value parameters
0501a9f ppc-gen: VT_BOOL load/store, computed goto, void* deref
15deb71 libtcc1 PPC: add __eprintf for assert()
ef6fa2b tests2 Makefile: capture compile-step stderr
1479224 gitignore: release tarballs
902aea7 EXE PIC reloc fallback: fix break-out-of-loop bug
```

### Releases on GitHub

- v0.1.0-g3 (back-fill from earlier session)
- v0.2.0-g3 (full self-link)
- v0.2.1-g3 (>8-arg fns, FP shadow for variadic)
- v0.2.2-g3 (more libgcc helpers + auto-link)
- v0.2.3-g3 (struct-by-value parameters)
- v0.2.4-g3 (struct returns + dynamic param-area + many fixes)
- **v0.2.5-g3 (latest, marked `Latest`) — long-frame + VLAs**

Tarballs sitting locally (not committed, gitignored):
- `tcc-darwin8-ppc-v0.2.0-g3.tar.gz`
- `tcc-darwin8-ppc-v0.2.1-g3.tar.gz`
- `tcc-darwin8-ppc-v0.2.2-g3.tar.gz`
- `tcc-darwin8-ppc-v0.2.3-g3.tar.gz`
- `tcc-darwin8-ppc-v0.2.4-g3.tar.gz`
- `tcc-darwin8-ppc-v0.2.5-g3.tar.gz`

Safe to delete; they're on GitHub.

## User's preferences (unchanged from previous handoff)

- Likes incremental commits with detailed bodies (the "why"
  matters more than the "what")
- Likes per-session READMEs with categorized findings, plausible
  fix sketches, explicit hand-off pointer for the next session
- OK with frequent patch-level releases for small fixes
- "Unsupervised mode" rules in CLAUDE.md: don't stop to ask,
  document judgment calls, only block on truly unreasonable
  actions

## Closing notes

This was a productive session: 77 → 101 tests2 (+24, 80.3%) with
3 patch releases shipped (v0.2.3, v0.2.4, v0.2.5) — and one more
test (108_constructor) just landed in d82b12a but isn't yet
included in any release. If 108 verifies and fixpoint holds at
d82b12a, a small v0.2.6-g3 release for it alone would be
reasonable, but waiting until the next batch of wins is also
fine.

Good luck.
