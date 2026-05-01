# 022 — v0.1.0-g3 release tarball

First `/opt`-installable tarball for tcc-darwin8-ppc on Tiger PowerPC.
Built and verified on iMac G3.

## Layout

```
tcc-darwin8-ppc-v0.1.0-g3.tar.gz   (≈146 KB gzipped)
└── tcc-darwin8-ppc-v0.1.0-g3/
    ├── bin/
    │   └── tcc                              # the self-hosted PPC binary
    ├── lib/
    │   └── tcc/
    │       ├── libtcc1.a                    # runtime helpers (__floatundidf)
    │       └── include/                     # tcc's own headers
    │           ├── float.h, stdarg.h, stddef.h, …
    │           └── tcclib.h
    └── share/
        └── doc/
            └── tcc-darwin8-ppc/
                ├── README                   # project README
                ├── RELEASE_NOTES            # install instructions
                └── SELF_HOST_NOTES          # session 020 writeup
```

## Install

```sh
sudo mkdir -p /opt
sudo tar -C /opt -xzf tcc-darwin8-ppc-v0.1.0-g3.tar.gz
/opt/tcc-darwin8-ppc-v0.1.0-g3/bin/tcc -v
```

## Build (on the G3)

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
./scripts/build-release-tarball.sh
# produces tcc-darwin8-ppc-v0.1.0-g3.tar.gz in repo root
```

## Verification

```
$ ls /tmp/install-test/tcc-darwin8-ppc-v0.1.0-g3/bin/tcc
…/bin/tcc
$ /tmp/install-test/tcc-darwin8-ppc-v0.1.0-g3/bin/tcc -v
tcc version 0.9.28rc (PowerPC Darwin)

$ cat > /tmp/hello.c <<EOC
int printf(const char *,...);
int main(void) { printf("hello from installed tcc!\n"); return 42; }
EOC
$ /tmp/install-test/tcc-darwin8-ppc-v0.1.0-g3/bin/tcc \
    -B/tmp/install-test/tcc-darwin8-ppc-v0.1.0-g3/lib/tcc \
    -nostdinc -c /tmp/hello.c -o /tmp/hello.o
$ gcc-4.0 -arch ppc /tmp/hello.o \
    /tmp/install-test/tcc-darwin8-ppc-v0.1.0-g3/lib/tcc/libtcc1.a \
    -o /tmp/hello
$ /tmp/hello
hello from installed tcc!
$ echo $?
42
```

## What's still missing (deferred to a v0.2)

- Linking without gcc-4.0: tcc's own linker / Mach-O executable
  output isn't wired up for PPC yet. Currently you write `.o` with
  tcc and link with gcc-4.0. The full `-o exe` path needs the same
  PIC stubs / `__nl_symbol_ptr` machinery hooked into executable
  output, plus a Mach-O `LC_SEGMENT` writer for the text/data
  segments.
- `tcc -run` for programs that do function calls (currently SIGILLs
  in JIT mode; OBJ output works fine).
- Full upstream regression suite (we run our own demos and
  hand-written tests; haven't pointed `tests/tests2` at our binary).
- `floats.h`, `stddef.h`, etc. for full C99 compliance — what's
  bundled today is what tcc ships with, and that's been adequate
  for our test programs but not exercised broadly.

## What this means

TCC has never had a PowerPC backend in any release — Bellard's
0.9.27, the community 0.9.27, the active mob branch all skip PPC
entirely. This release is the first time tcc compiles itself on
PowerPC, and the first time anyone (as far as we can tell) has
shipped a self-hosted tcc for Mac OS X 10.4 Tiger.

## Files added

- `scripts/build-release-tarball.sh` — orchestrates the four-step
  build (configure + make + bootstrap + stage + tar).
