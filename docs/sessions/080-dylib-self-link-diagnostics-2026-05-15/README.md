# Session 080 — dylib self-link diagnostics

**Date:** 2026-05-15
**Arrival HEAD:** `6ff2879` (end of session 079, v0.2.58-g3 + handoff).
**Goal:** mirror the four pre-write sanity-check invariants from
`macho_output_exe` into `macho_output_dylib`. Carried forward from
session 068's "open work for next session" #2.

## Why

Session 068 added a four-invariant pre-write sanity-check block to
`tcc/ppc-macho.c::macho_output_exe`. Each check turns a class of
historically-cryptic dyld failures into a tcc-level diagnostic at link
time. The parallel `macho_output_dylib` writer never got the same
treatment — a dylib gone wrong (LC_DYSYMTAB miscounting, stub/slot
mispairing, common-symbol-in-missing-bss, PIC layout drift) currently
surfaces only as `dlopen("…") returned NULL` with no useful context.

The dylib writer is structurally similar enough that the same four
invariants port across with small adjustments for dylib-specific fixed
points.

## Plan

### Insertion point

`macho_output_dylib` (starts at `tcc/ppc-macho.c:4016`) does its
own inline serialization (no `macho_write` call). All layout
variables are settled by the end of the `Compute __LINKEDIT layout`
block (around line 4681). The natural insertion point is right
after that block and before `Serialize: Mach header` at line 4683.

### Adjustments per invariant

| invariant | EXE form | dylib adjustment |
|---|---|---|
| (a) VM layout | `text_seg_vmaddr == EXE_TEXT_VMADDR_BASE`, page-align, no overlap, vmsize ≥ filesize, `__LINKEDIT` placed via vmsize, `__DWARF` sandwich | base = `DYLIB_TEXT_VMADDR_BASE` (0x40000000), no `__PAGEZERO`, **no `__DWARF`** in dylib path (drop the dwarf-sandwich check), `text_seg_vmsize == text_seg_filesize` always for dylibs (the check is trivial but cheap to keep for symmetry) |
| (b) Required symbols | `__mh_execute_header` SHN_ABS at `__TEXT` base; `entry_addr` inside `__text` | `__mh_dylib_header` SHN_ABS at `__TEXT` base; **no `entry_addr` check** (dylibs have no entry point — drop it) |
| (c) Stub/slot wiring | stub addrs inside `__symbol_stub1` (16-byte stubs), slot addrs inside `__nl_symbol_ptr`, every `ST_PPC_NEEDS_STUB` UNDEF has a stub | identical, but dylib stubs are **PIC, 32 bytes** (8 instructions); use `32u` instead of `16u` |
| (d) Section-presence | every defined symbol's section is in `sects[]` | identical (no adjustment — pattern transfers) |

### Available variables at insertion point

Confirmed by reading `macho_output_dylib`:

* layout: `text_seg_vmaddr`, `text_sect_vmaddr`, `text_data_size`,
  `text_seg_filesize`, `text_seg_vmsize`, `data_seg_vmaddr`,
  `data_seg_file_off`, `data_seg_filesize`, `data_seg_vmsize`,
  `linkedit_vmaddr`, `linkedit_file_off`, `linkedit_filesize`
* section bookkeeping: `sects[]`, `nsec`, `n_text_sects`, `n_data_sects`,
  `sect_idx_stub`, `sect_idx_nlptr`
* extern wiring: `nstubs`, `stubs[]`, `stub_for_elfsym[]`,
  `n_nlptrs`, `nlptrs[]`
* error: `ret`, `cleanup:` label

### Verification approach

Same as session 068:
1. Confirm happy path is unchanged (existing dylib demos all PASS).
2. Hand-inject a deliberate break per invariant (e.g.
   `text_seg_vmaddr = 0xdeadbeef;` just above the sanity block,
   `entry_addr-equivalent` adjustments where applicable, etc.),
   rebuild, observe the resulting tcc error message, **revert**.
3. New `demos/v0.2.59-dylib-self-link-diagnostics.{c,sh}` happy-
   path that builds + dlopens a dylib touching every emitted
   section class, parallel in spirit to the v0.2.50 EXE demo.

### Release shape

Bump to `v0.2.59-g3` for symmetry with v0.2.50 — a regression-
prevention release rather than a new-feature release, but bumping
keeps the demo naming consistent and gives the roadmap a row.

## What landed

* `tcc/ppc-macho.c` — single 230-ish-line sanity-check block
  inserted in `macho_output_dylib` between the
  `Compute __LINKEDIT layout` block and `Serialize: Mach header`.
  Pure read-and-check; no mutations of writer state. Same shape
  as the EXE block (don't bail on first failure; emit
  `tcc_error_noabort` for each violation; final
  `if (!sanity_ok) goto cleanup`).

