# Session 067 — libtcc1.a __builtin_clz / __builtin_ctz match gcc-PPC (2026-05-11)

Picked up open item #4 from session 066's HANDOFF: tcc's libtcc1.a
`__builtin_clz` / `__builtin_ctz` (and the long / long-long variants)
returned different values from gcc-4.0 on PPC for the UB input
`x == 0`. The session-062 `builtin_compat.h` shim papered over this
in csmith differential-testing campaigns, but the source-level
divergence remained.

## TL;DR

* **Fix:** added `if (!x) return XX;` short-circuits in the function
  bodies of `BUILTIN(clz)` / `BUILTIN(clzll)` / `BUILTIN(ctz)` /
  `BUILTIN(ctzll)` in `tcc/lib/builtin.c`. `clrsb` deliberately
  keeps the bare `CLZI` / `CLZL` macros (`clrsb(0)` is well-defined
  per the gcc spec).

* **Values now match gcc-4.0 on PPC byte-for-byte**:
  `clz(0) = 32`, `ctz(0) = -1`, `clzll(0) = 64`, `ctzll(0) = 31`,
  `clzl(0) = 32`, `ctzl(0) = -1`.

* **Regression:** fixpoint holds, tests2 111/111, abitest cc + tcc
  all `success`, 7 release demos sampled (v0.2.45..v0.2.48, JIT,
  Python, self-link) all PASS.

* **New demo:** [`demos/v0.2.49-clz-ctz-ub.{c,sh}`](../../../demos/v0.2.49-clz-ctz-ub.sh)
  exercises 18 sub-tests (6 UB inputs, 8 defined-behavior smoke, 4
  clrsb cases) — `All PASS` on imacg3.

* **v0.2.49-g3 tag:** created locally on the `tcc/lib/builtin.c` fix
  commit; push pending user sign-off.

## What changed and why

### The divergence

Pre-fix, `tcc/lib/builtin.c` used the upstream tcc de-Bruijn fallback:

```c
#define CTZI(x) \
    return table_1_32[((x & -x) * 0x077cb531u) >> 27];
#define CLZI(x)   \
    x |= x >> 1; x |= x >> 2; x |= x >> 4; \
    x |= x >> 8; x |= x >> 16; \
    return table_2_32[(x * 0x07c4acddu) >> 27];

int BUILTIN(clz) (unsigned int x) { CLZI(x) }
int BUILTIN(ctz) (unsigned int x) { CTZI(x) }
```

When `x == 0`:
* `CTZI(0)`: `((0 & -0) * magic) >> 27 = 0`, returns `table_1_32[0] = 0`.
* `CLZI(0)`: all OR-shifts leave `x = 0`, returns `table_2_32[0] = 31`.

gcc-4.0 on PPC inlines hardware `cntlzw` for `clz`: `cntlzw(0) = 32`.
And it derives `ctz(x) = 31 - cntlzw(x & -x)`, so `ctz(0) = 31 - 32 = -1`.
For `ctzll` it uses libgcc's `__ctzdi2`, which is roughly
`lo == 0 ? 32 + ctz(hi) : ctz(lo)` — when both halves are zero that's
`32 + ctz(0) = 32 + (-1) = 31`.

So tcc and gcc diverged at `x == 0`:

| input | gcc-4.0 PPC | tcc pre-fix |
|---|---|---|
| `__builtin_clz(0)`   | 32 | 31 |
| `__builtin_clzl(0)`  | 32 | 31 |
| `__builtin_clzll(0)` | 64 | 63 |
| `__builtin_ctz(0)`   | -1 | 0  |
| `__builtin_ctzl(0)`  | -1 | 0  |
| `__builtin_ctzll(0)` | 31 | 0  |

The gcc spec marks all six as undefined behavior, so both
implementations are conformant. But csmith `--builtins` emits
`clz(0)` / `ctz(0)` patterns freely, and the differential-test
output diverged at those points. Session 062 papered over this
with `docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h`,
a UB-wrapper header `-include`d on both gcc and tcc compile lines
that routes `__builtin_clz(x)` through `__compat_clz(x)` =
`x ? __builtin_clz(x) : 32` — making both compilers return the
gcc-PPC value for the UB case.

This session moves that UB-normalization from the shim into
`libtcc1.a` itself, so it Just Works without a `-include`.

### The fix

