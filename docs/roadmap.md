# Roadmap

## Where we are (May 2026)

Self-hosting tcc on Tiger PowerPC: **shipped as
[v0.1.0-g3](sessions/022-v0.1.0-g3-release/README.md)**. A
`/opt`-installable tarball that compiles `tcc.c` into a `.o`
byte-identical to its own — the canonical self-host fixpoint, on a
22-year-old G3.

Since the v0.1.0 release, sessions 023-025 closed the
"executable output" gap. Today, plain `tcc -o exe file.c` produces a
working libSystem-using binary on Tiger, with no `gcc-4.0`
involvement at link time:

* [023](sessions/023-macho-executable/README.md) — Phase A+B exec
  writer (syscall-only programs).
* [024](sessions/024-libsystem-init/README.md) — extern-data PIC,
  __mh_execute_header, libSystem-init attempts.
* [025](sessions/025-macho-o-reader/README.md) — classic Mach-O `.o`
  reader, auto-loads `/usr/lib/crt1.o`. Full self-link for normal
  programs.

The one remaining hole: `tcc-self` itself still needs `gcc-4.0` to
link, because `tcc.c` references libgcc runtime helpers
(`__udivdi3`, `__ashldi3`, `__fixdfdi`, etc.) that aren't yet in
our `libtcc1.a`. Closing that loop is the v0.2.0 milestone.

## Next milestone: v0.2.0

A `/opt`-installable tarball that:

1. Bootstraps end-to-end via tcc alone (no `gcc-4.0` anywhere in the
   pipeline).
2. Passes a meaningful chunk of TCC's own `tests/` and `tests2/`
   suites.
3. Builds at least one nontrivial real-world program (sqlite
   amalgamation is the canonical smoke test).

## Open work, prioritized

### Toward v0.2.0

1. ✅ **Bundle libgcc helpers into `libtcc1.a`.** Done in
   [026](sessions/026-libgcc-helpers/README.md) — `lib-ppc.c`
   provides ten helpers (`__floatundidf`, `__fixunsdfdi`,
   `__fixdfdi`, `__ashldi3`, `__lshrdi3`, `__ashrdi3`,
   `__udivdi3`, `__umoddi3`, `__divdi3`, `__moddi3`). Verified
   end-to-end by tcc-linking a long-long signed-divide program.

