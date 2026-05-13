# Handoff — end of session 068 (2026-05-12)

## TL;DR

Closed roadmap item #7 ("Self-link diagnostics"). The Mach-O EXE
writer (`tcc/ppc-macho.c::macho_output_exe`) now runs four
pre-write sanity checks that turn the session-025 class of cryptic
runtime dyld errors into informative tcc-level diagnostics:

| invariant | what it catches |
|---|---|
| (a) **VM layout** — segments page-aligned, no overlap, `vmsize >= filesize`, `__LINKEDIT` placed using vmsize not filesize | session-025 "Cannot allocate memory" from dyld |
| (b) `__mh_execute_header` is `SHN_ABS` at `__TEXT` base + `entry_addr` inside `__text` | session-025 "Symbol not found: __mh_execute_header" |
| (c) Stub VAs inside `__symbol_stub1`, slot VAs inside `__nl_symbol_ptr`, every `ST_PPC_NEEDS_STUB` has a stub | session-025 SIGBUS during crt1 startup |
| (d) Every defined symbol's section is in the EXE writer's emitted section list | session-025 "common symbol in `.bss` but `__bss` not emitted" |

Each check verified by hand-injecting a deliberate break, rebuilding,
observing the resulting tcc error, then reverting. The verification
log lives in the [session README](README.md).

* **HEAD at session start:** `df31e67` (end of session 067).
* **HEAD at session end:** TBD on commit.
* **v0.2.50-g3 tag:** not yet pushed — needs user sign-off (per
  the user-driven push protocol established in 067).

**Regression suite:** all green.
* tests2: 111/111.
* abitest cc + tcc: all `success`.
* Sampled demos: `v0.2.49`, `v0.2.48`, `s025-self-link`,
  `v0.2.42-python` all PASS.

**New demo:** `demos/v0.2.50-self-link-diagnostics.{c,sh}` — happy-
path program that touches every emitted section class (text /
rodata / data / bss / libSystem stubs / function-pointer init) so
the checks walk every code path on a normal compile. Prints
`rodata works | data=42 | bss=0 33 77 | p=ok`.

## What landed

* `tcc/ppc-macho.c` — single sanity-check block inserted in
  `macho_output_exe` between the __DWARF layout step and the
  Mach-header serialize step. Linear walks; happy-path cost
  <1 ms per link. No mutations of emitter state; pure
  read-and-check.

* `demos/v0.2.50-self-link-diagnostics.{c,sh}` — runnable
  milestone demo.

* `demos/README.md` — table row for v0.2.50.

* `docs/roadmap.md`:
  * Header bumped to v0.2.50-g3, "twenty-nine patch releases".
  * v0.2.50-g3 row added at the top of the reverse-chrono table.
  * Structural item #7 moved to the ~~struck-through~~ done set.

* `docs/sessions/068-self-link-diagnostics-2026-05-12/`:
  * `README.md` — narrative + invariant catalog + verification log.
  * `HANDOFF.md` — this file.

## Why this is a release

A regression-prevention release rather than a new-feature release,
but a real one: every future session that touches the Mach-O EXE
writer now has a safety net for the four invariants that took
session 025 a week to debug. Bumping the version makes the demo
naming consistent and lets the roadmap row capture the change.

## Status of session 067's open items

| # | Item | Status |
|---|---|---|
| 3 | Sibling r11 watch | unchanged (no surface yet) |
| 4 (optional) | Trim or retire `builtin_compat.h` clz/ctz wrappers | unchanged |
| 5 (optional) | Real csmith `--builtins` campaign without the shim | unchanged |
| **roadmap #7** | **Self-link diagnostics** | **landed (this session)** |

## Open work for next session

### 1. Push v0.2.50-g3 tag

Pending user sign-off. The tag should point at the source-level
commit (`tcc/ppc-macho.c` change), not the docs commit — same
convention as v0.2.49-g3 in session 067.

### 2. (optional) Extend the same checks to `macho_output_dylib`

Same shape of writer, different fixed points (no `__PAGEZERO`, no
`__mh_execute_header`, vmaddr base = `0x40000000`, no `entry_addr`,
function stubs are PIC-bigger-than-EXE). Probably 50-100 LOC
mirrored from the EXE block. Worth doing in a follow-up if anyone
hits a dylib-write regression; not blocking.

### 3. (optional) Post-write linter

A small `otool`/`nm`-driven check that reopens the just-written
file and verifies the on-disk Mach header matches the in-memory
layout we computed. Different class of bug (`obuf` corruption,
byteorder regressions, command-size mismatches). Useful but
distinct from "is the design correct" — the existing checks
verify the design; a linter would verify the **emission**.

### 4. (optional) Diagnostic mode for the reader's silent drops

`macho_load_object_file` and `macho_translate_relocs` have a few
`continue`-on-unknown paths that silently drop relocations.
Adding a `-vv` (or env-gated) mode that emits one-line
"dropped reloc type N targeting <section>" warnings would help
diagnose future archive-loading regressions like the libgcc.a
whole-archive bug from session 025.

### 5. (carried) OSO STAB / AltiVec / bcheck

Unchanged. Each is its own multi-session arc; pick one with the
user.

## How to pick up

### Trigger one of the diagnostics deliberately

On `imacg3` (or any host with the post-068 tcc built):

```sh
# Confirm the happy path is unchanged.
./demos/v0.2.50-self-link-diagnostics.sh

# To exercise a diagnostic, hand-inject one of the breaks from
# the session 068 README (e.g. `entry_addr = 0xdeadbeef;` just
# before the `/* ---- Pre-serialize sanity checks */` block in
# `tcc/ppc-macho.c::macho_output_exe`), rebuild, and run any
# small program. tcc should error out with a specific message
# pointing at the violated invariant — and produce no output
# file.
```

### Rebuild on imacg3

```sh
ssh imacg3 '
cd /Users/macuser/tcc-darwin8-ppc/tcc &&
/opt/make-4.3/bin/make tcc &&
rm -f libtcc1.a lib/*.o &&
/opt/make-4.3/bin/make libtcc1.a
'
```

Do **not** use `/usr/bin/make` — Make 3.80 silently builds an empty
archive (see session 067 HANDOFF).

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

A small, defensively-motivated change. The four checks add up to
~200 lines of straight-line code that walk already-computed
layout data and check it against the invariants documented (often
implicitly) in surrounding comments. The benefit is preventive:
the next person to touch this code path — whether refactoring,
adding a new section type, or accidentally breaking a fixed-point
invariant — gets a useful tcc error at compile time instead of a
cryptic dyld error at run time.

The verification approach (inject a break → observe the message
→ revert) was satisfying. It would be even better as a permanent
suite, but the test harness cost is high relative to the small
number of invariants, and any *real* regression would be caught
the same way: by tests2 / abitest / the demo set failing with a
clear error.

Next session: [docs/sessions/068-self-link-diagnostics-2026-05-12/HANDOFF.md](docs/sessions/068-self-link-diagnostics-2026-05-12/HANDOFF.md)