```diff
-int BUILTIN(clz) (unsigned int x) { CLZI(x) }
-int BUILTIN(clzll) (unsigned long long x) { CLZL(x) }
+int BUILTIN(clz) (unsigned int x) { if (!x) return 32; CLZI(x) }
+int BUILTIN(clzll) (unsigned long long x) { if (!x) return 64; CLZL(x) }
...
-int BUILTIN(ctz) (unsigned int x) { CTZI(x) }
-int BUILTIN(ctzll) (unsigned long long x) { CTZL(x) }
+int BUILTIN(ctz) (unsigned int x) { if (!x) return -1; CTZI(x) }
+int BUILTIN(ctzll) (unsigned long long x) { if (!x) return 31; CTZL(x) }
```

The `clzl` / `ctzl` variants are `__attribute__((alias))`es to the
32-bit functions on PPC (since `__SIZEOF_LONG__ == 4`), so they
inherit the new behavior automatically.

### Why not touch the macros directly

`CLZI` / `CLZL` are also used inside `clrsb` / `clrsbll`:

```c
int BUILTIN(clrsb) (int x) { if (x < 0) x = ~x; x <<= 1; CLZI(x) }
```

For `clrsb`, the input `x == 0` is **not** undefined behavior — the
gcc spec defines `clrsb(0) = 31` (the MSB is the sign bit and the
remaining 31 bits are identical to it). The path `x = 0; x <<= 1 = 0;
CLZI(0)` happens to produce exactly that (`table_2_32[0] = 31`). If I
had short-circuited the macro itself to return 32 instead, `clrsb(0)`
would have become 32 — wrong. Keeping the short-circuit in the
function body leaves the macro pure and clrsb untouched.

## Verification

### Direct values

The new `demos/v0.2.49-clz-ctz-ub.{c,sh}` calls all six builtins on
`x == 0`, plus eight non-zero smoke values, plus four clrsb cases.
Run on imacg3 against the post-fix tcc:

```
PASS __builtin_clz  (0)            : 32
PASS __builtin_clzl (0)            : 32
PASS __builtin_clzll(0)            : 64
PASS __builtin_ctz  (0)            : -1
PASS __builtin_ctzl (0)            : -1
PASS __builtin_ctzll(0)            : 31
PASS __builtin_clz  (1)            : 31
PASS __builtin_clz  (0xFFFFFFFF)   : 0
PASS __builtin_ctz  (1)            : 0
PASS __builtin_ctz  (0x80000000)   : 31
PASS __builtin_clzll(1)            : 63
PASS __builtin_clzll(~0ULL)        : 0
PASS __builtin_ctzll(1)            : 0
PASS __builtin_ctzll(1ULL << 63)   : 63
PASS __builtin_clrsb  (0)          : 31
PASS __builtin_clrsbll(0)          : 63
PASS __builtin_clrsb  (-1)         : 31
PASS __builtin_clrsbll(-1LL)       : 63
All PASS
```

The same 18 lines come out of `/usr/bin/gcc-4.0 -O0` byte-for-byte
(verified directly via a probe program earlier in the session).

### Regression suite

* **Bootstrap fixpoint:** `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh`
  → `FIXPOINT HOLDS: tcc-self2.o == tcc-self3.o`.

* **tests2:** `./scripts/run-tests2.sh` → `Total: 111  Pass: 111
  Fail: 0  (100.0% pass)`.

* **abitest:** `cd tcc/tests && /opt/make-4.3/bin/make abitest`. All
  cc-side and tcc-side sub-tests print `success`, including the
  recent `ret_*` / `stdarg_*` / `arg_align_test` / `many_struct_test*`
  cases (24/24 effective).

* **Release demos sampled** (post-fix tcc, on imacg3):
  * `v0.2.45-be-bitfield-abi.sh` — PASS
  * `v0.2.46-float-int-const-fold.sh` — PASS
  * `v0.2.47-fp-arg-shadow.sh` — PASS
  * `v0.2.48-r12-clobber.sh` — PASS (`checksum = 76F5DB56`)
  * `v0.2.27-jit-heisenbug.sh` — 30-iteration JIT loop OK
  * `v0.2.42-python.sh` — PASS (Python 2.7.18 end-to-end)
  * `s025-self-link.sh` — works (`hello from tcc-built and tcc-linked program`)

## Build infrastructure gotcha worth recording

`tcc/lib/Makefile` uses GNU Make's `$(or ...)` function, added in
**make 3.81**. imacg3's `/usr/bin/make` is **3.80** — `$(or ...)`
silently evaluates to empty, which makes `T` empty, which makes
`OBJ-libtcc1` empty, which makes `libtcc1.a` build as an empty
archive (8 bytes, only the `!<arch>\n` magic). The build prints
just `../tcc -ar rcs ../libtcc1.a` and exits successfully — no
warning.

