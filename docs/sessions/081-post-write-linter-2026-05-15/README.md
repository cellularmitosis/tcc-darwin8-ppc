# Session 081 — post-write linter

**Date:** 2026-05-15
**Arrival HEAD:** `7776ce5` (end of session 080, v0.2.59-g3 + handoff).
**Goal:** add a post-write linter that re-reads every just-emitted
Mach-O exe / dylib and verifies high-level shape invariants. Carried
forward from session 068's "open work" #3 (recapped as #1 in
[session 080's HANDOFF](../080-dylib-self-link-diagnostics-2026-05-15/HANDOFF.md)).

## Why

The v0.2.50 / v0.2.59 pre-write blocks verify the writer's **design**:
arithmetic relationships among writer-state variables before any
bytes are emitted (LC_DYSYMTAB index drift, `__LINKEDIT` placed inside
`__bss`'s vmsize footprint, stub/slot mispairing). They are blind to
bugs that happen during emission:

* `obuf` corruption — a later `put32be` trampled an earlier slot
* byteorder regression — `put32be` slips to `put32le`
* cmdsize literal drift — a `put32be(&out, ...);` count parameter
  goes stale relative to the actual command's emitted extent
* truncated `fwrite` / `fclose`
* missing or duplicated load commands

The v0.2.57 strip work tripped on exactly this class of bug — the
`__LINKEDIT` interior ordering was wrong, the writer-state arithmetic
was internally consistent, but cctools `strip` rejected the emitted
bytes. A post-write linter would have caught it at link time instead
of at strip time.

## Design call: re-read in-process, not shell out

The session 080 handoff sketched the linter as "shell out to `otool -lv`
/ `nm` and parse." On reflection an in-process re-read is strictly
better:

* same semantic check (post-write, fresh parse, black-box view)
* no fork + grep fragility, no `otool` output-format drift between
  cctools versions
* one C helper shared by both writers, no platform dependency
* the bug-class we care about (in-process state divergence between
  what the writer thinks it emitted and what's actually on disk)
  is exactly what an in-process re-read with a separate code path
  catches

So the implementation is a single `static int macho_post_write_lint(
TCCState *s1, const char *filename, int is_dylib, uint32_t
expected_size, int has_externs, int n_extra_dylibs)` that

1. `stat`s the file → confirms `st.st_size == expected_size` (catches
   truncated `fwrite` / `fclose`);
2. `open` + `read`s into a fresh `tcc_malloc`'d buffer — independent
   of the writer's `out.buf`;
3. parses the Mach header field-by-field via a local `pwl_get32be`
   helper (no struct casting, no endianness shortcuts);
4. walks load commands, accumulating `n_load_dylib` / `n_id_dylib` /
   `n_symtab` / `n_dysymtab` / `n_load_dylinker` / `n_unixthread`
   counts and per-command-shape checks;
5. compares walked totals against `header.ncmds` and
   `header.sizeofcmds`;
6. compares LC_* counts against caller-derived expected values;
7. checks LC_DYSYMTAB partition arithmetic + side-table reachability
   (indirectsym / extrel / locrel offsets fit in the file).

Failure mode mirrors the pre-write blocks: every violation goes
through `tcc_error_noabort` with prefix `ppc-macho: post-write:`, so
the user sees the full picture in one link. Caller sets `ret = -1; goto
cleanup;` on a non-zero return.

## What landed

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — one new ~470-line
  helper between the previous `emit_stab_nlist` block and the
  `/* The big one. */` line preceding `macho_output_exe`. Pure
  read-and-check; no writer-state mutations. Two call sites:
  `macho_output_exe` after `chmod(filename, 0755)`,
  `macho_output_dylib` after the same. The helper computes
  expected LC_LOAD_DYLIB / LC_LOAD_DYLINKER / LC_DYSYMTAB counts
  from `is_dylib + has_externs + n_extra_dylibs` so callers don't
  duplicate the formula.

* [`demos/v0.2.60-post-write-linter.{c,sh}`](../../../demos/) — a
  happy-path demo. The `.c` is a small EXE exercising printf +
  rodata + data + bss; the `.sh` builds it and also builds an
  inline dylib with externs + a dlopen driver, so both writer call
  sites are exercised. Final line: `PASS: exe + dylib both built
  and ran; post-write linter silent on both paths`.

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped
  (`v0.2.59-g3` → `v0.2.60-g3`, `Thirty-seven` → `Thirty-eight`);
  v0.2.60-g3 row prepended.
* [`demos/README.md`](../../../demos/README.md) — v0.2.60 row
  prepended above v0.2.59.
* This README + `HANDOFF.md`.

## Verification

### Happy path

| suite | result |
|---|---|
| `demos/v0.2.60-post-write-linter.sh` | PASS — clean exe + dylib build + dlopen + run |
| `tests2` | 111 / 111 |
| `abitest` (cc + tcc) | 48 / 48 (all `success`) |
| `bootstrap-tcc-self.sh FIXPOINT=1` | FIXPOINT HOLDS |
| eight existing dylib demos (v0.2.25, v0.2.26, v0.2.29, v0.2.31, v0.2.40, v0.2.41, v0.2.42, v0.2.59) | all PASS |

### Hand-injected breaks (three diagnostic categories)

| break | injection | diagnostics that fired |
|---|---|---|
| (a) byteorder regression | `put32be(&out, MH_MAGIC)` → `put32be(&out, 0xcafebabe)` at line 3987 (EXE writer Mach header) | `tcc: error: ppc-macho: post-write: bad magic 0xcafebabe != MH_MAGIC 0xfeedface (byteorder regression?)` |
| (b) extent drift (EXE) | `put32be(&out, symtab_cmd_size)` → `put32be(&out, 999)` at line 4302 (EXE LC_SYMTAB cmdsize) | cascade — `bogus cmdsize 999 (must be >=8 and 4-aligned)`, `walked 6 commands but header.ncmds = 10`, `sum of cmdsizes 848 != header.sizeofcmds 1152`, `expected exactly 1 LC_SYMTAB, found 0`, `LC_DYSYMTAB count 0 != expected 1`, `exe expected exactly 1 LC_UNIXTHREAD, found 0` |
| (c) extent drift (dylib) | `put32be(&out, dysymtab_cmd_size)` → `put32be(&out, 999)` at line 5757 (dylib LC_DYSYMTAB cmdsize) | cascade — `bogus cmdsize 999`, `walked 7 commands but header.ncmds = 8`, `sum of cmdsizes 720 != header.sizeofcmds 800`, `LC_DYSYMTAB count 0 != expected 1` |

In each case the broken `tcc` exited non-zero and downstream tooling
treated the link as failed. Categories (b) and (c) exercise the EXE
and dylib call sites independently — the cascade shape is identical,
confirming the helper is genuinely shared.

## Findings worth remembering

### `__DWARF` segments need a `vmsize == 0` exemption

First run of `tests2` after wiring the linter blew up on
`132_bound_test` with:

```
ppc-macho: post-write: LC_SEGMENT '__DWARF' vmsize 0x0 < filesize 0x1000
ppc-macho: post-write: section '__DWARF,__stab' vm range [0x0, 0x414) outside segment vm range [0x0, 0x0)
ppc-macho: post-write: section '__DWARF,__stabstr' vm range [0x0, 0x427) outside segment vm range [0x0, 0x0)
```

The `__DWARF` segment is intentionally `vmaddr=0 vmsize=0` —
file-only, never mapped into VM (dsymutil reads it from disk; dyld
ignores it). The pre-write block already handles this exception
(see the comment at `ppc-macho.c:3151-3159`). I had to mirror the
same shape in the linter:

* the `vmsize < filesize` check is gated on `seg_vmsize > 0`
* the per-section VM-range check is gated on `seg_vmsize > 0`

The on-disk file-range check still applies — `__DWARF` sections do
have file presence and must lie within the segment's file extent.

### `set -e` plus a known-failing `tcc` invocation kills cleanup

The first hand-injection script used:

```sh
set -e
cp ppc-macho.c /tmp/bak
perl -i -pe '...' ppc-macho.c
bash scripts/build-tiger.sh
./tcc/tcc /tmp/x.c -o /tmp/x   # <-- expected to fail
echo "EXIT: $?"
cp /tmp/bak ppc-macho.c        # <-- never reached
```

`set -e` aborted the script the moment `tcc` exited non-zero (which
was the whole point of the test), so the restore step never ran.
Without it, the next injection's `cp ppc-macho.c /tmp/bak` backed up
the already-corrupted source. Lesson: for "this command should fail"
test loops, either drop `set -e`, suffix the test command with `|| true`,
or run the restore in a `trap EXIT`.

### Restoring source ≠ restoring the build

Even after the source was restored, the on-disk `tcc` binary still
had the previous injection's break compiled in. The v0.2.60 demo
failed the dylib half because the *compiled tcc* was still emitting
a broken dylib, not because the *source* was wrong. After every
injection cycle, rebuild before testing happy-path behavior. A
related corollary for the demo workflow: when an injection round
ends, do `cp /tmp/bak source; bash build-tiger.sh` together, not just
`cp`.

### LC_LOAD_DYLIB count formula differs slightly between writers

For the EXE writer: when `nstubs == 0 && n_nlptrs == 0`, **zero**
LC_LOAD_DYLIB commands are emitted (libSystem block AND the per-
extra-dll loop are both behind the same `(nstubs > 0 || n_nlptrs > 0)`
gate). For the dylib writer: the libSystem block is gated, but the
extra-dll loop is unconditional — so a dylib that loads zero externs
but `-l`'d some user dylib will emit just the user LC_LOAD_DYLIBs,
no libSystem entry. The linter takes `has_externs` and
`n_extra_dylibs` from the call site and computes the expected count
internally to keep this asymmetry in one place.

## Next session

[HANDOFF.md](HANDOFF.md)
