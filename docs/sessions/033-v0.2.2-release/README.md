# 033 — v0.2.2-g3 release

## Result

✅ Shipped a small follow-up to v0.2.1 with three more bug fixes.

| Fix | Effect |
|---|---|
| `__floatdidf` / `__floatdisf` / `__floatundisf` added to `lib-ppc.c` | `(float/double)long_long` conversions now have their helpers; tests using these stop crashing with `dyld: Symbol not found: ___floatdidf` |
| Auto-link `libtcc1.a` on `tcc -o exe` for PPC | User code that needs libgcc helpers no longer requires explicitly passing `libtcc1.a` on the command line |
| `bootstrap-tcc-self.sh` drops the explicit `$LIBTCC1` from the link line | Avoids the new auto-link's "defined twice" — bootstrap fixpoint chain still passes |

## tests2 baseline

* v0.2.1-g3: 75 / 122 (61.5%)
* v0.2.2-g3: **77 / 122 (63.1%)**

Two newly-passing: `91_ptr_longlong_arith32` (newly broken — see below)
and `110_average` — wait, the new pass is `49_bracket_evaluation` and
`110_average`, both fixed by the helpers + auto-link.

The canonical `tcc-self → tcc-self2 → tcc-self3` byte-identical
fixpoint still holds.

## Remaining roadmap items

Same as before — these are the bigger remaining items, each of
which is significant work and not in scope for v0.2.x patch
releases:

* **Struct-by-value parameters / returns** — biggest single
  category of remaining failures (~7 tests + sqlite3). Real PPC
  ABI work, ~200-500 lines.
* **Bound checking (`-bcheck`) for PPC** — bcheck.o not yet
  wired into `OBJ-ppc-osx`; bound-instrumentation codegen not
  implemented.
* **Pre-existing 32-byte fixpoint regression** vs gcc-built tcc.o
  — partially understood (031), partially fixed.
* Various per-test small failures (bitfields, alignas, output
  diffs, etc.).

## Files touched (since v0.2.1)

| Commit | What |
|---|---|
| `82f12ff` | `__floatdidf`, `__floatdisf`, `__floatundisf`; auto-link libtcc1.a |
