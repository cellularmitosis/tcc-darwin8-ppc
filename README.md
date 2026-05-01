# tcc-darwin8-ppc

A Mac OS X 10.4 Tiger / PowerPC backend for [tcc](https://repo.or.cz/tinycc.git),
the Tiny C Compiler.

**Status: very early but moving.** TCC has never had a PowerPC
backend; we're writing one from scratch. As of session 005, tcc
compiles `int main(void) { return N; }` to correct PowerPC
instructions and executes them on a real G3 via `tcc -run`. Real
codegen (variables, arithmetic, calls) is the next ramp.

The first major milestone is a `/opt`-installable G3 tarball that
self-hosts on Tiger.

## Status

| | |
|---|---|
| ✅ | Source base chosen: [mob branch @ b39da9f6](tcc/.UPSTREAM.md) |
| ✅ | Architecture & licensing surveyed ([000](docs/sessions/000-source-survey/README.md), [001](docs/sessions/001-fork-comparison/README.md)) |
| ✅ | Baseline builds verified on uranium (host) and imacg3 (target) ([002](docs/sessions/002-baseline-build/README.md)) |
| ✅ | Backend scaffold — `tcc` builds, runs, preprocesses ([003](docs/sessions/003-backend-scaffold/README.md)) |
| ✅ | First real PPC instructions emitted, hand-verified ([004](docs/sessions/004-prolog-epilog-return/README.md)) |
| ✅ | **`tcc -run` executes PPC code on G3, exit codes propagate** ([005](docs/sessions/005-macho-stubs/README.md), [demo](demos/s005-return-the-answer.c)) |
| ✅ | Local variables, assignment, arithmetic, branches ([006](docs/sessions/006-locals-arith-control/README.md), [demo](demos/s006-factorial.c)) |
| ✅ | Function calls — direct, indirect, recursive, up to 8 int args ([007](docs/sessions/007-function-calls/README.md), [demo](demos/s007-fibonacci.c)) |
| ⏳ | Floating point |
| ⏳ | `tccmacho.c` PPC support (real .o output, dylib loading) |
| ❌ | Self-host bootstrap on Tiger |
| ❌ | G3 tarball |

[Roadmap](docs/roadmap.md) • [Sessions](docs/sessions/) • [Demos](demos/README.md)

## What this project is

TCC ships with backends for i386, x86_64, ARM, ARM64, C67, and
RISC-V64. **It has never had a PowerPC backend** in any release
(Bellard's, the 0.9.27 community release, or the active mob branch).
This project adds one, targeting 32-bit PowerPC on Mac OS X 10.4
Tiger Mach-O.

In addition to the codegen, this requires extending TCC's
`tccmacho.c` (currently hardcoded for x86_64 and arm64) to emit
32-bit PPC Mach-O object files and dyld-compatible executables.

## Why

- TCC compiles fast, in a place where everything else is slow.
  GCC takes minutes on a G3; even a quarter-functional TCC could
  meaningfully speed up small builds on the platform.
- Self-hosting a from-scratch compiler backend on a 22-year-old
  machine is fun.
- PowerPC ISA is genuinely pleasant — a fixed-size 32-bit
  instruction set with a clean RISC dispatch model.

## Building (G3, Tiger)

```sh
ssh imacg3
git clone <this-repo> ~/tcc-darwin8-ppc
cd ~/tcc-darwin8-ppc/tcc
./configure --config-semlock=no
/opt/make-4.3/bin/make           # system make is GNU 3.80, lacks $(or)
./tcc -B. -run hello.c           # works for "int main(void) { return N; }"
```

Notes:
- `--config-semlock=no` is required: tcc.h's `__APPLE__` branch
  hardcodes Grand Central Dispatch (10.6+); Tiger has POSIX
  semaphores only. The semlock feature isn't needed for our use.
- System `make` is GNU 3.80 (2002), which doesn't support `$(or
  ...)`. Use `/opt/make-4.3/bin/make` (preinstalled on the
  PowerPC fleet via `tiger.sh`).

## Building (uranium, host build for sanity check)

```sh
cd tcc
./configure
make -j8        # builds an arm64-osx tcc, has nothing to do with PPC
                # but confirms our patches don't break upstream targets
```

## Layout

```
tcc/                     - upstream mob source, modified in place. No
                           patch-set workflow. tcc/.UPSTREAM.md notes
                           the snapshot commit.
  ppc-gen.c              - PPC32 code generator (we wrote this).
  ppc-link.c             - PPC32 ELF relocation handling (we wrote).
  ppc-macho-stubs.c      - integration shims while tccmacho.c lacks
                           PPC support; deleted when phase 3 lands.
docs/
  roadmap.md             - phases and per-session breakdown.
  sessions/NNN-<slug>/   - per-session narrative + findings.
```

## Sister projects

(Currently private. Will cross-link once they're public.)

## License

Project code is MIT-licensed. The `tcc/` subdirectory inherits
upstream TCC licensing — LGPL 2.1 with most files MIT-relicensed
per [`tcc/RELICENSING`](tcc/RELICENSING). A handful of files
(notably `arm-gen.c`) remain LGPL-only; we don't copy from those.

See [`tcc/COPYING`](tcc/COPYING) and
[`tcc/RELICENSING`](tcc/RELICENSING) for upstream details.
