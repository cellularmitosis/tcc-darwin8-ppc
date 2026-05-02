# 032 — v0.2.1-g3 release

## Result

✅ **Shipped `tcc-darwin8-ppc-v0.2.1-g3.tar.gz`** — a patch
release on top of v0.2.0-g3 that bundles real bug fixes landed
in sessions 030-032:

| Fix | Effect |
|---|---|
| `>8-arg functions` (030) | Sqlite3 now compiles 53K more lines before next blocker; unblocks any program with a function declaring more than 8 parameters |
| `TOK_PDIV` codegen (030) | Pointer-arithmetic division now works |
| `tcc/include/stddef.h` typedef guards (030) | Programs that include both `<stddef.h>` and `<sys/types.h>` no longer error on `ssize_t` redefinition |
| `vpushi → vpush64` for long-long-constant high half (031) | Const-folded 64-bit constants with bit 31 set in the low half no longer get wrongly sign-extended in gcc-built tcc |
| `macho_load_dll` becomes no-op (`-lm` etc. work) (032) | Programs that link against libm/libpthread/libdl no longer fail at compile time |
| **FP shadow for variadic calls** (032) | `printf("%f", x)`, math functions, all variadic calls with FP args now produce correct results |

## tests2 baseline

* v0.2.0-g3 (029): **70 / 122** pass (57.4%)
* v0.2.1-g3 (here): **75 / 122** pass (61.5%)

Newly passing tests:
* `22_floating_point` — needs libm via dylib loading + correct printf-with-%f
* `24_math_library` — same
* `49_bracket_evaluation` — fixed by FP shadow (the variadic call site there uses %f)
* `83_utf8_in_identifiers` — fixed by FP shadow
* `133_old_func` — fixed by FP shadow

The self-host fixpoint (`tcc-self2.o == tcc-self3.o`) still
holds — verified by `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh`.

## What's still on the roadmap

* **Struct-by-value parameters / returns** — the biggest tests2
  failure category (7+ tests). Real PPC ABI work in
  `gfunc_call`/`gfunc_prolog` (~200-500 lines). Not done; would
  unblock sqlite past line 122853 and lots of real-world C code.
* **Bound checking (-bcheck) for PPC** — large infrastructure
  work; 4 of the 47 remaining test failures need it. Not done.
* **Pre-existing 32-byte fixpoint regression** vs gcc-built tcc.o
  — partially understood and partially fixed (031); the
  underlying constant-folding bug in tcc's compiled gen_opic
  remains. Doesn't block self-host, just gcc-parity.
* The remaining ~40 test failures span various smaller issues:
  bitfields, output diffs, alignas, etc. Each is a small
  individual fix.

## Files touched (over and above v0.2.0-g3)

| Commit | What |
|---|---|
| `df110b4` | Track `tcc/conftest.c` (was incorrectly gitignored) |
| `7b59599` | Session 030 sqlite smoke writeup |
| `6958ef3` | `>8-arg functions`, `TOK_PDIV`, `stddef.h` guards |
| `e7c14f0` | Session 031 fixpoint deep-dive writeup |
| `35c7312` | Partial const-fold fix |
| `dd4acf9` | macho_load_dll no-op |
| `b359d62` | FP shadow for variadic calls |
