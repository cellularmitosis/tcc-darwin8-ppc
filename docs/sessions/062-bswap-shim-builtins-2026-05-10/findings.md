# Findings — session 062

## Headline

The bswap shim worked — `--builtins` is now usable for csmith on Tiger
PPC. The carry-forward "default-opts past seed 1500" sweep paid off
too. The combined run surfaced **9 FAILs**, of which:

* **6 are csmith-generated UB false-positives** in tcc's libtcc1.a
  software clz/ctz impls (which differ from gcc-PPC's cntlzw on UB
  inputs). These pass cleanly with the `builtin_compat.h` UB-guard
  shim.
* **3 are real tcc bugs** that survive the UB-guard:
  * **seed-1536** — default-opts, no builtins. tcc-built binary
    SIGSEGVs at runtime.
  * **seed-8271** — builtins+bitfields. tcc-built binary SIGSEGVs at
    runtime.
  * **seed-8255** — builtins+bitfields. Output divergence, gcc says
    `A2955E73`, tcc says `64984FA0`.

Two of the three real bugs are SIGSEGVs — they may share a root cause.

## The clz/ctz UB false-positive class

### Symptom

csmith `--builtins` emits calls like `__builtin_clz((void*)0 == g_NN)`
where `g_NN` is non-NULL — i.e. `__builtin_clz(0)`, which is
explicitly undefined behavior per the gcc spec. gcc and tcc both
"handle" the UB but with different deterministic values:

| Builtin | gcc-O0 (PPC) | gcc-O2 (PPC) | tcc (libtcc1.a) | Match? |
|---|---|---|---|---|
| `clz(0)`   | 32 | 32 | 31 | NO |
| `clzl(0)`  | 32 | 32 | 31 | NO |
| `clzll(0)` | 64 | 64 | 63 | NO |
| `ctz(0)`   | -1 | -1 |  0 | NO |
| `ctzl(0)`  | -1 | -1 |  0 | NO |
| `ctzll(0)` | 31 | 31 |  0 | NO |
| `popcount(0)` | 0 | 0 | 0 | YES |
| `parity(0)`   | 0 | 0 | 0 | YES |
| `ffs(0)`      | 0 | 0 | 0 | YES |

gcc inlines `cntlzw` (which the PowerISA defines as 32 for input 0),
plus libgcc helpers for the long-long variants. tcc's
`tcc/lib/builtin.c` uses a de-Bruijn-sequence-multiply software impl
(no UB special case — `clz(0)` falls through to `table_2_32[0]` = 31).
ctz uses `x & -x` which is 0 for input 0; the de-Bruijn table maps
that to position 0.

Both compilers are within their rights per the gcc spec, but the
divergence breaks csmith differential testing because csmith doesn't
avoid UB shapes. Our oracle (gcc-O0 vs gcc-O2 agreement) flags it as
a "tcc bug" because the two gcc opt levels both inline `cntlzw` and
agree on 32.

### Fix (this session): UB-guard shim

`builtin_compat.h` provides `static inline` wrappers `__compat_clz`,
`__compat_ctz`, etc. that short-circuit `x == 0` to gcc-PPC's observed
UB-result, then macro-shadow the builtins to route through the
wrappers. Both compilers now compile the same C wrapper for clz/ctz
calls — the divergence vanishes.

The trick: define the wrappers *before* the macros so the wrapper
bodies still see the un-shadowed builtins. Subsequent uses (the
`-include`d seed source) pick up the macro and route through the
wrapper.

Verified on the 4 initial UB-shape FAILs (seed-8034, 8068, 8084,
8228) plus the 2 later ones (seed-8312, 8356). All 6 produce matching
checksums between gcc and tcc with the UB-guard in place.

### Fix (proposed for future session): patch tcc's libtcc1.a

The cleaner long-term fix is to make tcc's `tcc/lib/builtin.c` clz/ctz
impls match gcc-PPC's UB behavior — short-circuit `x == 0` to 32 (clz)
or -1 (ctz), and the long-long variants similarly. That way tcc-built
binaries behave the same as gcc-built binaries on PPC for UB inputs,
matching what real-world PPC code expects (PowerISA cntlzw semantics).
This is a 10-line patch but warrants its own session — it changes
runtime behavior of every tcc-built program that uses these builtins,
so needs a fixpoint check + tests2 + abitest run.

