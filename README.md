# tcc-darwin8-ppc

A Mac OS X 10.4 Tiger / PowerPC backend for [tcc](https://repo.or.cz/tinycc.git),
the Tiny C Compiler.

**Status: v0.1.0-g3 SHIPPED.** TCC has never had a PowerPC backend
in any release. As of session [020](docs/sessions/020-self-host/README.md),
tcc-built-by-tcc compiles `tcc.c` into a `.o` byte-identical to
its own — the canonical self-host fixpoint, on a 22-year-old G3.
A `/opt`-installable tarball is built end-to-end by
`scripts/build-release-tarball.sh` ([022](docs/sessions/022-v0.1.0-g3-release/README.md)).

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
| ✅ | Pointers, arrays, modulo ([008](docs/sessions/008-pointers-modulo/README.md), [demo](demos/s008-array-sum.c)) |
| ✅ | **Real PPC Mach-O `.o` output, links with gcc, runs on G3** ([009](docs/sessions/009-tccmacho-ppc/SESSION_README.md), [demo](demos/s009-real-macho.sh)) |
| ✅ | Long long arithmetic, args, return ([010](docs/sessions/010-long-long/README.md), [demo](demos/s010-long-long.c)) |
| ✅ | Structs (member access, pass-by-pointer) ([011](docs/sessions/011-structs/README.md), [demo](demos/s011-struct.c)) |
| ✅ | Varargs (`<stdarg.h>`, `va_start`/`va_arg`/`va_end`) ([012](docs/sessions/012-varargs/README.md), [demo](demos/s012-varargs.c)) |
| ✅ | IEEE 754 single + double floating point ([013](docs/sessions/013-floating-point/README.md), [demo](demos/s013-floating-point.c)) |
| 🟡 | **Bootstrap attempt: every tcc source file compiles to .o with our tcc** ([014](docs/sessions/014-bootstrap-attempt/README.md)) — 2 codegen gaps + 1 macho bug remain before self-host links |
| 🟡 | **Bootstrap link: 11/11 .o files link to a 463KB tcc-self binary** ([015](docs/sessions/015-bootstrap-gaps/README.md)) — needs PLT stubs to actually run |
| ✅ | **PPC PIC stubs — tcc-emitted `printf` works** ([016](docs/sessions/016-ppc-plt-stubs/README.md), [demo](demos/s016-hello-printf.sh)) |
| ✅ | External-data PIC indirection — `errno` etc. work end-to-end ([017](docs/sessions/017-extern-data-pic/README.md)) |
| ✅ | **`tcc-self -v` runs** — bootstrap binary executes ([018](docs/sessions/018-tcc-self-sigbus/README.md)) |
| ✅ | **BE long-long handling fixed — tcc-self compiles real programs** ([019](docs/sessions/019-be-ll-fixes/README.md)) |
| ✅ | **🎉 SELF-HOST FIXPOINT — tcc-self2 compiles tcc.c into a byte-identical .o** ([020](docs/sessions/020-self-host/README.md)) |
| ✅ | Self-contained self-host (libtcc1.a bundles `__floatundidf`; predefined `__FLT_EVAL_METHOD__`) ([021](docs/sessions/021-libtcc1-and-predefs/README.md)) |
| ✅ | **`v0.1.0-g3` tarball — `/opt`-installable, ~146 KB, verified on iMac G3** ([022](docs/sessions/022-v0.1.0-g3-release/README.md)) |
| 🟡 | Mach-O exec output — `tcc -o exe` works for syscall-only programs ([023](docs/sessions/023-macho-executable/README.md)); libSystem stdio needs crt1 |
| 🟡 | EXE writer extended: extern-data PIC, `__data` section, auto-injected `__tcc_start_main`, `__mh_execute_header`, `LC_TWOLEVEL_HINTS` ([024](docs/sessions/024-libsystem-init/README.md)) — libSystem init still elusive |
| ✅ | **Mach-O `.o` reader → full self-link via tcc** — auto-loads `/usr/lib/crt1.o`, `tcc -o exe` produces working printf/malloc binaries with no gcc involvement ([025](docs/sessions/025-macho-o-reader/README.md)) |

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

## Building (G3 / G4, Tiger)

Primary dev host is **`ibookg37`** (300 MHz faster than the iMac G3
the project started on; both run Tiger 10.4.11 with the same gcc-4.0
+ libSystem + dyld).

```sh
ssh ibookg37
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
