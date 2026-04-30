# Session 002 — baseline build findings

## What works out of the box

### Uranium (arm64 Darwin, host build)

Vanilla mob compiles cleanly with `clang 17` on macOS arm64:

- `./configure --prefix=/tmp/tcc-baseline-uranium` — detects `arm64
  Darwin`, writes `config.mak` and `config.h` cleanly.
- `make -j4` — builds `tcc`, `libtcc.a`, `libtcc1.a` without
  errors.
- `./tcc -B. /tmp/hello.c -o /tmp/hello-tcc && /tmp/hello-tcc` —
  compiles and runs a hello world.

This confirms our `tcc/` snapshot is a sound copy of mob and that
nothing about the rsync / in-tree workflow corrupted the source.

### imacg3 (PowerPC G3, Tiger 10.4.11, target build)

`./configure` detects the host correctly:

```
Build OS            Darwin Power Macintosh
C compiler          gcc-4.0 (4.0)
Target OS           Darwin
CPU                 ppc
Config              OSX dwarf=4 codesign new_macho=no BIGENDIAN
```

Notably:
- `CPU=ppc` is recognized — there's a configure branch
  `"Power Macintosh"|ppc|ppc64) cpu="ppc"` at line 339.
- `BIGENDIAN` is set — the configure script knows to set this for
  `ppc|mips|s390`.
- `new_macho=no` — surprising. There's pre-existing awareness of an
  "old" Mach-O variant. Worth investigating in a later session.
- `xcrun: command not found` warning is harmless on Tiger; configure
  proceeds.
- `--config-semlock=no` is required (see blocker #1 below).

## Blockers found

### Blocker #1 — GCD doesn't exist on Tiger (worked around)

`tcc.h:1934-1944` uses Grand Central Dispatch (`<dispatch/dispatch.h>`,
`dispatch_semaphore_t`) on the `__APPLE__` branch. GCD shipped in
10.6 Snow Leopard; Tiger has POSIX `<semaphore.h>` but not GCD.

**Workaround in this session:** pass `--config-semlock=no` at
configure time. Disables the entire SEMLOCK feature. Acceptable for
v1 — it's used for thread safety in `libtcc`, which we don't need
for a self-hosting compiler driver.

**Permanent fix (deferred):** rewrite the `#elif defined __APPLE__`
branch to fall through to the POSIX `<semaphore.h>` path on
pre-10.6 SDK, gated on `MAC_OS_X_VERSION_MIN_REQUIRED`. Would let
us turn SEMLOCK back on if we ever need libtcc thread safety on
Tiger.

### Blocker #2 — Makefile has no ppc target (expected)

```
ar rcs libtcc.a
ar: no archive members specified
```

The build dies because `LIBTCC_OBJ` is empty. Tracing back:
`Makefile:223 LIBTCC_SRC = $(filter ... $($T_FILES))`. With `T=ppc`,
the lookup is `$(ppc_FILES)` which is undefined. Compare line 215
where `arm64_FILES` is defined; there is no `ppc_FILES` line.

This is the **expected baseline** — Makefile wiring matches the
source-tree state (no `ppc-gen.c`, `ppc-link.c`, `ppc-asm.c`,
`ppc-tok.h`).

**Fix:** session 003 will add the Makefile entries and stub source
files together.

## Things to investigate later

- **`new_macho=no`** — what is the "old vs new" Mach-O distinction
  in `tccmacho.c`? Could be useful for our 32-bit PPC Mach-O work,
  since the legacy Mach-O format may be closer to what 10.4 expects.
- **`codesign` config flag is set** — Tiger predates code signing
  (introduced in 10.5). Need to disable this for our Tiger build,
  or confirm the codesign code path is a no-op when the `codesign`
  binary isn't present.
- **`dwarf=4`** is on by default — DWARF debug emission. We've
  decided debug info is out of scope for v1; verify we can disable
  it with `--config-dwarf=no` (or similar) and that the source
  compiles cleanly without DWARF.

## Build infrastructure decision

Since the host gcc on imacg3 is **gcc-4.0.1 (Apple)**, build it on
G3 directly rather than cross-compiling from uranium. Reasoning:

- TCC is small; a clean build on G3 is fast (a few minutes).
- Cross-compiling would require building a PPC-targeting cross-cc
  toolchain on uranium, plus library mocking — significant friction
  for a project this small.
- We have `gcc-10.3.0` and `gcc-4.9.4` available in `/opt` if we
  need a more modern host compiler (e.g. for C99 features).
- The end product is a tarball that runs on G3, so testing on G3
  is the source of truth anyway.

The rsync workflow (uranium → imacg3) is straightforward and
fast.

## Configure options that worked

```
./configure --prefix=/tmp/tcc-baseline --config-semlock=no
```

Other options to try in session 003:

```
--config-codesign=no       # Tiger predates codesign
--config-dwarf=no          # debug info out of scope for v1
--config-new-macho=no      # already set automatically; document why
--cpu=ppc                  # explicit (auto-detected anyway)
```