`/opt/make-4.3/bin/make` (installed via `tiger.sh make-4.3`) is the
right binary on imacg3 for any libtcc1.a rebuild:

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc/tcc && \
            /opt/make-4.3/bin/make libtcc1.a'
```

This matches the documented incantation in `host_ibookg38.md` /
`host_ibookg37.md`. Worth a heads-up because the failure mode is
"silently empty archive → `dyld: Symbol not found: ___builtin_clz`"
on the first link against the broken libtcc1.a.

## Status of session 066's open items

| # | Item | Status |
|---|---|---|
| 1 | Push v0.2.48-g3 tag | already on origin (HANDOFF stale; landed in cleanup pass) |
| 2 | v0.2.48 demo | landed in session 066 |
| 3 | Sibling r11 watch | unchanged (no surface yet) |
| 4 | **libtcc1.a clz/ctz to match gcc-PPC** | **landed (this session)** |
| 5 | **csmith building on a PPC host** | **landed (post-session cleanup, 2026-05-12).** See "Post-session addendum" below. |
| 6 | ibookg38 — back online or written off | **written off** (bad hardware; memory updated) |
| 7 | OSO STAB / self-link / AltiVec / bcheck | unchanged |

## Files

* `tcc/lib/builtin.c` — four `if (!x) return XX;` lines plus an
  explanatory comment above each pair. `clrsb` untouched.
* `demos/v0.2.49-clz-ctz-ub.c` — 18 sub-test program.
* `demos/v0.2.49-clz-ctz-ub.sh` — runner that invokes the post-fix
  tcc and asserts `All PASS`.
* `demos/README.md` — table row added.
* `docs/roadmap.md` — version line bumped to v0.2.49-g3, new row at
  the top of the reverse-chrono section.

Memory updates (uranium-side):
* `host_ibookg38.md` — marked **WRITTEN OFF** (bad hardware).
* `host_ibookg37.md` — initially narrowed to "uranium is the only
  csmith binary"; subsequently updated by the post-session addendum
  below to reflect the new in-fleet build.
* `MEMORY.md` — both index entries updated to match.

## Post-session addendum (2026-05-12) — open item #5 closed

After the session shipped, ibookg38 came back up briefly. A targeted
disk dump (`/Users/macuser/tmp/` only — that's all that was reachable
before the host fell off again) recovered the campaign script, the
runtime headers, and seven seed corpora, but **no csmith binary** —
likely it lived in `/tmp/` and was reboot-wiped, or in `/Users/macuser/`
and the dump didn't reach it. Probes against ibookg38 while it was up
(`which csmith`, all-of-disk `find`, shell history) all came up empty.

Decision: stop trying to recover and rebuild. Wrote
[`scripts/build-csmith-on-ppc.sh`](../../../scripts/build-csmith-on-ppc.sh)
— a one-shot bash script that downloads csmith-2.3.0 from GitHub
(sha256-verified against the Homebrew formula on uranium), builds
with `/opt/gcc-4.9.4` + `/opt/make-4.3`, and installs to
`$HOME/csmith-2.3.0`. Run on ibookg37; built cleanly in ~15 minutes;
final binary at `/Users/macuser/csmith-2.3.0/bin/csmith` reports
`csmith 2.3.0` (git `30dccd73b`).

End-to-end verification: `csmith --seed 1` on ibookg37 produces a
1607-line program; both gcc-4.0 and the post-067 tcc compile and
run it, both produce `checksum = 69F30756`, byte-for-byte agreement.

The build script's structure is recorded in the script itself
(header comment, ~200 lines). Three notes worth surfacing:

* `/opt/gcc-4.9.4`'s binaries are suffixed `-4.9` (i.e. `gcc-4.9`,
  `g++-4.9`), not unsuffixed. The script sets `CC` / `CXX` explicitly.
* csmith's release tarball includes a pre-generated `./configure`,
  so no autotools (autogen.sh) regeneration is needed.
* The resulting binary dynamically links `/opt/gcc-4.9.4/lib/
  libstdc++.6.dylib`; to copy the binary to a sibling G3 host the
  libstdc++ must come along too (or rebuild via the script).

Memory updates (post-addendum):
* `host_ibookg37.md` — re-flowed: csmith binary path captured, dylib
  deps recorded, "first PPC host with native csmith" noted.
* `MEMORY.md` — ibookg37 line updated to reflect the new binary.

This closes session 066/067's carried open item #5 — uranium is no
longer the only csmith host in the fleet.
