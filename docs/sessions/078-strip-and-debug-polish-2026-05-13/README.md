# Session 078 — strip-and-debug-via-dSYM polish (v0.2.57-g3)

**Date:** 2026-05-13 → 2026-05-14
**Build hosts:** uranium ⇄ imacg3 (rsync + ssh)
**Arrival HEAD:** `21ad15a` (end of session 077 / v0.2.56-g3 handoff).
**Exit HEAD:** v0.2.57-g3 (this session).

## TL;DR

The two pre-existing tcc gaps the v0.2.56 handoff flagged as
blocking the *strip-and-distribute-`.dSYM`-separately* workflow on
Tiger are now closed:

1. **`strip -S exe` succeeds.** The `__DWARF` segment used to sit
   *after* `__LINKEDIT` in the file image; cctools `strip` rejects
   that with `the __LINKEDIT segment does not cover the end of the
   file (can't be processed)`. v0.2.57 lays `__DWARF` *before*
   `__LINKEDIT` and page-pads it so `__LINKEDIT.fileoff` lands on a
   page boundary (else dyld faults trying to mmap the indirect
   symbol table). A second strip-side check
   (`symbol table out of place`) forced the `__LINKEDIT` interior
   order to swap from `indirect → symtab → strtab` to gcc-4.0's
   canonical `symtab → indirect → strtab`.

2. **`LC_UUID` is emitted.** A 16-byte content fingerprint (FNV-1a
   64-bit twice over post-relocation text/rodata/data/dwarf bytes,
   with RFC 4122 v4 marker bits) is written into the linked exe.
   `dsymutil` propagates the same UUID into the `.dSYM` bundle, so
   Tiger gdb 6.3 pairs the stripped exe with its companion `.dSYM`
   by UUID match — `gdb stripped-exe` now finds and loads
   `stripped-exe.dSYM` automatically. The UUID is deterministic:
   the same source + toolchain produces the same UUID across
   rebuilds.

A third change rides along: the per-function debug-map bracket
gained an `N_SOL` entry between `N_FUN(name)` and `N_FUN(size)`,
matching gcc-4.0's chain shape exactly. This does **not** by
itself fix dsymutil's full `.debug_info` consolidation (still
empty in the `.dSYM` — only `.debug_pubnames` is populated, so
gdb can resolve symbol names via the `.dSYM` but not source
lines). That gap is pre-existing from v0.2.56, predates today's
work, and is documented in the HANDOFF as the v0.2.58 task.

* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint
  holds; v0.2.37–v0.2.56 debug demos all green; new v0.2.57 demo
  green.**

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) (+188 / −69):

  * `#define LC_UUID 0x1b` added to the load-command catalog.

  * `macho_output_exe`:

    * `__DWARF` segment now laid out *before* `__LINKEDIT` in the
      file (was: after). Section file offsets assigned at the
      `text+data` end; segment `filesize` rounded up to the next
      page so `__LINKEDIT.fileoff` lands page-aligned (required for
      dyld's `__LINKEDIT` mmap — otherwise
      `doBindIndirectSymbolPointers` faults at the indirect
      symbol-table address). VM layout unchanged: `__DWARF` keeps
      `vmaddr=0`, so `__LINKEDIT.vmaddr` still sits right after
      `__TEXT+__DATA`.
    * `__LINKEDIT` interior order changed from
      `indirect → symtab → strtab` to `symtab → indirect → strtab`
      (gcc-4.0's order; cctools `strip` enforces it via the
      `symbol table out of place` check). The pad/write block was
      reordered to match.
    * Sanity check `__LINKEDIT.fileoff == __TEXT+__DATA end`
      updated to add `+ dwarf_seg_filesize` and a new check
      asserts `__DWARF.fileoff == __TEXT+__DATA end` so we catch
      any future layout regression at link time.
    * The `LC_SEGMENT __DWARF` and `LC_SEGMENT __LINKEDIT` write
      blocks were physically swapped so the LC table also lists
      `__DWARF` before `__LINKEDIT`.
    * `LC_UUID` (24 bytes total: cmd + cmdsize + 16 UUID bytes)
      now always emitted, between `LC_DYSYMTAB` and
      `LC_UNIXTHREAD`. `ncmds` and `total_cmds_size` bumped
      unconditionally.
    * The 16 UUID bytes are computed (after relocations resolve,
      before serialization) by running two FNV-1a 64-bit hashes
      with different seeds over the post-relocation bytes of
      `text + rodata + eh_frame + data + init_array + fini_array`
      and every `__DWARF` section, then setting the RFC 4122 v4
      marker bits.

  * `emit_stab_nlist`: per-function bracket gained an `N_SOL`
    record between `N_FUN(name)` and `N_FUN(size)`, carrying the
    `.o`-path string offset (the same string `N_OSO` already
    points at). gcc-4.0 emits this carrying the source path; we
    use the `.o` path as a stand-in because we don't yet read
    the `.o`'s DWARF-CU `AT_name` at link time. Tiger's dsymutil
    accepts the marker structurally; full `.debug_info`
    consolidation requires a different fix (see HANDOFF).

