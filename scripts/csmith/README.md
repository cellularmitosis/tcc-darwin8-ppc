# scripts/csmith — csmith differential-testing harness

Shared test infrastructure for running [csmith](https://github.com/csmith-project/csmith)
differential-testing campaigns against tcc on PowerPC Tiger hosts
(imacg3, ibookg37, ibookg38). The pieces are reused across sessions —
moved here in session 070 so they're discoverable rather than
archaeological. See sessions 062 / 065 / 066 / 069 for the original
contexts.

## Files

| File | Purpose |
|---|---|
| [`csmith_campaign.sh`](csmith_campaign.sh) | Campaign runner. Compiles each csmith seed with gcc-4.0 and tcc, runs both, diffs `(exit, stdout)`. Writes per-seed results and a `SUMMARY.txt` of PASS / FAIL / SKIP. |
| [`bswap_compat.c`](bswap_compat.c) | Function bodies for `__builtin_bswap32`, `__builtin_bswap64`, `__builtin_ia32_crc32qi` — three builtins gcc-4.0 lacks (or treats as ordinary externs) and which csmith `--builtins` can emit. Linked into both compilers' binary so both resolve the symbol the same way. |
| [`bswap_compat.h`](bswap_compat.h) | `extern` prototypes for the three functions in `bswap_compat.c`. Injected via `-include` into both compilers. |

## Why a shim at all

gcc-4.0 (Tiger's system gcc, our reference compiler for differential
testing) doesn't recognize `__builtin_bswap32 / bswap64 / ia32_crc32qi`
as builtins; it emits an extern call to the symbol. tcc on PPC also
doesn't intrinsify these — it emits the same extern call. Providing a
single linker-resolved C body makes both compilers exercise identical
runtime behavior, isolating tcc-vs-gcc divergence to genuine codegen
issues.

The `__builtin_ia32_crc32qi` body is the canonical reflected-Castagnoli
CRC32 step. The bitwise behavior matches the x86 SSE4.2 instruction
that the builtin normally maps to, but for differential-testing
purposes any deterministic function of `(crc, data)` would suffice
— csmith just needs both compilers to agree.

## Why `bswap_compat.h` instead of `builtin_compat.h`

Session 062 originally also UB-guarded `__builtin_clz / clzl / clzll /
ctz / ctzl / ctzll` because tcc's libtcc1.a software implementation
returned different values on UB inputs (`x == 0`) than gcc-PPC's
hardware `cntlzw` + libgcc helpers. v0.2.49-g3 (session 067) fixed
that at the source: `tcc/lib/builtin.c` now returns gcc-PPC's UB
values directly. Session 069 then confirmed empirically (400 seeds,
byte-identical PASS set to the with-shim baseline) that the
clz/ctz wrappers were dead weight and retired them. The
`bswap_compat.h` you see here is the trimmed successor.

The original `builtin_compat.h` lives on as a historical artifact in
[`docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h`](../../docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h)
— don't use it for new campaigns.

## Usage

Deploy the shim to the test host and run the campaign:

```sh
# One-time: copy shim to the host's scratch dir.
scp scripts/csmith/bswap_compat.{c,h} imacg3:/Users/macuser/tmp/

# Per-campaign: invoke with EXTRA_CC_{SRC,HDR} pointing at the deployed copies.
ssh imacg3 '
  cd /Users/macuser/tmp &&
  EXTRA_CC_SRC=/Users/macuser/tmp/bswap_compat.c \
  EXTRA_CC_HDR=/Users/macuser/tmp/bswap_compat.h \
  bash csmith_campaign.sh 8020 8419 \
    /Users/macuser/tmp/csmith-builtins-8020 \
    /Users/macuser/tmp/csmith-out-070
'
```

`SRC` is the directory of pre-generated `seed-N.c` files; `OUT` is
where the script writes results. Both default to `/Users/macuser/tmp`
subdirs if positional args are omitted.

The script hardcodes `TCC_TREE=/Users/macuser/tcc-darwin8-ppc` — the
location of the working-copy tcc tree on the test host. Override by
editing the script if your tree lives elsewhere.

## See also

* [`scripts/build-csmith-on-ppc.sh`](../build-csmith-on-ppc.sh) — how
  the native csmith generator was built on ibookg37.
* Session [062](../../docs/sessions/062-bswap-shim-builtins-2026-05-10/),
  [065](../../docs/sessions/065-v0.2.48-validation-2026-05-11/),
  [066](../../docs/sessions/066-seed-732-2026-05-11/),
  [067](../../docs/sessions/067-libtcc1-clz-ctz-2026-05-11/),
  [069](../../docs/sessions/069-retire-clz-ctz-shim-2026-05-13/) —
  campaign history and per-session findings.