2. ✅ **Resolve the keymgr / DWARF init-order bug + four other
   independent bugs converging on the same SIGBUS-on-launch
   symptom.** Done in
   [027](sessions/027-self-link/README.md) — auto-injected keymgr
   stub that calls `_dyld_register_func_for_{add,remove}_image` (a
   load-bearing side effect that kicks libSystem's init);
   populated `__DATA,__dyld` with the dyld helper pointers; set
   `REFERENCED_DYNAMICALLY` on `_environ`/`_NXArgc`/`_NXArgv`/
   `__mh_execute_header`; **sorted external defined symbols
   alphabetically** (dyld uses binary search!); skipped crt1's
   private machinery from the external symbol table; bypassed a
   tcc PPC backend bug in `put_nlist`'s narrow-arg call sequence.

3. ✅ **Self-host fixpoint reached.** `bootstrap-tcc-self.sh
   FIXPOINT=1` runs the canonical chain: tcc → tcc-self.o →
   tcc-self → tcc-self2.o → tcc-self2 → tcc-self3.o, then
   verifies `tcc-self2.o == tcc-self3.o` byte-identical
   ([027](sessions/027-self-link/README.md)). The pre-existing
   23-25 era 32-byte regression vs gcc-built tcc's output remains
   (so tcc.o ≠ tcc-self.o), but tcc-self → tcc-self2 → tcc-self3
   is a stable fixpoint, which is what self-host actually means.

4. **Wire up TCC's own testsuites.** `tcc/tests/` and `tcc/tests2/`
   together hold ~330 small C programs covering language and
   codegen edge cases. Get the Make targets running on PPC, capture
   a baseline pass/fail count, fix the obvious wins, document the
   rest. (Session 028.)

5. **Real-world program smoke tests.** Build sqlite3 amalgamation
   with `tcc -o`, then lua. Document what breaks, fix what's quick,
   capture the rest as follow-ups. (Session 029.)

6. **Cut v0.2.0.** Update `scripts/build-release-tarball.sh` to
   bundle the self-link path. Regenerate demos. Update the README
   status table. Tag, release notes, GitHub upload. (Session 030.)

### After v0.2.0

7. **Proper Mach-O archive alacarte loader.** Parse `__.SYMDEF
   SORTED` and selectively pull members that resolve undefined
   symbols, instead of the current force-whole-archive on Mach-O.
   Would also fix the libgcc.a whole-archive crash noted in 025.

8. **`ppc-macho-stubs.c` cleanup.** Was supposed to be deleted
   "when phase 3 lands." Phase 3 has long since landed; verify
   nothing references it and remove.

9. **De-duplicate UNDEF symbols in the Mach-O reader.** Spotted in
   026: tcc-linked binaries have duplicate UNDEF entries for
   `___keymgr_dwarf2_register_sections`, `_atexit`, `_exit` when
   both crt1.o and another input reference them. Looks like the
   `macho_translate_sym` coalescing path needs work.

10. **Improve self-link diagnostics.** Currently when something
    goes wrong in the EXE writer the user gets `dyld` errors with
    no context. Better error messages would shorten future
    debugging sessions like 025 considerably.

### Larger scope (post-v0.2.0)

11. **DWARF debug info emission.** Currently `tcc/tccdbg.c` has no
    PPC support, so backtraces from tcc-built programs are useless.
    Would unblock `lldb`/`gdb` debugging.

12. **AltiVec intrinsics.** None today; tcc emits scalar code for
    everything. The port is plausible but a large project.

13. **Bounds-checking mode.** `-bt` and `-bcheck`. Same story as
    AltiVec — exists for other backends, just nobody wired up the
    PPC version.

## Out of scope (still)

- C++ support. TCC has none anywhere.
- Cross-compilation FROM ppc TO other targets. We only need
  ppc-darwin8 native.
- 64-bit PPC (G5 in 64-bit mode). 32-bit only; runs on G5 in 32-bit
  mode anyway.
- libtcc thread safety. Disabled at build time.
- Inline assembly support beyond the basic `__asm__` block.

## Testing methodology

In rough order of cost vs. confidence:

1. **The four pre-release demos** (`demos/s00*-*.c`,
   `demos/s025-self-link.sh`). Quick smoke that the basic codegen +
   exec output works after every nontrivial change.

2. **Self-host fixpoint.** Run
   `scripts/bootstrap-tcc-self.sh` and confirm `tcc-self` compiles
   `tcc.c` into a `.o` byte-identical to the original. Catches
   regressions in any path that affects compilation correctness.

3. **TCC's own `tests/` and `tests2/`** (Phase 4 above, currently
   not wired up). When working, this is the broadest catch-net for
   codegen bugs.

4. **Differential testing against `gcc-4.0`** for any new feature.
   Compile the same C with both, compare exit code / stdout.
   Cheapest way to catch semantic divergence.

5. **Real-world program builds** (sqlite, lua). Smoke test for
   things the synthetic tests don't exercise (long compilation
   units, real-world preprocessor patterns, etc.).

## Risk register

- **libgcc.a whole-archive load is broken** (known limitation from
  025). The whole-archive path loads all 75 members but produces a
  binary that crashes even simple `printf`. Bypassed for v0.2.0 by
  bundling helpers into libtcc1.a; properly fixed by item #5.

- **Mach-O testsuite expectations.** Some `tcc/tests*/` tests
  assume ELF idioms (e.g. checking `nm` output formats). Likely
  some failures will be testsuite-format issues, not real bugs.
  Need to triage.

- **Tiger's libSystem is old.** Functions added after 10.4 (e.g.
  several POSIX 2008 additions) aren't available. Programs that
  use them will fail to link with informative errors; not really a
  risk so much as a constraint to communicate.

- **No DWARF / no debugger.** Debugging tcc-built programs is
  hex-dump-and-disassemble work. Slows down anyone hitting an
  obscure bug. See item #8.