* [`docs/roadmap.md`](../../roadmap.md) — v0.2.57-g3 row prepended,
  header bumped, #7.5 row gets the v0.2.57 follow-on appended.

* [`demos/README.md`](../../../demos/README.md) — v0.2.57 row
  added above v0.2.56.

### Files added

* [`demos/v0.2.57-strip-uuid-dsym.c`](../../../demos/v0.2.57-strip-uuid-dsym.c)
  — main TU; calls `helper_add` + `helper_mul` then prints
  `sum=N prod=N argc=N`.
* [`demos/v0.2.57-strip-uuid-dsym-helpers.c`](../../../demos/v0.2.57-strip-uuid-dsym-helpers.c)
  — helpers TU. Multi-TU build keeps the OSO chain non-trivial.
* [`demos/v0.2.57-strip-uuid-dsym.sh`](../../../demos/v0.2.57-strip-uuid-dsym.sh)
  — driver. Asserts:
  * `__DWARF` appears before `__LINKEDIT` in the LC table
  * `__LINKEDIT` covers the end of the file
  * `LC_UUID` present, non-zero, deterministic across rebuilds
  * `dsymutil` produces a `.dSYM` carrying the same `LC_UUID`
  * `N_SOL` records appear in the OSO chain (one per function)
  * `strip -S` exits 0; stripped exe still runs; `LC_UUID`
    survives the strip
  * Tiger `gdb` pairs the stripped exe with its `.dSYM` via
    UUID and resolves `info functions main` from it.

* `docs/sessions/078-strip-and-debug-polish-2026-05-13/README.md`
  (this file) and `HANDOFF.md`.

### Memory updates

