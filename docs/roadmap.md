# Roadmap

Ultimate goal: **self-hosting tcc on Mac OS X 10.4 Tiger PowerPC**.

First major shippable milestone: a `/opt`-installable G3 tarball
that, when unpacked, gives the user a `tcc` that "just works" —
compiles and links a hello-world, links against system libc, runs
on a G3.

(G4/G5 forward-compatibility comes for free — all G3 instructions
are a strict subset of G4/G5 ISA. Tarballs will be labeled "g3"
to signal the floor.)

## Phases

### Phase 0: research & infrastructure ✅
- 000 — pick base (mob)
- 001 — characterize forks
- 002 — baseline build on uranium and imacg3 (this session)

### Phase 1: scaffold the backend
- 003 — Makefile entries (`ppc_FILES`, `ppc-osx_FILES`), stub
  files (`ppc-gen.c`, `ppc-link.c`, `ppc-asm.c`, `ppc-tok.h`).
  Goal: `make` completes (with stubs that abort at runtime).
- 004 — wire in `tccmacho.c` for ppc (CPU type, header
  emission). Get the linker side compiling, even if it emits
  garbage.

### Phase 2: incremental codegen
Each step: pick the smallest C program that exercises the
feature, get it to compile + link + run on imacg3, commit, move
on. Differential test against system gcc-4.0 as oracle.

- 005 — function prologue / epilogue / return (no body)
- 006 — integer literals, integer arithmetic
- 007 — local variables, stack frame
- 008 — branches and loops
- 009 — function calls (caller side, non-vararg)
- 010 — function calls (callee side, parameter unpacking)
- 011 — pointers, dereference, address-of
- 012 — global variables, BSS/data sections
- 013 — string literals, .const sections
- 014 — struct member access (load/store at offset)
- 015 — float / double codegen (FPU registers, conversions)
- 016 — varargs (Apple PPC ABI's specifics)
- 017 — struct passing in registers (Apple PPC ABI)
- 018 — struct return values

### Phase 3: linker / dynamic linking
- 019 — Mach-O symbol table, string table
- 020 — relocations
- 021 — `__TEXT` / `__DATA` segment layout
- 022 — dyld stubs (calls into libSystem)
- 023 — getting `printf` to actually print

### Phase 4: testsuite
- 024 — get tests/tests2 running with system gcc as oracle on imacg3
- 025 — run our tcc against same suite, fix gaps
- 026 — bench: how fast does it compile compared to gcc-4.0?

### Phase 5: bootstrap
- 027 — tcc-on-G3 compiles `tcc.c` itself
- 028 — full self-host: tcc compiles all of itself, the resulting
  binary passes the testsuite

### Phase 6: G3 release
- 029 — package layout for `/opt/tcc-VERSION/`
- 030 — sysroot decisions (which Tiger headers to ship vs. point to)
- 031 — `make install` works on imacg3
- 032 — tarball: `tcc-darwin8-ppc-VERSION-g3.tar.gz`
- 033 — release notes, GitHub release upload, README update

## Testing methodology

We use multiple testing layers, each catching different failure
modes:

1. **Differential testing** — for any non-trivial codegen step,
   compile the same C program with `gcc-4.0` (oracle) and our tcc,
   compare:
   - exit code
   - stdout / stderr
   - (where applicable) generated assembly under `gcc -S` for
     manual review
   This is the cheapest, most useful test — it catches semantic
   bugs that a "did it compile" check would miss.

2. **TCC's `tests/` and `tests2/`** — already in-tree at
   `tcc/tests*/`. ~330 small C programs covering language features
   and codegen edge cases. Run with `make test` after each
   non-trivial codegen change.

3. **Per-feature regression suite under `./tests/`** — small C
   programs we add ourselves, one per codegen feature. Lives
   outside `tcc/` so it's not touched by upstream re-syncs (if we
   ever do one). Each has an expected-output file; a runner script
   compiles with our tcc, runs, and diffs.

4. **Bootstrap test** — the ultimate end-to-end. Tcc compiles its
   own source. The result must pass the testsuite. If both hold,
   we're self-hosting.

5. **Real-world programs** — once self-hosting works, build a
   couple of small known-good C programs (e.g. lua, sqlite
   amalgamation) as smoke tests. Not a release blocker, but
   useful confidence signal.

## Out of scope for first G3 release

- DWARF debug info emission (`tccdbg.c` / `--config-dwarf=yes`)
- bounds-checking mode (`-bt`)
- C++ support (TCC has none anyway, but worth being explicit)
- assembler-as-frontend (`tcc -c foo.S`) — defer until we know
  more about which inline-asm forms actually appear in real C
  code on Tiger
- cross-compilation FROM ppc TO other targets — we only need
  ppc-darwin8 native
- 64-bit PPC (G5 in 64-bit mode) — 32-bit only, runs on G5 in
  32-bit mode anyway
- AltiVec intrinsics — none for v1; basic float/double only
- libtcc thread safety (SEMLOCK) — disabled at build time

## Risk register

- **Apple PPC ABI is documented but not in tcc** — every other
  TCC backend has SVR4 ABI as starting point. Apple's PPC ABI is
  similar but has its own struct-passing rules. Sources:
  Apple's "Mac OS X ABI Function Call Guide" (still on
  developer.apple.com via the Wayback Machine), and
  PowerPC32 SVR4 ABI doc as comparison baseline.
- **Mach-O 10.4 vs modern** — `tccmacho.c` was written for 10.6+.
  10.4 may not accept some load command variants. The
  `new_macho=no` configure flag suggests there's awareness of
  this; investigate.
- **gcc-4.0 may reject some C99 in the source** — if the mob
  source uses C99 features that gcc-4.0.1 doesn't support, we
  fall back to `gcc-4.9.4` from `/opt`. Worst case, edit source.