* `demos/v0.2.59-dylib-self-link-diagnostics.c` and
  `demos/v0.2.59-dylib-self-link-diagnostics.sh` — happy-path
  demo. The dylib source touches every section class (text,
  rodata, data, bss, libSystem stubs, function-pointer
  initializer producing a locrel slide-time fixup) so the four
  checks walk every code path on a normal compile. Expected
  output: `ro=ok | data=42 | bss=11 22 33 | got=ok | call_count=2`.

## Verification

### Happy path

| suite | result |
|---|---|
| `demos/v0.2.59-dylib-self-link-diagnostics.sh` | PASS — clean dylib build + dlopen + run |
| eight existing dylib demos (v0.2.25, v0.2.26, v0.2.29, v0.2.31, v0.2.40, v0.2.41, v0.2.42; v0.2.28 skipped — sqlite source not present) | all PASS |
| `tests2` | 111/111 |
| `abitest` (cc + tcc) | all `success` |
| `bootstrap-tcc-self.sh FIXPOINT=1` | FIXPOINT HOLDS |

### Hand-injected breaks (one per invariant)

Each break was injected via perl right after `int sanity_ok = 1;`
in `macho_output_dylib`'s sanity block (anchored on `$. > 4690`
to skip the EXE writer's identical line at 3098). After each
round the source was restored from a backup copy, the source-tree
hash was unchanged, and the v0.2.59 demo re-PASSed.

| break | injection | diagnostic that fired |
|---|---|---|
| (a) | `linkedit_vmaddr = 0xdeadbeef;` | `ppc-macho: dylib __LINKEDIT vmaddr 0xdeadbeef != __TEXT+__DATA end 0x40003000 (would collide with __bss or earlier section)` |
| (b) | clobber `__mh_dylib_header.st_value = 0xdeadbeef` via `find_elf_sym` | `ppc-macho: '__mh_dylib_header' value 0xdeadbeef != __TEXT base 0x40000000` |
| (c) | `if (nstubs > 0) stubs[0].addr = 0xdeadbeef;` | `ppc-macho: dylib stub 0 addr 0xdeadbeef outside __symbol_stub1 [0x40000920+0x20]` |
| (d) | `nsec = 0;` | four messages — `dylib symbol '_diag_ro' is defined in section '.data' but that section is not emitted by the dylib writer (missed section-pickup case in macho_output_dylib?)`, plus the same shape for `_diag_bss`/`_diag_init_fn`/`_L.9` |

In every case `tcc -shared` exited non-zero and produced no output
file, exactly as intended.

## Findings worth remembering

### perl `-pe` + `next` does NOT skip the implicit print

First attempt at the break-injection script used:
```
perl -i -pe "if(MATCH){print; print qq(INJECTED); next}"
```
This DUPLICATED the matched line — perl's `-p` mode wraps the
script in `while(<>){...} continue { print }`, and `next` jumps
to the next iteration but **still runs the continue block**. So
the implicit print fired after the explicit one. Replace the
duplication-prone `print; next` with `$_ .= "INJECTED\n"`, which
appends the injection to `$_` and lets the implicit print emit
the combined line + injection without duplication. Captured here
in case a future session reaches for the same trick.

### `linkedit_filesize = loff - linkedit_file_off;` appears in
both EXE and dylib writers

The line `linkedit_filesize = loff - linkedit_file_off;` is
present at 3079 (EXE writer) AND 4680 (dylib writer). My first
break-injection anchor used that line, and the FIRST match wins —
which meant I was injecting into the EXE writer's path while
trying to test the dylib writer. The EXE writer's `goto cleanup`
never fired (the `tcc -shared` invocation goes through
`macho_output_dylib`, not `macho_output_exe`), so the test
silently produced a working dylib and the verification reported
"MISS" four times in a row. Anchor on something unique to the
dylib path — the `(roadmap #7, dylib half).` comment, or
`$. > 4690` to skip past the EXE block.

## Notes on dylib-vs-EXE differences picked up while writing

Confirmed by reading `macho_output_dylib`:

* `text_seg_vmsize == text_seg_filesize` always for the dylib
  writer (set at line 248 of the function relative to its
  declaration, i.e. `text_seg_vmsize = text_seg_filesize`).
  So the EXE-style `vmsize >= filesize` check is trivially true
  for dylibs but kept for symmetry — one fewer special case in
  the future if the writer ever grows a real vmsize ≠ filesize
  case.
* No `__DWARF` segment in the dylib path (verified by grep —
  zero references to `dwarf` inside `macho_output_dylib`). The
  EXE block's __DWARF-sandwich check is dropped from the dylib
  block.
* Dylib stubs are PIC and 32 bytes each (8 instructions); EXE
  stubs are absolute and 16 bytes each (4 instructions). Used
  `32u` in the dylib's stub-bounds check.
* `__mh_dylib_header` is registered (search-confirmed at
  function-relative line 122) as the dylib equivalent of
  `__mh_execute_header`. Same SHN_ABS / `text_seg_vmaddr`
  invariants apply.

## Next session

[HANDOFF.md](HANDOFF.md)
