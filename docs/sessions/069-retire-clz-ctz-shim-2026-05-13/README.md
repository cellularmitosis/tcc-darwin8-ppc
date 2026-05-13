# Session 069 — Retire the clz/ctz half of `builtin_compat.h`

**Date:** 2026-05-13
**HEAD at start:** `c264906` (end of session 068)
**HEAD at end:** (this session's commit)
**Version bump:** none — no tcc source-code change, just CI/test
infrastructure cleanup.

## Arrival state

End-of-068 left two optional items carried over from session 067:

* (4) **Trim or retire `builtin_compat.h`** — the clz/ctz wrappers in
  [`docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h`](../062-bswap-shim-builtins-2026-05-10/builtin_compat.h)
  were no longer required for csmith differential testing once
  v0.2.49-g3 made tcc's libtcc1.a return gcc-PPC's observed UB
  values directly. The bswap32/64 + ia32_crc32qi prototypes in the
  same header were still needed (separate ABI-prototype issue).
* (5) **Confirm with a real csmith `--builtins` campaign** — run a
  few hundred seeds **without** the `-include builtin_compat.h`
  flag for the clz/ctz part and confirm tcc and gcc still agree.

Both landed in this session.

## What landed

### `bswap_compat.h` — trimmed successor

[`bswap_compat.h`](bswap_compat.h) is the post-v0.2.49 replacement
for session 062's `builtin_compat.h`. Contents:

* `extern unsigned int __builtin_bswap32(unsigned int);`
* `extern unsigned long long __builtin_bswap64(unsigned long long);`
* `extern unsigned int __builtin_ia32_crc32qi(unsigned int, unsigned char);`

That's it. The six `__compat_clz / clzl / clzll / ctz / ctzl /
ctzll` static-inline wrappers and matching `#define
__builtin_clz(x) __compat_clz(x)` macros are gone. Future csmith
`--builtins` campaigns should `-include` this file instead of
session 062's `builtin_compat.h`.

The bswap-shim **implementation** (function bodies in
[`bswap_compat.c`](../062-bswap-shim-builtins-2026-05-10/bswap_compat.c))
is unchanged. Linked into both the gcc and tcc binaries via the
`EXTRA_CC_SRC=` env var in
[`csmith_campaign.sh`](../062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh).

Session 062's `builtin_compat.h` is **kept as a historical artifact**
(the right thing as of session 062; pointing forward sessions at
the trimmed file is sufficient).

## Verification

### Direct UB-value check on imacg3

Rebuilt tcc + libtcc1.a from a fresh sync of post-068 sources on
imacg3 (after re-running `./configure --cc=gcc-4.0
--config-semlock=no` — see [Build-infra gotchas](#build-infra-gotchas)
below). Then:

```sh
cat > /tmp/clz_check.c <<EOF
#include <stdio.h>
int main(int argc, char **argv) {
    unsigned long long x = 0;
    if (argc > 1) x = 1;   /* defeat constant fold */
    x = x - x;             /* make it 0 at runtime */
    printf("ctzll(0)=%d\n", __builtin_ctzll(x));
    return 0;
}
EOF
gcc-4.0 -O0 -w /tmp/clz_check.c -o /tmp/g && /tmp/g
tcc -B$TCC -I$TCC/include /tmp/clz_check.c -o /tmp/t && /tmp/t
```

Both print `ctzll(0)=31`. Runtime values match between
compilers; `__builtin_ctzll(x)` against a runtime-zero `x` is the
shape csmith actually produces. (gcc's constant-folder gives `-1`
for the **literal** `__builtin_ctzll(0ULL)`, but csmith never
generates that pattern.)

### Csmith `--builtins` re-sweep — byte-identical to session 066

Ran [session 062's `csmith_campaign.sh`](../062-bswap-shim-builtins-2026-05-10/csmith_campaign.sh)
on imacg3 over the same seed range session 066 used:

* Range: 8020-8419 (400 seeds), `--builtins+bitfields` corpus
  generated for session 065/066.
* Source: `/Users/macuser/tmp/csmith-builtins-8020/` (already on host).
* `EXTRA_CC_SRC=/Users/macuser/tmp/bswap_compat.c` (unchanged).
* `EXTRA_CC_HDR=/Users/macuser/tmp/bswap_compat.h` (the trimmed
  successor — no clz/ctz wrappers).
* Output: `/Users/macuser/tmp/csmith-out-069/SUMMARY.txt`.

**Result: `PASS=352 FAIL=0 SKIP=48`.**

Byte-identical to session 066's baseline
(`/Users/macuser/tmp/csmith-out-066-builtins/SUMMARY.txt`, also
`352 / 0 / 48`):

```sh
$ diff <(grep -E "^(PASS|FAIL|SKIP) " csmith-out-066-builtins/SUMMARY.txt | sort) \
       <(grep -E "^(PASS|FAIL|SKIP) " csmith-out-069/SUMMARY.txt | sort)
# (empty — same set of seeds in each bucket)
```

Same seed-by-seed result. The clz/ctz wrappers were empirically
dead weight after v0.2.49.

### Regression suite — all green

On the rebuilt imacg3 tcc:

* `scripts/run-tests2.sh`: **111 / 111** pass.
* `tcc/tests/abitest`: all subtests `success` in both the gcc-built
  (`abitest-cc`) and tcc-built (`abitest-tcc`) drivers.
* Sampled demos (all `PASS`):
  * `demos/v0.2.50-self-link-diagnostics.sh`
  * `demos/v0.2.49-clz-ctz-ub.sh`
  * `demos/v0.2.48-r12-clobber.sh`
  * `demos/v0.2.42-python.sh`

## Build-infra gotchas

Two surprises rebuilding tcc on imacg3 from scratch this session:

1. **Initial `rsync` flattened multiple source dirs into the dest
   root.** I used trailing-slash sources (`rsync -a tcc/ scripts/
   docs/ demos/ ...`), which with multiple sources copies their
   *contents* (not the dirs themselves) to the destination root.
   The fix is to drop the trailing slashes (`rsync -a tcc scripts
   docs demos ...`) so each source becomes a subdirectory on the
   destination. Mentioned because the failure mode was confusing
   (Makefile errors with no obvious cause).

2. **`./configure` on Tiger needs `--config-semlock=no`.** tcc's
   default semaphore implementation on `__APPLE__` uses
   `dispatch/dispatch.h` (GCD, introduced in 10.6 Snow Leopard);
   Tiger 10.4 doesn't have it. Build fails at
   `tcc.h::wait_sem` with `'p' undeclared` and friends. The
   `--config-semlock=no` option is the right knob; produces a
   `Config: semlock=no OSX dwarf=2 codesign new_macho=no BIGENDIAN`
   line in the configure summary. Once disabled, build proceeds
   cleanly with `gcc-4.0` and `/opt/make-4.3/bin/make`.

3. **Stale uranium build artifacts** (`c2str.exe` as Mach-O 64-bit,
   `arm64-*.o` files) interfere with a fresh imacg3 build. After
   rsync, `rm -f *.o lib/*.o libtcc.a libtcc1.a c2str.exe tcc`
   under `tcc/` before invoking `make`.

The build-infra gotchas are environmental, not tcc bugs — captured
here so the next person rebuilding tcc on a fresh imacg3 sync
doesn't burn time on them.

## Exit state

* `bswap_compat.h` lives in this session's dir as the canonical
  trimmed shim header for csmith `--builtins` campaigns.
* `builtin_compat.h` in session 062's dir is unchanged — historical
  artifact.
* tcc source unchanged. No version bump.
* On imacg3, `/Users/macuser/tmp/bswap_compat.h` is the deployed
  copy; `/Users/macuser/tmp/csmith-out-069/SUMMARY.txt` records
  the re-sweep result.
* Roadmap: line about "the `builtin_compat.h` shim still papers
  over this" in v0.2.49's section now stale and can be retired;
  session 067 open items #4 and #5 closed.