None. The Tiger-specific gotchas (cctools strip's
`__LINKEDIT-covers-end-of-file` assertion, the `symbol table out
of place` interior-order check, dyld's page-alignment requirement
for `__LINKEDIT`, dsymutil's `.dSYM` UUID propagation) are all
captured in the source comments of `macho_output_exe` and in
this README.

## Findings worth remembering

### cctools strip on Tiger requires three layout invariants

`strip -S exe` walks the segments and aborts unless ALL of:

1. The segment with the highest fileoff is `__LINKEDIT`. Pre-fix,
   tcc placed `__DWARF` after `__LINKEDIT`, so strip aborted with
   "the __LINKEDIT segment does not cover the end of the file
   (can't be processed)".
2. Inside `__LINKEDIT`, the symbol table comes BEFORE the
   indirect symbol table. Pre-fix, tcc emitted indirect first.
   strip aborted with "symbol table out of place".
3. The `__LINKEDIT` segment's `fileoff` is page-aligned. (Implicit
   from a separate dyld constraint — but matters at strip time
   too, because strip rewrites the file region from
   `__LINKEDIT.fileoff` onward and assumes it's mappable.)

All three are now satisfied by `macho_output_exe`. The first
two have explicit sanity checks (see "Pre-serialize sanity checks"
block). The third is a load-bearing comment on the
`dwarf_seg_filesize` page-pad.

### dyld faults on non-page-aligned `__LINKEDIT.fileoff`

When we first reordered `__DWARF` before `__LINKEDIT`, the new
`__LINKEDIT.fileoff` was `__DWARF.fileoff + __DWARF.filesize` —
which is non-page-aligned because the `__debug_*` section sizes
don't sum to a page multiple. dyld's mmap of `__LINKEDIT` then
returned a page that didn't actually back the indirect-table file
bytes, so `doBindIndirectSymbolPointers` faulted at
`__LINKEDIT.vmaddr + offset(indirect)` (= 0x40b4 in our test).

Fix: round `dwarf_seg_filesize` up to the next page. The padding
bytes are zeros and live at the tail of the `__DWARF` segment in
the file; `__DWARF` is `vmaddr=0/vmsize=0` so the padding never
touches VM.

### Tiger `dsymutil` UUID propagation works out-of-the-box

Once we emit `LC_UUID` in the linked exe, `dsymutil` copies the
exact same 16-byte UUID into the `.dSYM` bundle's inner Mach-O
file (in `.dSYM/Contents/Resources/DWARF/<basename>`). Tiger
gdb 6.3 then pairs the two via UUID lookup — no warnings, no
manual hookup. The UUID is the only identification mechanism;
gdb does not look at file contents otherwise.

For gdb to find the `.dSYM`, the inner DWARF file must be named
after the exe's basename, and the bundle must sit at
`<exe>.dSYM` next to the exe. Both are dsymutil defaults; if you
strip the exe to a different name, copy the `.dSYM` along too
(or strip in place, which is what the v0.2.57 demo does).

### dsymutil consolidates pubnames but not `.debug_info`

Even with N_SO/N_OSO/N_BNSYM/N_FUN/N_SOL/N_FUN/N_ENSYM/N_SO
chains structurally identical to gcc-4.0's, Tiger's dsymutil
(`dwarf_utilities-42`) writes a `.dSYM` that has only
`.debug_pubnames` populated; `.debug_info`, `.debug_line`,
`.debug_abbrev`, `.debug_str` are all empty. So:

* `gdb info functions main` — works (reads pubnames).
* `gdb break *main`, `print main` — works (resolves symbol).
* `gdb list main`, `break main:LINE`, `step` source view —
  fail with `No line number known for main`.

The pre-strip flow (where gdb reads stabs directly out of the
exe's `LC_SYMTAB`) handles all of these cases since v0.2.51, so
this gap matters only for the *post-strip* flow.

The cause is not yet fully isolated. Hex-patching `N_SO` to point
at a real source path didn't help. Adding `N_SOL` (gcc's chain
parity) didn't help. Plausible next investigations:

* dsymutil may need `N_SOL` to carry the *source* path, not the
  `.o` path. Reading the source path from the `.o`'s
  `__DWARF,__debug_info` (DWARF compile-unit `AT_name`, encoded
  as `DW_FORM_strp` into `__debug_str`) requires DWARF parsing
  in `macho_load_object_file`.
* dsymutil may reject some other property of tcc's DWARF that
  gcc-4.0 satisfies (line-table format? abbrev table?
  comp_dir/AT_name resolution?). A diff against a gcc-built `.o`
  with the same source would isolate it.

This investigation is the v0.2.58 task.

### LC_UUID generation choice

We picked FNV-1a 64-bit because:

* Deterministic — same code/data/debug → same UUID.
* No external crypto dependency (Tiger doesn't have a stable
  MD5/SHA-1 in libc).
* Fast — single pass per buffer.

The two-hash design (different seeds, also feeding the buffer
backwards in the second pass to decorrelate) gives 16 bytes
without spurious collisions for the small inputs typical of tcc
builds. RFC 4122 v4 marker bits make the result parseable by
otool / dwarfdump / lldb pretty-printers.

Apple's ld64 historically used MD5(content) for `LC_UUID`. We
don't claim MD5 strength here — just "different builds get
different UUIDs reliably", which is all gdb's pairing needs.

## How to pick up

### Verify the v0.2.57 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.57-strip-uuid-dsym.sh
```

Expected last line:

```
OK: __DWARF reorder + page-padding makes strip -S work; LC_UUID
    round-trips through dsymutil, survives strip, and lets gdb
    pair the stripped exe with its .dSYM.
```

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    cd tcc/tests && make abitest && cd ../.. && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0`, 48 abitest successes,
`FIXPOINT HOLDS`.

### Re-run the v0.2.37–v0.2.57 debug demos

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    for d in v0.2.37-dwarf-obj v0.2.38-dwarf-exe v0.2.39-eh-frame \
             v0.2.44-gdebug-link v0.2.51-gstabs-oso-stab \
             v0.2.52-gstabs-locals v0.2.53-bnsym-ensym \
             v0.2.54-stabs-roundtrip v0.2.55-g-default-stabs \
             v0.2.56-n-oso-dsymutil v0.2.57-strip-uuid-dsym; do
        printf "%s: " "$d"; bash demos/$d.sh 2>&1 | tail -1
    done'
```

Each line should end with `PASS` / `OK: ...` / a one-line
success summary.
