# tcc-darwin8-ppc

A Mac OS X 10.4 Tiger / PowerPC backend for [tcc](https://repo.or.cz/tinycc.git),
the Tiny C Compiler.

**Status: very early.** Backend scaffold not yet started; we just
finished researching and standing up the build infrastructure.
Self-hosting on Tiger PPC is the goal; a `/opt`-installable G3
tarball is the first major milestone.

## Status

- ✅ Source base chosen: [mob branch @ b39da9f6](tcc/.UPSTREAM.md)
- ✅ Architecture & licensing surveyed
  ([000](docs/sessions/000-source-survey/README.md),
  [001](docs/sessions/001-fork-comparison/README.md))
- ✅ Baseline builds verified on uranium (host) and imacg3 (target)
  ([002](docs/sessions/002-baseline-build/README.md))
- ⏳ Backend scaffold (next session)
- ❌ Codegen
- ❌ Mach-O ppc32 emission
- ❌ Self-host bootstrap on Tiger
- ❌ G3 tarball

[Roadmap](docs/roadmap.md) — phases 0 through 6, ~30 sessions total.

## What this project is

TCC ships with backends for i386, x86_64, ARM, ARM64, C67, and
RISC-V64. It has **never** had a PowerPC backend in any of its
releases (Bellard's, the 0.9.27 community release, or the active
mob branch). This project adds one, targeting 32-bit PowerPC on
Mac OS X 10.4 Tiger Mach-O.

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
  instruction set with a clean RISC dispatch model. Easier to
  hand-emit than the ARM mess.

## Building (for the curious — does not yet work end-to-end)

Source layout: `tcc/` is a snapshot of the mob branch, modified
in-place. There is no patch-set workflow.

```sh
# host build (uranium / Apple Silicon Mac):
cd tcc && ./configure && make           # works today

# target build (imacg3 / G3 Tiger):
ssh imacg3
cd ~/tcc-darwin8-ppc/tcc
./configure --config-semlock=no
make                                    # currently fails: no ppc backend
```

The G3 build will succeed once the backend scaffold lands in
session 003.

## Sister projects

(Currently private — will cross-link here once they're public.)

## License

Project code is MIT-licensed. The `tcc/` subdirectory inherits the
upstream TCC licensing (LGPL 2.1 with most files MIT-relicensed per
[`tcc/RELICENSING`](tcc/RELICENSING)). A handful of files (notably
`arm-gen.c`) remain LGPL-only; we don't copy from those.

See [`tcc/COPYING`](tcc/COPYING) and
[`tcc/RELICENSING`](tcc/RELICENSING) for upstream details.
