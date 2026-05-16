# Session 084 — `tcc -run` JBSR coverage probe (083 HANDOFF #6)

**Started:** 2026-05-15
**Arrival HEAD:** `7a55377` (end of session 083, v0.2.62-g3 + HANDOFF).
**Exit HEAD:** unchanged — investigation-only; no source change.
**Question answered:** does the v0.2.62 JBSR fix benefit `-run` mode,
and is there any case in the current test surface where a
JBSR-routed call appears under `-run`?

## TL;DR

**Yes, transparently.** `macho_translate_relocs` is shared between
the OBJ / EXE / RUN code paths and the JBSR arm (lines 7267–7283 of
[`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c)) does **not** gate on
`s1->output_type`. Under `-run`, the emitted `R_PPC_REL24` against
the resolved UNDEF extern is handled by the JIT's existing
`build_got_entries` + `relocate()` path — identical to an ordinary
`bl extern`.

**No naturally-occurring JBSR exists in the current `-run` test
surface.** libtcc1's four PPC objects (`alloca-ppc.o`,
`atomic-ppc.o`, `builtin.o`, `lib-ppc.o`) contain zero JBSR relocs;
tcc's own codegen never emits JBSR either (uses its own stub
machinery for far calls); `gcc-4.0` doesn't emit JBSR for ordinary
calls (it emits BR24 against `__picsymbolstub1`); and `-run` doesn't
auto-link `crt1.o`. JBSR only appears when a user explicitly passes
an Apple-CRT-style `.o` to `tcc -run` (or hand-assembles one).

**The fix's reach under `-run` is demonstrable** by passing
`/usr/lib/crt1.o` explicitly as an input — `tcc -vv -run /usr/lib/crt1.o
hello.c`. Pre-fix: the same 4 "non-extern stub-slot resolve failed"
lines that session 083 surfaced under EXE mode. Post-fix: lines gone.
Identical diagnostic profile to EXE mode (5 section-skips + 2
wholesale drops). Execution itself fails downstream (crt1.o defines
`_start` which conflicts with `-run`'s in-process startup), but the
**reader phase** is what the fix lives in — and the reader phase
runs and behaves identically across modes.

**Conclusion: HANDOFF #6 closed.** No code change. No demo needed
(the v0.2.62 EXE-mode demo and its hello-world `-vv` verification
fully exercise the fix; `-run` shares the same reader).

## Investigation

### 1. Codepath: `macho_translate_relocs` is shared OBJ/EXE/RUN

[`tcc/libtcc.c:1141`](../../../tcc/libtcc.c) routes a Mach-O `.o`
input to `macho_load_object_file` regardless of `s1->output_type`:

```c
case AFF_BINTYPE_MACHO_REL:
    ret = macho_load_object_file(s1, fd, 0);
    break;
```

`macho_load_object_file` calls `macho_translate_relocs` for each
section, again without conditioning on `output_type`. The JBSR arm
(lines 7267–7283 in [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c))
does not gate on output type:

```c
} else if (r_type == PPC_RELOC_JBSR) {
    pair_w0 = (k + 1 < sec->nreloc) ? mach_get32(r + 8) : 0;
    target_value = pair_w0;
}
```

The scattered-SECTDIFF arm at line 7035 *does* gate on output type
(drops for non-OBJ-and-non-EXE because `R_PPC_HA16_PIC` / `LO16_PIC`
have no defined translation under `-run`) — but that arm is
unrelated to JBSR.

The emitted reloc type for JBSR is `R_PPC_REL24` (shared with BR24
at line 7392), which the in-memory relocator at
[`tcc/ppc-link.c:242`](../../../tcc/ppc-link.c) handles without any
output-type gate. If the target extern is far (>32 MB from the JIT
region — common since libSystem ends up many MB away), the existing
`build_got_entries` path synthesizes a PLT trampoline near the JIT
code and rewrites the reloc; same path that handles ordinary
extern calls under `-run`.

### 2. `ST_PPC_NEEDS_STUB` tag is harmless under `-run`

The JBSR fix path sets `ST_PPC_NEEDS_STUB` on the resolved extern
(line 7341, when the source section is `S_SYMBOL_STUBS`). That tag
is consumed only by `collect_extern_stubs()` at line 522, which is
invoked from the EXE / OBJ / DLL writer paths (lines 2780, 4674,
5954) — never from `-run` (TCC_OUTPUT_MEMORY uses
[`tcc/tccrun.c`](../../../tcc/tccrun.c)'s path and bypasses the
Mach-O writers entirely). Setting the tag under `-run` is a no-op:
the symbol still resolves the same way through `dlsym(libSystem)`
or `tcc_add_symbol`-registered callbacks.

### 3. No naturally-occurring JBSR in `-run`'s actual surface

Probed every realistic source of JBSR for `-run`:

* **libtcc1's PPC objects** —
  `otool -rv` on each of `alloca-ppc.o`, `atomic-ppc.o`,
  `builtin.o`, `lib-ppc.o` (built on imacg3 by the in-tree
  Makefile): zero JBSR relocs. All branches use BR24, all data
  references use HA16/LO16 with PAIR. Confirms HANDOFF #6's "no
  JBSR appears in the in-memory test surface" claim.

* **tcc's own codegen** — tcc emits direct branches and stubs
  managed by its own writer machinery (`__picsymbolstub1` /
  `__nl_symbol_ptr`). It never emits a JBSR reloc itself.

* **gcc-4.0 output** — a probe (`gcc-4.0 -c hello.c -o hello.o`
  with `printf` and an internal helper call) shows the Apple
  compiler/assembler combo emits `BR24` against `__picsymbolstub1`
  for the printf call and `BR24` for the internal call. JBSR is
  not used for ordinary C-compiled code at typical scale.

* **Apple's `as` directly** — accepts the `jbsr` pseudo-op
  (`jbsr _exit, 0` produces an `extern=True` JBSR + PAIR reloc).
  But **extern** JBSR has always worked under tcc's reader — the
  `if (r_extern)` arm at line 7241 falls through to the standard
  `target_old_sym = r_symnum` path, ELF-maps JBSR→REL24, advances
  past the PAIR via the `k++` at line 7412. The v0.2.62 fix
  exclusively addressed **non-extern** JBSR (the kind crt1.o has,
  produced by Apple's linker when binding a JBSR call to a
  section-local stub at static-link time).

JBSR-with-non-extern-target therefore appears almost exclusively
in Apple's pre-built CRT objects (`crt1.o`, `dylib1.o`, perhaps
some `gcrt1.o` variants) and possibly in hand-rolled CRT-style
assembly. `-run` does not auto-link any of these.

### 4. Forced repro: `tcc -vv -run /usr/lib/crt1.o hello.c`

To demonstrate the fix's reach under `-run`, pass crt1.o as an
explicit input. The reader phase will process it (calling
`macho_load_object_file` → `macho_translate_relocs`), exercising
the JBSR arm. Execution will fail downstream (crt1.o defines
`_start` and a different libSystem-glue path that conflicts with
`-run`'s in-process startup), but the reader diagnostics fire
before that.

On imacg3, after temporarily reverting the JBSR arm to the pre-fix
state (single-line edit collapsing `} else if (r_type == BR24) {`
back to `} else if (r_type == BR24 || r_type == JBSR) {` so the
JBSR arm becomes dead code) and rebuilding:

```
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub (sect 1, ...)
tcc: reader: crt1.o: section-skip: __TEXT,__picsymbol_stub (sect 2, ...)
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub1 (sect 3, ...)
tcc: reader: crt1.o: section-skip: __DATA,__nl_symbol_ptr (sect 6, ...)
tcc: reader: crt1.o: section-skip: __DATA,__la_symbol_ptr (sect 7, ...)
tcc: reader: crt1.o: reloc-drop: section __text reloc 21: non-extern stub-slot resolve failed (target sect 3, value 0x330)
tcc: reader: crt1.o: reloc-drop: section __text reloc 23: non-extern stub-slot resolve failed (target sect 3, value 0x340)
tcc: reader: crt1.o: reloc-drop: section __text reloc 29: non-extern stub-slot resolve failed (target sect 3, value 0x350)
tcc: reader: crt1.o: reloc-drop: section __text reloc 85: non-extern stub-slot resolve failed (target sect 3, value 0x370)
tcc: reader: crt1.o: reloc-drop: section __TEXT,__symbol_stub1 skipped: 16 relocs dropped wholesale
tcc: reader: crt1.o: reloc-drop: section __DATA,__la_symbol_ptr skipped: 4 relocs dropped wholesale
tcc: error: file 'libtcc1.a' not found
```

Identical to the pre-v0.2.62 EXE-mode output reported in session
083 (same 4 target-sect-3 / value-0x330/0x340/0x350/0x370 lines,
same reloc indices 21 / 23 / 29 / 85).

After restoring the post-fix file and rebuilding:

```
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub (sect 1, ...)
tcc: reader: crt1.o: section-skip: __TEXT,__picsymbol_stub (sect 2, ...)
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub1 (sect 3, ...)
tcc: reader: crt1.o: section-skip: __DATA,__nl_symbol_ptr (sect 6, ...)
tcc: reader: crt1.o: section-skip: __DATA,__la_symbol_ptr (sect 7, ...)
tcc: reader: crt1.o: reloc-drop: section __TEXT,__symbol_stub1 skipped: 16 relocs dropped wholesale
tcc: reader: crt1.o: reloc-drop: section __DATA,__la_symbol_ptr skipped: 4 relocs dropped wholesale
```

The 4 stub-slot resolve failures are gone — identical to the
post-v0.2.62 EXE-mode output. **The fix applies to `-run`
identically.**

(The downstream `tcc: error: file 'libtcc1.a' not found` is
incidental to this probe — it happens after the reader phase and
reflects that `-run` looks for libtcc1.a in a different path than
the EXE driver. Doesn't affect the reader-stage observation.)

### 5. tests2 regression sanity after the toggle

After restoring and rebuilding the post-fix binary:

```
$ bash scripts/run-tests2.sh
Total: 111  Pass: 111  Fail: 0  (100.0% pass)
```

No drift from the toggle-and-restore sequence (md5 of
`tcc/ppc-macho.c` on imacg3 matches uranium HEAD byte-for-byte
after the restore).

## Findings worth remembering

### `-run`'s `.o`-loading surface goes through `macho_load_object_file`

Per [`tcc/libtcc.c:1141`](../../../tcc/libtcc.c), any `.o` passed
to `tcc -run` enters the same reader as the EXE path. Changes in
`macho_translate_relocs` automatically apply to `-run` unless the
arm explicitly gates on `output_type` (the way the scattered-SECTDIFF
arm at line 7035 does). Future reader-side fixes should consider
this default-shared property — opt-in, not opt-out.

### `ST_PPC_NEEDS_STUB` tag is a writer-only signal

The tag is set by the reader but consumed exclusively by
`collect_extern_stubs()`, which writer paths call. Under `-run`,
setting the tag is harmless dead state. No need to gate the tag
setter on output_type.

### JBSR is rare in practice on Tiger PPC

Apple's compiler/assembler emits BR24 for ordinary C-compiled
calls (even cross-section ones — they go through
`__picsymbolstub1` BR24). JBSR is reserved for Apple's CRT
objects and hand-written assembly with explicit `jbsr` pseudo-ops.
A user who doesn't link Apple CRT objects (and `tcc -run` doesn't
auto-link them) will never see JBSR in their reader output —
explaining why v0.2.61's `-vv` diagnostic was clean on every
prior link until hello-world+crt1.o made it visible.

### Extern vs. non-extern JBSR diverge in the reader

Two paths through `macho_translate_relocs`:
* **r_extern=1** (line 7241): pass through to `target_old_sym =
  r_symnum`, ELF-map to REL24, consume PAIR via `k++`. Works for
  any JBSR-with-extern-target. **Always worked.**
* **r_extern=0** (line 7244): need to derive target VA from the
  reloc data. **v0.2.62's fix lives here** — the BR24 displacement
  is wrong (it points at a local thunk); the PAIR's full 32-bit
  `r_address` is the truth.

The fix only changes non-extern JBSR. Hand-written `jbsr _foo, 0`
assembly typically produces extern JBSR and would not exercise
the fix.

## What landed

Nothing committed. The session was investigation-only. No new tag,
no source change.

The toggle-and-restore on imacg3 left the post-fix file in place
(md5-verified). Files modified on imacg3 only (no uranium
modifications), and they're all back to the post-fix state.

## Docs

* This README.
* HANDOFF.md.

No roadmap update (v0.2.62 row already accounts for the fix; this
session is purely a probe of an open question).

## Status of session 083's open items

| # | Item | Status |
|---|---|---|
| (083 #1, carried 067 #3) | Sibling r11 watch | unchanged |
| (083 #2, carried) | AltiVec intrinsics | unchanged |
| (083 #3, carried) | bcheck.c port | unchanged |
| (083 #4, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (083 #5, optional, carried) | `rsync` exclude-list polish | unchanged |
| (083 #6, new from 083) | Eight `tcc -run` mode: does it also need this fix? | **closed this session** — answered "yes, transparently; no naturally-occurring JBSR in the current -run surface; demo not warranted" |

## Open work for next session

### 1. Sibling r11 watch (carried, 067 #3)

Carried forward from session 067. No change.

### 2. AltiVec intrinsics (carried)

Multi-session lift. Carried forward.

### 3. `bcheck.c` port (carried)

Multi-session lift. Carried forward.

### 4. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Still safe to drop — fourteen+ rebuild
cycles now (added 084's two rebuilds for the toggle/restore).

### 5. (optional, low priority) `rsync` exclude-list polish

Same as session 083 — sidestepped this session entirely (no
uranium-side source edits to sync; all toggling happened in-place
on imacg3 via perl -i, then restored from imacg3's own backup of
the post-fix file at `/tmp/ppc-macho.c.v062`).
