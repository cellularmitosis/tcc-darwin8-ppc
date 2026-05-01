# 025 — Mach-O .o reader: full self-link via tcc

## Goal

Close the libSystem-init gap from
[024](../024-libsystem-init/README.md) by reading classic Mach-O `.o`
files directly so tcc can pull in `/usr/lib/crt1.o` automatically and
produce executables that work with libSystem stdio (printf, malloc,
etc.) — without going through `gcc-4.0` for the link step.

## Result

✅ **Full self-link works for normal libSystem-using programs.** A
plain `tcc -o exe file.c` (no explicit crt1 on the command line)
produces a working executable that uses printf/malloc/getenv just
fine. The auto-link of `/usr/lib/crt1.o` is wired into
`tcc_output_file`. Demos under [demos/s025-…](../../../demos/) confirm
end-to-end behavior on the iBook G3/G4 fleet.

🟡 **`tcc-self` still uses gcc-4.0 to link** because tcc.c references
libgcc helpers (`___udivdi3`, `___ashldi3`, `___fixdfdi`, etc.) that
aren't in our libtcc1.a yet. The Mach-O archive loader CAN now pull
members from `/usr/lib/gcc/.../libgcc.a` (BSD `#1/N` long names + the
new `macho_load_object_file` dispatch in `tcc_load_archive`), but the
whole-archive load currently produces a broken binary — see "Known
limitations" below. Adding the helpers to `lib/lib-ppc.c` (or fixing
the archive loader) is the next step.

## Test programs that work

All compile via plain `tcc -o exe file.c` and run cleanly on iBook G3:

* `int main(void) { return 7; }` — exits 7. (h3)
* `puts("hi")` — prints "hi". (h2)
* `printf("hello via full self-link\n")` — prints. (h)
* `malloc(64) + free + printf("argc=%d argv[0]=%s\n", ...)` — works. (m)

## Implementation summary

The reader (`tcc/ppc-macho.c::macho_load_object_file`, ~700 LOC) parses
classic Mach-O object files and merges them into tcc's ELF-style
in-memory model so the existing link/output pipeline can handle them.
Key pieces:

1. **Fat (universal) binary support.** `/usr/lib/crt1.o` ships as a
   fat archive with 4 architectures (ppc, i386, ppc64, x86_64).
   `tcc_object_type` and `macho_load_object_file` both detect
   `0xcafebabe` magic and seek to the ppc slice (cputype=18) before
   parsing.

2. **Section mapping.** `__TEXT,__text` → `.text`,
   `__TEXT,__const`/`__cstring` → `.data.ro` (tcc's rodata
   section name; merging into one avoids creating a parallel
   `.rodata`), `__DATA,__data` → `.data`, `__DATA,__bss` → `.bss`.
   `__symbol_stub*` and `__nl_symbol_ptr` are skipped — the EXE writer
   re-synthesizes them from scratch.

3. **Symbol translation.** Pass 2 walks `nlist` entries (starting at
   index 0 — Mach-O has no null-sym reservation, unlike ELF; this
   off-by-one was the source of a multi-hour bug where 1-symbol
   archive members like `_ashldi3.o` silently dropped their only
   definition). Common (`N_UNDF + n_value > 0`) symbols allocate space
   in `.bss`. Defined-in-section symbols become `STB_GLOBAL`/`LOCAL`
   tcc symbols at the merged offset.

