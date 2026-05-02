# 028 — v0.2.0-g3 release

## Result

✅ **Shipped `tcc-darwin8-ppc-v0.2.0-g3.tar.gz`.** A 141 KB
`/opt`-installable tarball containing a fully self-hosted,
self-linked PPC tcc — no `gcc-4.0` involvement in the bootstrap
chain. Built end-to-end on `ibookg37` (PowerBook G4 600MHz, Tiger
10.4.11). Verified by installing the tarball to a scratch dir and
compiling a hello-world that runs.

## What's in the tarball

```
/opt/tcc-darwin8-ppc-v0.2.0-g3/
├── bin/
│   └── tcc                    — the self-linked PPC binary
├── lib/
│   └── tcc/
│       ├── libtcc1.a          — runtime helpers
│       └── include/           — tcc's own headers
└── share/
    └── doc/
        └── tcc-darwin8-ppc/
            ├── README
            ├── RELEASE_NOTES
            ├── SELF_HOST_NOTES   — 020 writeup (the fixpoint)
            └── SELF_LINK_NOTES   — 027 writeup (the four bugs)
```

## What changed in v0.2.0 vs v0.1.0-g3

* **Self-link**: `bootstrap-tcc-self.sh` no longer needs `gcc-4.0`
  for the link step. tcc reads `/usr/lib/crt1.o` directly via the
  Mach-O `.o` reader landed in 025.
* **Bundled libgcc helpers** (026): ten helpers in
  `lib/lib-ppc.c` cover everything `tcc.c` needs from libgcc —
  long-long arithmetic, IEEE 754 conversions.
* **Self-link works** (027): four overlapping bugs in the EXE
  writer were peeled off, ending with the discovery that dyld uses
  binary search on the externally-defined symbol table and our
  emission path was unsorted.
* **Fixpoint verification** baked into the bootstrap script:
  `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` now runs the
  canonical chain `tcc-self2 → tcc-self3` and verifies the .o
  files are byte-identical. The release script always runs this.

## Build process

```sh
$ ssh ibookg37
$ cd ~/tcc-darwin8-ppc
$ ./scripts/build-release-tarball.sh
==> step 1/4: build host tcc with gcc-4.0
==> step 2/4: bootstrap tcc-self + fixpoint test
  CC tcc.c (ONE_SOURCE; via ./tcc/tcc)
  LINK /tmp/tcc-darwin8-ppc-v0.2.0-g3-build/tcc-self (via ./tcc/tcc, no gcc-4.0)
  TEST /tmp/tcc-darwin8-ppc-v0.2.0-g3-build/tcc-self -v
tcc version 0.9.28rc (PowerPC Darwin)
  CC tcc.c (via .../tcc-self) -> tcc-self2.o
  LINK /tmp/tcc-self2 (via .../tcc-self)
  CC tcc.c (via /tmp/tcc-self2) -> tcc-self3.o
  LINK /tmp/tcc-self3 (via /tmp/tcc-self2)
  FIXPOINT HOLDS: tcc-self2.o == tcc-self3.o
  done
==> step 3/4: stage install tree under /tmp/tcc-darwin8-ppc-v0.2.0-g3
==> step 4/4: package tcc-darwin8-ppc-v0.2.0-g3.tar.gz
-rw-r--r-- ... 144447 ... tcc-darwin8-ppc-v0.2.0-g3.tar.gz
```

## Install + smoke test

```sh
$ sudo tar -C /opt -xzf tcc-darwin8-ppc-v0.2.0-g3.tar.gz
$ /opt/tcc-darwin8-ppc-v0.2.0-g3/bin/tcc -v
tcc version 0.9.28rc (PowerPC Darwin)

$ cat hello.c
#include <stdio.h>
int main(){printf("hello from v0.2.0!\n"); return 0;}

$ /opt/tcc-darwin8-ppc-v0.2.0-g3/bin/tcc \
    -B /opt/tcc-darwin8-ppc-v0.2.0-g3/lib/tcc \
    -o hello hello.c \
    /opt/tcc-darwin8-ppc-v0.2.0-g3/lib/tcc/libtcc1.a
$ ./hello
hello from v0.2.0!
```

## Known limitations carried into v0.2.0

These don't block the release but are documented for users:

* Long-long shift codegen has a residual bug for some inputs in
  tcc-self-built output (`0x100000000LL << 4` returns 0 instead
  of `0x1000000000`). Same source compiled by gcc-built tcc gives
  the right answer.
* Common symbols from crt1.o end up with the wrong nlist type
  (lowercase `c` instead of `C`) — same general class of tcc PPC
  backend codegen issue as the long-long shift one. Doesn't affect
  runtime for any program we've tested, but worth fixing.
* `tcc-self.o` is 32 bytes shorter than the gcc-built tcc's
  `tcc.o` for the same input — a regression introduced somewhere
  in 023-025 (pre-existing per
  [026/findings.md](../026-libgcc-helpers/findings.md)). The
  tcc-self → tcc-self2 → tcc-self3 fixpoint still holds; this only
  breaks parity with gcc-built tcc's output.
* The error/warning emission path in `libtcc.c` was originally
  using `fprintf+fflush(stderr)`, which crashed in tcc-self-built
  code due to a tcc PPC backend bug. Workaround landed in 027
  (commit `f3e58c8`): use `write(2)` directly instead. This was
  necessary to make the bootstrap chain succeed without `-w`. The
  underlying backend bug is unfixed — any other `fflush(stderr)`
  call site in tcc.c could trip the same wire — but this was the
  only one in the hot path.

## Files touched

| | |
|---|---|
| `scripts/build-release-tarball.sh` | bumped to v0.2.0-g3 default; added FIXPOINT=1; ship SELF_LINK_NOTES |
| `tcc-darwin8-ppc-v0.2.0-g3.tar.gz` | the release artifact (not committed; cut a GitHub release with it) |
