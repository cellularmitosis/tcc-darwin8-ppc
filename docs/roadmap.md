# Roadmap

## Where we are (May 2026)

Self-hosted tcc for Tiger PowerPC, **shipped through v0.2.8-g3**.
Eight patch releases on top of [v0.2.0-g3](sessions/028-v0.2.0-g3-release/README.md)
have brought tests2 from 70 / 122 (57%) to **105 / 118 (89.0%)**:

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

(Total dropped 122 → 118 in v0.2.6 because four LE-byte-order-
specific tests are properly classified as skipped on big-endian.)

* Bootstrap fixpoint (`tcc → tcc-self → tcc-self2 → tcc-self3` with
  `tcc-self2.o == tcc-self3.o`) holds at every release.
* `tcc -run hello.c` works (since v0.2.7).
* `tcc -o exe` produces working libSystem-using binaries.
* Multi-threaded atomics correct (since v0.2.8).

## Open work, prioritized

### Concrete remaining test failures (13)

| Bucket | Tests | Effort |
|---|---|---|
| **bcheck.c port** | 112_backtrace, 113_btdll, 115_bound_setjmp, 116_bound_setjmp2, 126_bound_global | Multi-session. Codegen instrumentation in `ppc-gen.c` partially exists (`func_bound_offset` / `func_bound_ind` are unused statics); the no-op stubs in `lib-ppc.c` need replacing with a real port of `tcc/lib/bcheck.c` (hash-table region tracking + signal hooks). Would also unblock 117_builtins (its `-b` second invocation needs `bcheck.o`) and the three `-run`-only failures (121_struct_return, 122_vla_reuse, 132_bound_test) that use `-b` and pass under `-o exe` only because the no-op stubs are linked in. |
| **`-dt` test classification** | 60_errors_and_warnings, 96_nodata_wanted, 125_atomic_misc, 128_run_atexit | These need `-dt -run`. Several pass under `RUN=1` already. Two paths: (a) update `scripts/run-tests2.sh` to drop `NORUN=true` so per-test `NORUN` overrides apply, or (b) add `T1` overrides per test in the Makefile. 128's `on_exit` is glibc-only on Mac and won't pass either way. |
| **104_inline** | inverse: passes under `-run`, fails under `-o exe` | Test-mode quirk; same `-dt` classification issue from the other side. |
| **70_floating_point_literals** | 5-ULP error in upstream tcc's `parse_number` on PPC IEEE 754 | Upstream tcc bug; surfaces only on PPC's IEEE semantics. Deep but contained. |
| **73_arm64** | HFA aggregates designed for AArch64 ABI | Won't fully pass — the test exercises HFA semantics PPC doesn't have. The handful of sub-tests that fail isolate to long-double struct-by-value cases that span the GPR/stack boundary. Worth fixing the codegen even though the test is platform-mismatched; same fix would help any program passing big aggregates. |

### Structural items

| | item | notes |
|---|---|---|
| **#1** | **Real lwarx/stwcx atomics** | The v0.2.8 pthread_mutex implementation is correct but slow (4.2M lock cycles for 124_atomic_counter). Lock-free PPC atomics need either tcc inline-asm support OR a `.S` → `.o` build path through gcc-4.0. |
| **#2** | **2-element HFA struct-by-value on GPR overflow** | 73_arm64 isolates this. Generic struct-by-value codegen issue when arguments span r3..r10 + stack. |
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
| **#10** | **Real-world program builds** | sqlite amalgamation (smoke-tested in [030](sessions/030-sqlite-smoke/README.md), blocked at the time on struct-by-value which has since landed — worth retrying), then lua. |

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