4. **Reloc translation.** `PPC_RELOC_VANILLA`, `BR24`/`JBSR`,
   `HA16`/`HI16`/`LO16` (with `PAIR`) translate to the corresponding
   `R_PPC_*` types. Two cases needed care:
   - **Indirect refs through skipped sections.** When a
     `__symbol_stub1` or `__nl_symbol_ptr` slot is referenced, the
     reader walks the indirect symtab to find the underlying extern
     and rewrites the reloc to target it. If the original was
     `__symbol_stub1` (a function call site), the new tcc symbol is
     tagged with `ST_PPC_NEEDS_STUB` (a custom `st_other` bit) so the
     EXE writer's stub allocator picks it up even without a
     `R_PPC_REL24` reloc.
   - **Local-section targets.** For non-extern relocs into kept
     sections (e.g. crt1's `bl __start` and `lis r2, ha(_NXArgc)`),
     the reader synthesizes anonymous local "anchor" symbols pointing
     at the exact target offset. PPC reloc handlers REPLACE the
     immediate field rather than accumulating instruction bits as an
     addend, so the anchor's `st_value` must equal the absolute
     target — which we get for free since the merged offset is the
     final position.

5. **EXE-writer fixes** (in `macho_output_exe`):
   - `bss` section is now laid out in `__DATA` as zerofill
     (`S_ZEROFILL`, vmsize > filesize). `__LINKEDIT` shifts to use the
     full vmsize, not just the file portion.
   - `__mh_execute_header` is registered in `s1->symtab` as
     `STB_GLOBAL`/`SHN_ABS` at `text_seg_vmaddr` BEFORE
     `collect_extern_nlptrs` runs, so crt1's UNDEF reference to it
     resolves locally instead of being routed through libSystem.
   - `exe_sym_addr` handles `SHN_ABS` and falls back to a non-lazy
     pointer slot's VA when an UNDEF extern data symbol has a slot
     but no stub.
   - `collect_extern_nlptrs` now also catches `R_PPC_ADDR16_HA/HI/LO`
     and `R_PPC_ADDR32` to UNDEF externs (not just the PIC variants),
     so crt1's libSystem-data-load idiom (`lis r3, ha(slot); lwz r3,
     lo(slot)(r3)`) gets a slot allocated.
   - Entry point prefers crt1's `start` symbol (no leading underscore
     on disk — unusual for Mach-O) over our local crt-shim when crt1
     is present.
   - `LC_TWOLEVEL_HINTS` is no longer emitted: it was a session-024
     workaround for libSystem not running its initializers, but with
     crt1.o providing the proper init dance the workaround is
     obsolete and was making `nm`/`otool` complain.

6. **Auto-link `/usr/lib/crt1.o`.** `tccelf.c::tcc_output_file` now
   `access()`-checks for crt1 and `tcc_add_file()`s it for
   `TCC_OUTPUT_EXE` on PPC. The `__tcc_start_main` injection is
   gated on the absence of crt1's `start` symbol — when crt1 is
   loaded, the inject is a no-op.

7. **Mach-O archive support.** `tcc_load_archive` now:
   - Forces whole-archive mode for Mach-O targets (proper alacarte
     loading via `__.SYMDEF SORTED` is a future task).
   - Parses BSD-style `#1/N` long-name encoding so the archive name
     in `hdr.ar_name` reflects the real member name.
   - Dispatches `AFF_BINTYPE_MACHO_REL` members to
     `macho_load_object_file`.

## Diagnosing journey

The whole session was tcc → produce error → diagnose → fix → repeat:

| Error | Root cause |
|---|---|
| `unrecognized file type` on `/usr/lib/crt1.o` | crt1 is a fat (universal) binary — magic `0xcafebabe`, not `0xfeedface`. Added fat detection. |
| `'_NXArgc' defined twice` | `__tcc_start_main` injection ran even when crt1.o defined the same symbols. Added gate. |
| `'_L.0' (no stub allocated…)` for string literal | `_L.0` (anon string label) is in `.data.ro`, but EXE writer's section walk only looked at `.rodata`. Mapped Mach-O `__cstring` to `.data.ro` to dedupe. |
| `'___darwin_gcc3_preregister_frame_info' (no stub allocated…)` | Common symbol allocated to `.bss` but `.bss` wasn't in the EXE writer's section list. Added bss layout + `S_ZEROFILL` section emission. |
| `Cannot allocate memory` from dyld | `__LINKEDIT.vmaddr` collided with `__bss` because it was computed from `data_seg_filesize` (file portion only) instead of `data_seg_vmsize`. |
| `Symbol not found: __mh_execute_header` (in libSystem) | crt1's UNDEF reference made our nlptr collector route it through libSystem. Pre-define it in s1->symtab as N_ABS. |
| SIGBUS during crt1 startup | Crt1's `lis r12, ha(stub_va); ori r12, r12, lo(stub_va); mtctr; bctr` indirect-call sequences computed slot addresses (data) instead of stub addresses (code). Added `ST_PPC_NEEDS_STUB` hint. |
| `'___ashldi3' undefined` after whole-archiving libgcc.a | Off-by-one: pass 2 started at `i=1` (ELF convention) but Mach-O sym 0 is valid. `_ashldi3.o`'s only sym was at index 0 → silently dropped. |

## Known limitations (future work)

* **libgcc.a whole-archive load produces a broken binary.** Even a
  simple `printf("hi")` program crashes if linked with
  `/usr/lib/gcc/.../libgcc.a` on the command line. The reader loads all
  75 members successfully but something downstream (probably an
  unhandled reloc kind or a section-name mismatch in one of the
  members) corrupts the output. Bisecting which member breaks things
  is the obvious next step.

* **No alacarte symbol-table loading for Mach-O archives.** We
  whole-archive every member, which over-links and exposes the bug
  above. Proper fix: parse `__.SYMDEF SORTED` and only load members
  that resolve currently-undefined symbols.

* **PPC libgcc helpers not in `libtcc1.a`.** `lib/libtcc1.c` has
  generic-C versions of `__udivdi3`, `__ashldi3`, `__fixdfdi`, etc.,
  but is `#if defined __i386__`-gated. Adding a PPC branch (or just
  removing the architecture gate for the routines that don't use
  inline asm) would make `tcc -o tcc-self tcc.o libtcc1.a` work
  without needing libgcc.a at all. Cleanest fix.

* **`__eh_frame` and other DWARF sections** are skipped wholesale.
  Fine for not-emitting-debug-info, but means scattered LOCSDIF
  relocs in those sections are silently dropped — which is what we
  want, but worth being explicit about.

## Files changed

* `tcc/tcc.h` — `AFF_BINTYPE_MACHO_REL` (=5), forward-decl
  `macho_load_object_file`.
* `tcc/tccelf.c` — `tcc_object_type` Mach-O detection (incl. fat),
  `tcc_load_archive` Mach-O dispatch + BSD `#1/N` name parsing,
  `tcc_output_file` auto-load of `/usr/lib/crt1.o`, gate on `start`.
* `tcc/libtcc.c` — `tcc_add_binary` dispatch.
* `tcc/ppc-macho.c` — the reader (~700 LOC), plus EXE-writer fixes
  for bss layout, `SHN_ABS`, nlptr fallback for HA16/LO16, stub-hint
  collection, entry-point selection, dropped LC_TWOLEVEL_HINTS.
* `tcc/lib/lib-ppc.c` — removed redundant/buggy `__tcc_start_main`
  (the auto-inject covers it).
* `README.md` — primary dev host updated to `ibookg37`.

## How to verify

On `ibookg37`:

```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
./scripts/build-tiger.sh

cat > /tmp/h.c <<'EOF'
int printf(const char *,...);
int main(void) { printf("hello via full self-link\n"); return 0; }
EOF

./tcc/tcc -B./tcc -I./tcc -o /tmp/h /tmp/h.c
/tmp/h
# -> hello via full self-link
```

No `gcc-4.0` involvement. The auto-link of `/usr/lib/crt1.o` happens
inside `tcc_output_file`.

The existing self-host pipeline still works:

```sh
./scripts/bootstrap-tcc-self.sh    # gcc-4.0 still does the link;
                                    # libgcc helpers come from libgcc.a
/tmp/tcc-self -v                    # tcc-built-by-tcc, runs
```