The shim approach in this session unblocks csmith-on-PPC differential
testing without modifying tcc — orthogonal moves.

## Real bugs (live triage)

### seed-1536 — default-opts SEGV

* Source: 615 lines, no builtins, 2 bitfield-shaped decls.
* Symptom: tcc-built binary exits 139 (SIGSEGV) at runtime; gcc-built
  binary returns checksum `19FE6CAA` cleanly.
* Reduction: in progress (`creduce`, ssh-per-iter to imacg3, single
  thread, predicate = "tcc binary signal-killed but gcc binary clean
  exit"). See `seed-1536-repro/`.
* Highest-priority bug because (a) crash > wrong-output, (b) no
  builtin involvement, (c) NOT in the new builtins arm — confirms
  the "0-effort parallel arm" carry-forward sweep was right to do.

### seed-8271 — builtins+bitfields SEGV

* Source: 1261 lines, lots of builtins.
* Symptom: tcc-built binary exits 139; gcc-built binary clean.
* Survives the UB-guard shim — not a clz/ctz UB false-positive.
* May share a root cause with seed-1536 (both SEGVs in similar
  csmith shapes). Reduction not yet started.

### seed-8255 — builtins+bitfields output divergence

* Source: 877 lines.
* Symptom: gcc=`A2955E73`, tcc=`64984FA0`. Survives UB-guard shim.
* First divergent global per `transparent_crc(..., 1)` is `g_181[i]`.
* Reduction not yet started.

## Tooling notes

### EXTRA_CC_SRC + EXTRA_CC_HDR

The `csmith_campaign.sh` harness now takes two env vars:

* `EXTRA_CC_SRC` — extra .c file linked into both binaries
  (e.g. `bswap_compat.c` providing `__builtin_bswap{32,64}` +
  `__builtin_ia32_crc32qi`).
* `EXTRA_CC_HDR` — header injected via `-include` into both compilers
  (e.g. `builtin_compat.h` UB-guarding clz/ctz).

Both default to empty for backwards compatibility with existing
campaigns. The local copy under this session dir has both wired in;
ship to imacg3 / ibookg38 once their current campaigns finish.

### creduce on Tiger via ssh-per-iteration

creduce isn't installed on imacg3/ibookg38. We run it locally on
uranium with a `test.sh` interestingness script that ships the
candidate to the remote host, compiles + runs both compilers, and
exits 0 iff the bug is preserved.

* `--n 1` is required: parallel jobs all clobber the same
  `$VARIANT_HOST_DIR/test.c` over SSH. Local-mode creduce is fine
  with `--n 4` because each job has its own working dir, but the
  *remote* dir is shared.
* Per-iteration cost is the SSH round-trip (~5-6 s on imacg3, ~4-5 s
  on ibookg38) which dominates compile/run time.
* For SIGSEGV reductions, the predicate accepts any signal exit
  (>= 128 except 124) so the reducer can shift between SIGSEGV /
  SIGBUS / SIGILL as the program shrinks.

### Per-variable hash dump for narrowing without creduce

Csmith binaries take an optional `1` arg to print the running CRC32
after each global is hashed (via `transparent_crc(..., 1)`). Diffing
gcc vs tcc per-variable output points directly at the first divergent
global, which is usually within a few statements of the bug. Useful
companion to creduce.

## Reduction artifacts

* `bswap_compat.c` — provides `__builtin_bswap32`, `__builtin_bswap64`,
  `__builtin_ia32_crc32qi` for gcc-4.0 (and tcc, for symmetry).
* `builtin_compat.h` — UB-guards `__builtin_clz/ctz/clzl/ctzl/clzll/ctzll`.
* `csmith_campaign.sh` — updated harness with `EXTRA_CC_SRC` +
  `EXTRA_CC_HDR` env vars.
* `seed-8034-repro/` — original FAIL source, creduce script,
  test.c (reduced to 64 lines before we realized 8034 was a UB
  false-positive, then killed creduce).
* `seed-1536-repro/` — original SEGV source, creduce script,
  reduction in progress targeting imacg3.
* `fails/` — original sources + run outputs for all 9 FAILs.
