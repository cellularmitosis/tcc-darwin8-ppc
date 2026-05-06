# Roadmap

## Where we are (May 2026)

Self-hosted tcc for Tiger PowerPC, **shipped through v0.2.15-g3**.
Fifteen patch releases on top of [v0.2.0-g3](sessions/028-v0.2.0-g3-release/README.md)
have brought tests2 from 70 / 122 (57%) to **111 / 111 (100%)**:

| | tests2 | what it added |
|---|---|---|
| [v0.2.0-g3](sessions/028-v0.2.0-g3-release/README.md) | 70 / 122 | full self-link, no gcc-4.0 in pipeline |
| [v0.2.1-g3](sessions/032-v0.2.1-release/README.md) | 75 / 122 | >8-arg fns, FP shadow for variadic |
| [v0.2.2-g3](sessions/033-v0.2.2-release/README.md) | 77 / 122 | more libgcc helpers, auto-link libtcc1.a |
| [v0.2.3-g3](sessions/035-unsupervised-2026-05-02/README.md) | 87 / 122 | struct-by-value parameters |
| [v0.2.4-g3](sessions/035-unsupervised-2026-05-02/README.md) | 96 / 122 | struct returns, dynamic param-area |
| [v0.2.5-g3](sessions/035-unsupervised-2026-05-02/README.md) | 101 / 122 | long-frame prolog, VLAs |
| [v0.2.6-g3](sessions/036-post-travel-2026-05-02/README.md) | 105 / 118 | constructor/destructor, VLA × callee-spill safety, BE-aware skips |
| [v0.2.7-g3](sessions/037-tcc-run-on-ppc-2026-05-02/README.md) | (104) | **`tcc -run` works on PPC for the first time** (PLT, @hi/@ha fix) |
| [v0.2.8-g3](sessions/038-real-ppc-atomics-2026-05-02/README.md) | 105 / 118 | real atomics via pthread_mutex |
| v0.2.9-g3 | 105 / 118 | minor cleanup |
| v0.2.10-g3 / v0.2.11-g3 | 109 / 111 | bcheck-style skip refinements; total drops 118 → 111 due to 6 bcheck-asserting tests properly skipped on PPC |
| [v0.2.12-g3](sessions/040-pickup-2026-05-03/README.md) | 109 / 111 | struct-deref-by-value + cross-TU PIC reloc translation. **Lua 5.4.7 builds + runs end-to-end.** |
| [v0.2.13-g3](sessions/040-pickup-2026-05-03/README.md) | 110 / 111 | BE Sym-union enum_val clobber fix; `scripts/bench.sh` (lua compile-time benchmark) |
| [v0.2.14-g3](sessions/041-pickup-2026-05-03/README.md) | **111 / 111 (100%)** | BE bitfield read-back fix (force byte-wise loads on PPC32 BE) |
| [v0.2.15-g3](sessions/041-pickup-2026-05-03/README.md) | 111 / 111 | Apple PPC ABI long-long alignment + LL field-load clobber. **sqlite3_open works.** |
| [v0.2.16-g3](sessions/041-pickup-2026-05-03/README.md) | 111 / 111 | LL-arg shuffle clobber in gfunc_call. **sqlite SELECT works; zlib full test suite passes.** |
| (post-v0.2.16, [session 042](sessions/042-upstream-tests-2026-05-05/README.md)) | 111 / 111 | First end-to-end upstream `tcc/tests/make test` run. 5 fixes: VT_STRUCT sym+off load, libtcc1.a missing helpers (`__builtin_*`, alloca, `__fixsfdi`), tcctest weak_test gating for Tiger dyld, ppc_pic_pairs leak, ship bcheck.o/bt-log.o stubs. tests2-dir under `-run` now 111/111 (was 108/111); memtest passes; test3 now actually runs (gets 770/4500 lines into tcctest before SEGV, was previously unable to even build the gcc reference). |
| [v0.2.17-g3](sessions/043-unsupervised-2026-05-05/README.md) | 111 / 111 | **alloca() actually works.** Fixed two coupled bugs: epilog used `addi r1, r1, frame_size` which can't recover from alloca having moved sp; and alloca's safety zone (between user region and outgoing param area) was 32 bytes, too small to absorb subsequent `printf` arg spills. Switched epilog to back-chain restore via FP, bumped alloca padding to 256 bytes. Also added more `__bound_*` stubs (strrchr, strstr, malloc family, etc.). Net upstream-test impact: full tcctest.c runs to completion (1020 lines vs 1012 reference, 255-line content diff that's all known long-double / `_Bool`-size / FPR-past-8 divergences); `abitest-tcc::ret_2float_test` now passes (was Bus error). |
| [v0.2.18-g3](sessions/043-unsupervised-2026-05-05/README.md) | 111 / 111 | **bzip2 1.0.8 works end-to-end.** Two more codegen fixes: (a) variadic FP arg past 8 FPRs had an off-by-one on TREG_TO_FPR (already 1-indexed, we double-added 1) so we stfd the wrong FPR — printf's 9th+ doubles were huge garbage. (b) float negation (`-x`) used the generic LE-only sign-flip (XOR 0x80 at offset size-1); on big-endian PPC the sign bit is at offset 0. Routed TOK_NEG through gen_opf for PPC, emitting real `fneg`. Net: tcctest.c diff vs reference shrinks 255 → 122 lines. bzip2 1.0.8 builds + round-trips on 573KB /etc/services with byte-identical decompress. |
| [v0.2.19-g3](sessions/043-unsupervised-2026-05-05/README.md) | 111 / 111 | Three more codegen fixes from chasing the tcctest diff: (a) FP `>=`/`<=` correctly return false for NaN (was firing the inverted-LT/GT bit, which is clear for NaN — added `cror cr0_LT, cr0_LT, cr0_FU` before bc). (b) pointer→int cast follows destination signedness (matches gcc; previously always sign-extended). (c) long-long arg straddling r10/stack (gslot=7, HIGH in r10, LOW on stack) writes BOTH halves; old `gv(RC_R(gslot))` only loaded one. Plus tcctest's LONG_DOUBLE = double on Apple PPC so gcc + tcc produce comparable output. tcctest.c diff: 122 → 44 lines. Remaining diff is all structural / known: aligntest3/4 alignof (Apple ABI), `_Bool` size (gcc-4.0 quirk), relocation_test addend (deferred), promote-char-funcret UB. |
| [v0.2.20-g3](sessions/043-unsupervised-2026-05-05/README.md) | 111 / 111 | **Apple PPC ABI 13-FPR support.** Bumped FP arg passing from 8 → 13 FPRs (NB_REGS 18 → 23, RC_F bitmask, reg_classes[], prolog spill area, fpr_index/fslot caps, all the related counts). Apple PPC32 actually uses f1..f13; we'd been capped at f1..f8 (the SysV/Linux convention). Functions with >8 FP args used to error "parameters exceed 8 FPR slots". After this, abitest::ret_8plus2double passes (had been failing since v0.2.0). Plus `__gcc_qadd / qsub / qmul / qdiv / __cmpdi2 / __ucmpdi2` stubs in lib-ppc.c so tcc-linked programs that include gcc-4.0-built objects don't hit "Symbol not found" at startup (Tiger libSystem doesn't ship the libgcc helpers). |
| [v0.2.21-g3](sessions/043-unsupervised-2026-05-05/README.md) | 111 / 111 | `__builtin_fabs / __builtin_fabsf / __builtin_inf / __builtin_inff` stubs. Tiger's `<math.h>` for PPC declares `fabs()` / `inf()` etc. via macros that expand to these gcc-style builtins. gcc inlines them; tcc emits regular function calls. Without stubs, programs that include `<math.h>` and use fabs/inf hit "Symbol not found" at startup. After this, **cJSON 1.7.18 builds + parses end-to-end** — fourth real-world program (lua, zlib, bzip2, cJSON). |
| [v0.2.22-g3](sessions/044-unsupervised-2026-05-05/README.md) | 111 / 111 | **`int *p = &arr[N]` (N != 0) static initializers actually point at `&arr[N]`.** Pre-v0.2.22 they collapsed to `&arr[0]` because `R_PPC_ADDR32` overwrote the in-place value (which is the implicit ELF Rel addend) instead of adding to it. Three small fixes — ppc-link.c::relocate() switches to `add` (matches i386's `R_386_32`); the Mach-O exe writer's resolver mirrors that; the Mach-O .o loader zeros the in-place after extracting the addend into the synthetic anchor sym. Drops `tcctest.c` diff vs gcc reference 44 → 33 lines (relocation_test divergence gone). Closes the highest-impact open item from session 043. |

(Total dropped 122 → 118 in v0.2.6 because four LE-byte-order-
specific tests are properly classified as skipped on big-endian;
118 → 111 in v0.2.10/.11 because six bcheck-asserting tests are
properly skipped without a real bcheck.c port.)

* Bootstrap fixpoint (`tcc → tcc-self → tcc-self2 → tcc-self3` with
  `tcc-self2.o == tcc-self3.o`) holds at every release.
* `tcc -run hello.c` works (since v0.2.7).
* `tcc -o exe` produces working libSystem-using binaries.
* Multi-threaded atomics correct (since v0.2.8).
* tests2 at 100% (since v0.2.14).
* lua 5.4.7 builds + runs end-to-end (since v0.2.12).
* sqlite3 amalgamation: in-memory SELECT queries work
  (since v0.2.16). File-based sqlite_open_v2 still crashes
  in the file-open path — distinct bug, deferred.
* zlib 1.3.1 builds + full test suite passes (since v0.2.16).
* Compile-time benchmark vs gcc-4.0 (lua 5.4.7, 33 .c files):
  tcc 2 s, gcc -O0 17 s, gcc -Os 40 s on a 900 MHz iBook G3.

## Open work, prioritized

### Concrete remaining test failures (0)

**tests2 is at 111/111 (100%) since v0.2.14.** All previously-
failing tests have either been fixed or properly classified as
skipped (bcheck-asserting tests are skipped on PPC because we
have no-op stubs but no real bcheck.c port).

The bcheck.c port remains a multi-session lift if we ever want
the `-b` instrumented build to actually instrument. See "Larger
scope" below.

### Structural items

| | item | notes |
|---|---|---|
| ~~**#1**~~ | ~~Real lwarx/stwcx atomics~~ | ✅ Done in `ba7848b` (4-byte) + `defd38c` (1-byte and 2-byte via word-RMW with masking). Per-file Makefile rule routes `tcc/lib/atomic-ppc.S` through gcc-4.0 to dodge tcc's missing PPC inline-asm. 124_atomic_counter dropped from 6m23s → 2.4s (137x speedup). 8-byte ops still serialize through the mutex (PPC32 has no ldarx/stdcx). |
| ~~**#2**~~ | ~~2-element HFA struct-by-value on GPR overflow~~ | ✅ Done in `84ae52c`. Wasn't actually a struct-by-value bug — the failing 73_arm64 sub-cases isolated to printf's variadic FP arg passing when GPR shadow slots run past r10. The straddle (gslot==7) and fully-past (gslot>=8) cases didn't write the FP value to the outgoing param stack, so printf read garbage. Same fix flipped 70_floating_point_literals to passing (the "5-ULP error" was printf reading garbage stack words, not parse_number precision). |
| **#3** | **Mach-O `tcc -ar` driver** | Currently `XAR = $(AR)` for PPC because tcc's `-ar` is ELF-only. Eventual port lets libtcc1.a be built without external `ar`. |
| **#4** | **Mach-O archive alacarte loader** | (Was old roadmap #7.) Parse `__.SYMDEF SORTED` and selectively pull members vs. current force-whole-archive. Would also unblock the libgcc.a whole-archive crash from 025. |
| ~~**#5**~~ | ~~`ppc-macho-stubs.c` cleanup~~ | ✅ Done in `00751c8`. Stubs file was dead since session 009 — `ppc-macho.c` had subsumed all its symbols. |
| ~~**#6**~~ | ~~De-duplicate UNDEF symbols~~ | ✅ Done in `dc7a05d`. The dupe came from emitting one UNDEF nlist entry per stubs[] element AND another per nlptrs[] element when both lists referenced the same elfsym (e.g. `_atexit` referenced as both call target via REL24 and data pointer via ADDR32 from crt1.o's static-init machinery). Refactored to build an elfsym→nlist map so stub_sym_idx and data_sym_idx share UNDEF entries. |
| **#7** | **Self-link diagnostics** | (Was old #10.) When the EXE writer fails the user gets `dyld` errors with no context. Better messages would shorten future 025-style debugging considerably. |

### Larger scope

| | item | notes |
|---|---|---|
| **#8** | **DWARF debug info emission** | (Was old #11.) `tcc/tccdbg.c` has no PPC support → backtraces from tcc-built programs are useless. Would unblock `lldb` / `gdb` debugging. |
| **#9** | **AltiVec intrinsics** | (Was old #12.) None today; tcc emits scalar code for everything. Plausible but a large project. |
| **#10** | **Real-world program builds** | Lua 5.4.7 ✅ done in v0.2.12. sqlite3 next — `sqlite3_open` works since v0.2.15; `sqlite3_prepare_v2` with `select 1+1` still hits a Heisenbug deferred from session 041. |
| **#11** | **bcheck.c port** | If we want `-b` instrumented builds to actually instrument. Multi-session lift. Codegen hooks in `ppc-gen.c` partially exist (`func_bound_offset` / `func_bound_ind` are unused statics); the no-op stubs in `lib-ppc.c` need replacing with a real port. |

## Out of scope (still)

- C++ support. TCC has none anywhere.
- Cross-compilation FROM ppc TO other targets. We only need
  ppc-darwin8 native.
- 64-bit PPC (G5 in 64-bit mode). 32-bit only; runs on G5 in 32-bit
  mode anyway.
- libtcc thread safety. Disabled at build time.
- Inline assembly support beyond the basic `__asm__` block (gating
  some of #1 above; if it ever lands, would unblock several
  follow-ups).

## Testing methodology

In rough order of cost vs. confidence:

1. **`scripts/run-tests2.sh`** — full TCC tests2 suite.
   `NORUN=true` (default) exercises `-o exe`; `RUN=1` exercises
   `tcc -run`. ~7 min wall under default since v0.2.8 (was ~3 min
   before real atomics; 124_atomic_counter takes 6+ min through
   the global mutex).

2. **`FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh`** — bootstrap
   fixpoint. tcc → tcc-self → tcc-self2 → tcc-self3, verifying
   `tcc-self2.o == tcc-self3.o`. Catches any regression that
   affects tcc.c's own compilation.

3. **The pre-release demos** (`demos/s00*-*.c`,
   `demos/s025-self-link.sh`). Fast smoke for basic codegen + exec
   output after non-trivial changes.

4. **Differential testing against `gcc-4.0`** for new features.
   Compile the same C with both, compare output / exit code.
   Cheapest way to catch semantic divergence.

5. **Real-world program builds** (sqlite, lua). Smoke-tests the
   broader codegen surface that synthetic tests don't exercise.

6. **Upstream `make -k test` in `tcc/tests/`** (since session 042).
   Runs hello-exe / hello-run / libtest / libtest_mt / test3 /
   abitest-cc / abitest-tcc / vla_test-run / tests2-dir / pp-dir /
   memtest / dlltest / cross-test / btest / tccb. Currently the
   five passing stages are version, hello-exe, hello-run, libtest,
   vla_test-run, pp-dir (24/24), tests2-dir (111/111), memtest. The
   rest fail on known-deferred items (long double 128-bit ABI,
   13-FPR support, Mach-O dylib output, real bcheck port,
   cross-target configure). See
   [session 042 README](sessions/042-upstream-tests-2026-05-05/README.md).

## Risk register

- **bcheck blocker**. Five test failures plus three `-run` regressions
  all gate on the same `bcheck.c` port. Multi-session lift. Until
  it lands the test count is stuck around 89-90%.

- **lwarx/stwcx blocker**. The pthread_mutex atomics are correct
  but the mutex-contention scaling is bad. A real implementation
  needs tcc to grow inline-asm OR a separate `.S` build path; that's
  a prerequisite, not a small thing.

- **Tiger's libSystem is old**. Functions added after 10.4 (e.g.
  several POSIX 2008 additions) aren't available. Programs using
  them fail to link with informative errors. Constraint to
  communicate, not really a risk.

- **No DWARF / no debugger**. Debugging tcc-built programs is
  hex-dump-and-disassemble work. Slows down anyone hitting an
  obscure bug. Item #8 above.

- **Out-of-tree upstream divergence**. We modify tcc/ in place
  rather than maintaining a patch set. If we ever want to rebase
  onto a newer upstream, that's a session task — not a routine
  workflow.
