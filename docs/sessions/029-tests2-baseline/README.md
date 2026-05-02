# 029 — TCC tests2 baseline

## Result

**70 / 122 pass (57%)** on the first run of TCC's own `tests2/`
suite against our PPC tcc, on `ibookg37` (PowerBook G4 600MHz,
Tiger 10.4.11). Captured a baseline log and wrote a wrapper script
`scripts/run-tests2.sh` so future sessions can re-run with one
command and watch the number tick up.

## Setup

`tcc/tests/tests2/` is TCC's standard regression suite — 127
small C programs paired with `.expect` files for expected stdout.
The default Makefile uses `tcc -run` to execute each test, but
`-run` requires PLT-stub allocation (`create_plt_entry`) that
isn't implemented for our PPC backend. Solution: pass
`NORUN=true` to force the `tcc -o exe && ./exe` path, which we
*do* fully support post-027.

`scripts/run-tests2.sh`:

```sh
$ ./scripts/run-tests2.sh
==> Running tests2 (NORUN=true; force -o exe path)...
Total: 122  Pass: 70  Fail: 52  (57.4% pass)

Failed (52):
   03_struct
   17_enum
   ...
```

The full make log lands at `/tmp/tests2.log` (override with `LOG=`
env var).

## Categorized failures (52)

### Genuine codegen gaps (17 tests, ~33% of failures)

| Category | Tests |
|---|---|
| `ppc-gen: struct parameters not yet supported` | 73, 109, 121, 130, 131, 135, 137 (7) |
| `ppc-gen: VLAs not supported` | 78, 79, 114, 116, 122, 123 (6) |
| `ppc-gen: ggoto stub` | 90, 119 (2) |
| `ppc-gen: store VT_LOCAL of bt 0xb` | 88 (1) |
| `ppc-gen: bitfield codegen wrong` | 95, 95_ms (2) — output differs |

These are real backend gaps that need codegen work. Of the seven
"struct parameters" failures, fixing struct-by-value pass/return
would also unblock many real-world programs.

### EXE writer gaps (10 tests)

| Category | Tests |
|---|---|
| `ppc-macho: PIC reloc with no nlptr slot for sym 'X'` | 17_enum, 104_inline, 120_alias, 129_scopes (4) |
| `ppc-macho: dynamic library loading not yet supported (libm.dylib)` | 22_floating_point, 24_math_library (2) |
| `ppc-macho: only -c / -o exe outputs implemented` (DLL test) | 113_btdll (1) |
| `ppc-macho: _main not defined in .text` (no main but valid program) | 60_errors_and_warnings, 96_nodata_wanted, 128_run_atexit (3) |

The "no nlptr slot" cases are usually our reader missing some
extern-symbol path. The `_main not defined` ones are tests that
intentionally have no `main` (preprocessor-only or compile-error
tests).

### Bound-checking unsupported (4 tests)

`-bcheck`/`-bt` aren't wired up for PPC: 112_backtrace,
117_builtins, 126_bound_global, 132_bound_test. Documented in
roadmap as post-v0.2.0.

### Header / atomic redefinitions (3 tests)

```
/usr/include/stdint.h:78: error: incompatible redefinition of 'intptr_t'
```

Tests 124_atomic_counter, 125_atomic_misc, 136_atomic_gcc_style
include `<stdatomic.h>` which pulls in `<stdint.h>`, and the Tiger
SDK's stdint.h conflicts with TCC's own `intptr_t` typedef.
Header/include-ordering issue, not codegen.

### Output diffs without compiler errors (15 tests)

These compiled and ran cleanly but printed something different
from `.expect`:

03_struct, 23_type_coercion, 33_ternary_op, 42_function_pointer,
49_bracket_evaluation, 70_floating_point_literals,
83_utf8_in_identifiers, 91_ptr_longlong_arith32, 101_cleanup,
102_alignas, 107_stack_safe, 108_constructor, 110_average,
111_conversion, 133_old_func.

Some of these are warning-text drift (we print warnings slightly
differently than tcc's expected output expects), some are real
runtime miscompilations (e.g. 133_old_func's floats are wildly
wrong, 91_ptr_longlong_arith32 is the long-long codegen bug we
already noted). Each needs individual investigation.

## What's next

This baseline is now the regression check for any backend work.
Specific next-session opportunities:

1. **Struct-by-value parameters / returns.** Single biggest
   category of failures (7 tests) and a real-world blocker for
   many C programs. Would also help sqlite/lua smoke tests in 030.
2. **`ppc-macho: PIC reloc with no nlptr slot`** — these tests
   point at a reader-or-writer gap that's likely fixable with a
   small targeted change.
3. **The output-diff bucket** (15 tests) is mostly tractable: for
   each one, diff the actual vs expected output and either fix
   the underlying codegen bug or document the warning-text drift.

## Files added

| | |
|---|---|
| `scripts/run-tests2.sh` | one-command runner for tests2 with NORUN=true; prints summary |
| `docs/sessions/029-tests2-baseline/README.md` | this file |
| `docs/sessions/029-tests2-baseline/tests2.log` | full make log of the baseline run |
