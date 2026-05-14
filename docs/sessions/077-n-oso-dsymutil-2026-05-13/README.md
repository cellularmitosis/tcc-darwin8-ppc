# Session 077 — N_OSO + dsymutil-on-Tiger (v0.2.56-g3)

**Date:** 2026-05-13
**Build hosts:** uranium ⇄ imacg3 (rsync + ssh)
**Arrival HEAD:** `8828b62` (end of session 076 / v0.2.55-g3 + handoff).
**Exit HEAD:** v0.2.56-g3 (this session).

## TL;DR

`dsymutil-on-Tiger` now consumes tcc-built `-gdwarf-2` Mach-O exes
and produces a working `.dSYM` bundle — closes roadmap **#7.5 Phase 2
item 1b**, the final remaining sub-item of the stabs/gdb-on-Tiger arc.

```
# pre-v0.2.56
$ tcc -gdwarf-2 -c foo.c -o foo.o
$ tcc -gdwarf-2 foo.o -o foo
$ dsymutil foo -o foo.dSYM
warning: no debug map in executable (-arch ppc)        # empty .dSYM

# v0.2.56
$ tcc -gdwarf-2 -c foo.c -o foo.o
$ tcc -gdwarf-2 foo.o -o foo
$ dsymutil foo -o foo.dSYM
$ dwarfdump --debug-pubnames foo.dSYM/Contents/Resources/DWARF/foo
0x0000003f: main
0x000000ca: helper_add
0x0000009e: helper_mul
```

The stabs flow (`tcc -g` / `tcc -gstabs`) is unchanged from v0.2.55 —
Tiger `gdb 6.3` still reads stabs straight out of `LC_SYMTAB` without
any companion `.o` or `.dSYM`. The new machinery only fires under
`-gdwarf-N`.

## What landed

### Code changes

Four coupled changes plus one cleanup:

**1. `tcc/stab.def`** — define `N_OSO = 0x66` (Apple's "object file
path" stab):

```c
__define_stab (N_OSO, 0x66, "OSO")
```

**2. `tcc/tcc.h`** — new `struct macho_oso_state` (and per-state
`struct macho_oso_func`) plus an array pointer on `TCCState`'s
Mach-O block:

```c
struct macho_oso_func {
    char    *name;        /* function name, with leading '_' */
    uint32_t off;         /* offset in merged text_section */
    uint32_t size;        /* function body size (computed by adjacency) */
};
struct macho_oso_state {
    char    *path;        /* realpath of the .o */
    uint32_t mtime;       /* st_mtime from stat() */
    uint32_t after_idx;   /* idx in stab_section to inject N_OSO after
                           * (0 = synthesize SO/OSO/SO triplet at end) */
    uint32_t text_off;    /* this .o's offset in the merged text_section */
    uint32_t text_size;   /* this .o's contribution to text_section */
    struct macho_oso_func *funcs;
    int      n_funcs;
};
```

**3. `tcc/ppc-macho.c`** — `macho_load_object_file`'s Pass 1.5 records
per-loaded-.o OSO state, gated to DWARF mode only
(`s1->dwarf != 0`). Why DWARF-only:

* `dsymutil` only consumes DWARF — the whole point of the OSO chain.
* Tiger `gdb 6.3` reads stabs straight out of the linked exe's
  `LC_SYMTAB`; it doesn't need OSO.
* And if we DID emit OSO in stabs mode, gdb's OSO follower would
  open the on-disk `.o` to re-read stabs — but tcc places `.o`
  stabs in `__DWARF,__stab` (the v0.2.54 round-trip layout), not
  in `LC_SYMTAB` nlist (gcc-4.0's placement), so gdb SIGBUSes
  when it tries.

The capture: `realpath()` of `s1->current_filename` (using the stack-
buffer form to avoid Tiger libSystem's `realpath(...,NULL)` SIGBUS),
`stat()` for `st_mtime`, the .o's `__TEXT,__text` contribution offset
+ size, and a scan of the .o's nlist for defined-in-`__text`
function symbols (skipping `$a` / `L*` mapping syms). Functions are
sorted by offset; sizes computed by adjacency (next.off − this.off,
last function = `text_off + text_size − this.off`).

**4. `tcc/ppc-macho.c`** — `emit_stab_nlist` synthesizes a free-
standing debug-map chain in the linked exe's `LC_SYMTAB` per OSO
state. Per-`.o` layout matches gcc-4.0's `-gdwarf-2` exact format:

```
N_SO  <objpath>            n_sect=0 n_value=0
N_OSO <objpath>            n_sect=0 n_desc=1 n_value=mtime
  N_BNSYM ""               n_sect=1 n_value=fn_va
  N_FUN   <fn name>        n_sect=1 n_value=fn_va
  N_FUN   ""               n_sect=0 n_value=fn_size
  N_ENSYM ""               n_sect=1 n_value=fn_va
  ... (per function) ...
N_SO  ""                   n_sect=0 n_value=0
```

`dsymutil` walks SO-pair / OSO / BNSYM/FUN markers to map linked-exe
addresses back to each `.o`'s DWARF subprograms.

**5. `tcc/tccdbg.c`** — two pre-existing DWARF-2-strictness bugs
fixed under `#if defined TCC_TARGET_PPC && !defined(TCC_TARGET_PPC64)`
so they don't disturb other targets:

* **`AT_frame_base`** now emits `DW_OP_reg31` (one-byte DW_OP_reg0+31
  = 0x6F, "register 31"). Pre-fix tcc emitted `DW_OP_call_frame_cfa`
  for every non-x86/ARM target — that's a DWARF-3 op, and Tiger's
  cctools `dwarf_utilities-42` asserts on it:
  `failed assertion '!"Unknown one-operand"'`. r31 is the post-prolog
  frame pointer (= r1 + frame_size, set in `gfunc_prolog`); locals
  encode FP-relative offsets via `DW_OP_fbreg`, so using r31 as the
  frame base resolves to the same address as the CFA would have.

* **`AT_high_pc`** (`DW_FORM_data4`) now emits `func_ind + size`
  (absolute end-VA) with a text-section reloc, instead of just `size`
  (offset form). DWARF-2 mandates `DW_FORM_addr` for high_pc;
  DWARF-3+ allows offset-form `DW_FORM_data4`. tcc has been using
  data4-as-offset on PPC since v0.2.37 — works for some consumers
  but breaks dsymutil's multi-function-per-`.o` scoping: pre-fix the
  second-and-onward functions in a `.o` got `[low,high)` ranges that
  collapsed to empty (`low=fnstart`, `high=fnsize` — but `low > high`
  except for the first function), and dsymutil silently dropped them
  from the pubnames table. Post-fix all functions have distinct
  ranges and all appear in the consolidated `.dSYM`.

**6. `tcc/libtcc.c`** — `tcc_delete` frees each OSO state's `path` +
`funcs[]` array + the array itself.

Plus one collateral docs fix (out-of-band):

**`demos/v0.2.39-eh-frame.sh`** — v0.2.55 silently invalidated this
demo's `tcc -g` → "DWARF version 2" assertion (post-v0.2.55, bare
`-g` is stabs). Switched it to `-gdwarf` (which still resolves to
`DEFAULT_DWARF_VERSION = 2` on Darwin). The demo's actual purpose
(verifying eh_frame + per-prolog CFI emission) is unchanged.

### Files added

* [`demos/v0.2.56-n-oso-dsymutil.c`](../../../demos/v0.2.56-n-oso-dsymutil.c)
  — main TU (3-line `main` calling `helper_add` + `helper_mul`)
* [`demos/v0.2.56-n-oso-dsymutil-helpers.c`](../../../demos/v0.2.56-n-oso-dsymutil-helpers.c)
  — helpers TU (two functions in one .o, exercises the multi-
  function-per-.o high_pc fix)
* [`demos/v0.2.56-n-oso-dsymutil.sh`](../../../demos/v0.2.56-n-oso-dsymutil.sh)
  — driver. Tests both stabs and DWARF flows:
  * stabs: no OSO records emitted, scripted gdb reads `info line main`
    + source listing directly out of the exe (regression check that
    v0.2.51-v0.2.55 still works);
  * DWARF: six SO/OSO entries (one chain per `.o`), both OSO records
    carry the .o's realpath + non-zero mtime, three BNSYM/ENSYM pairs
    straddle `main` + `helper_add` + `helper_mul` with named FUN
    markers; `dsymutil` exits 0 and produces a `.dSYM` whose
    `__debug_pubnames` lists all three.
* [`docs/sessions/077-n-oso-dsymutil-2026-05-13/README.md`](README.md)
* `HANDOFF.md` (to be written at end of session).

### Docs updated

* [`docs/roadmap.md`](../../roadmap.md) — v0.2.56-g3 row prepended,
  header bumped ("v0.2.55-g3" → "v0.2.56-g3", "Thirty-three" →
  "Thirty-four"), #7.5 row rewritten to credit v0.2.56 with closing
  Phase 2 item 1b.
* [`demos/README.md`](../../../demos/README.md) — v0.2.56 row added
  above v0.2.55 (newest-first).

## Verification

* `bash scripts/run-tests2.sh` — `Total: 111  Pass: 111  Fail: 0`.
* `cd tcc/tests && make abitest` — 48 successes (24 cc + 24 tcc).
* `FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh` — `FIXPOINT HOLDS`.
* All ten release-debug demos still green:
  `v0.2.37-dwarf-obj`, `v0.2.38-dwarf-exe`, `v0.2.39-eh-frame`,
  `v0.2.44-gdebug-link`, `v0.2.51-gstabs-oso-stab`,
  `v0.2.52-gstabs-locals`, `v0.2.53-bnsym-ensym`,
  `v0.2.54-stabs-roundtrip`, `v0.2.55-g-default-stabs`,
  `v0.2.56-n-oso-dsymutil`.
* `dsymutil` end-to-end: clean exit + `.dSYM` with `__debug_pubnames`
  listing all three demo functions.

## Findings worth remembering

### Tiger `dsymutil` only consumes DWARF in `.o` files

Empirical from this session: pointing `dsymutil` at a `tcc -gstabs`-
built exe gets `warning: object file '...' doesn't contain DWARF
debug information for ppc.` for every `.o`. The OSO chain finds the
.o files (good); the .o files just don't have DWARF (only stabs).

So the handoff's expected workflow `tcc -g -c foo.c && tcc -g foo.o
-o foo && dsymutil foo` was based on a misreading of what
dsymutil-on-Tiger does. The achievable workflow is the
`-gdwarf-2` variant.

### Tiger `gdb 6.3` reads `.dSYM` bundles

Verified directly on imacg3: `dsymutil foo -o foo.dSYM && gdb foo`
loads the bundle and resolves source/line. UUID mismatch between
exe and .dSYM does come up (tcc doesn't emit `LC_UUID`) — gdb
warns but still proceeds for the un-stripped exe.

For the strip-and-debug-via-dSYM workflow (`strip foo; gdb foo` with
.dSYM in place), tcc has two further pre-existing issues:

* `strip -S foo` fails with `the __LINKEDIT segment does not cover
  the end of the file` — tcc lays `__DWARF` after `__LINKEDIT`,
  which Apple's strip predates.
* `LC_UUID` not emitted, so the .dSYM/exe UUIDs don't match — gdb
  warns but accepts.

Both out of scope for this session; tracked as future work in the
handoff.

### Tiger `dsymutil` is a 2007 cctools binary

`/usr/bin/dsymutil` reports `PROGRAM:dsymutil PROJECT:dwarfutils-42
DEVELOPER:root BUILT:Sep 21 2007`. It asserts on unknown DW_OPs,
silently drops empty-range subprograms, and produces a `.dSYM` with
two `__DWARF` segments (one carry-over from the exe at vmaddr=0
with size=0 in the sections, one consolidated at vmaddr=0x4000 with
real content). dwarfdump on the .dSYM sometimes reports the first
(empty) segment; the real DWARF is there. Tiger gdb finds the right
one.

### gcc-4.0's `-gdwarf-2` exe debug-map format (cross-checked)

Confirmed on imacg3 via `nm -ap` + raw nlist dump:

```
N_SO  <source-file>   n_sect=0 n_desc=0 n_value=0
N_OSO <obj-path>      n_sect=0 n_desc=1 n_value=mtime
N_BNSYM ""            n_sect=1 n_value=fn_start
N_FUN   <fn name>     n_sect=1 n_value=fn_start
N_SOL   <source-file> n_sect=0 (per inlined function — gcc-only)
N_FUN   ""            n_sect=0 n_value=fn_size
N_ENSYM ""            n_sect=1 n_value=fn_start    (note: start, not end)
... (per function) ...
N_SO  ""              n_sect=0 n_value=0
```

Note `N_ENSYM`'s `n_value` is the function START (same as `BNSYM`),
not the end — counterintuitive but matches gcc-4.0 verbatim. tcc
emits the same.

### gcc-4.0's `.o` files don't carry OSO records

A gcc-4.0 `-gdwarf-2 -c` `.o` file has NO stab entries at all in
its `LC_SYMTAB` — only regular function/data symbols. The OSO chain
is synthesized entirely by Apple's `ld` at link time from the .o
input list. tcc follows the same model: no `.o`-side OSO emission;
synthesis happens in `emit_stab_nlist` during `macho_output_exe`.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

This was a longer session than the handoff suggested — the
N_OSO-emission-itself work was the small part; the unexpected
expansions:

* dsymutil-on-Tiger doesn't process stabs `.o` files (only DWARF),
  so the handoff's "stabs flow + OSO" wasn't actually a dsymutil
  flow. The real flow is DWARF flow + OSO.
* Tcc's DWARF emission had two pre-existing DWARF-2-strictness
  bugs (`DW_OP_call_frame_cfa` and `AT_high_pc`-as-offset) that
  made even the DWARF flow unusable through dsymutil. Both fixed
  PPC-narrowly.
* Tiger gdb's stab reader crashes on tcc's `__DWARF,__stab` `.o`
  layout when following an OSO record (gcc puts `.o` stabs in
  `LC_SYMTAB`). So OSO is gated to DWARF mode only — emitting in
  stabs mode would let gdb crash itself.

The end-state is small in source-change terms (~150 lines net) but
involved several "discover the next blocker, fix, retest" cycles
before the demo went green end-to-end.

Future work for the broader debug-info story (out of scope here but
real):

* `LC_UUID` emission so `.dSYM` bundles can be UUID-matched.
* `strip -S` compatibility — currently fails because `__DWARF`
  lives after `__LINKEDIT` in the exe.
* `DW_AT_name "<string>"` at link time: tcc emits its own DWARF
  compile-unit for the linker run with placeholder name; in a
  dsymutil flow with proper `.o`-side DWARF this is moot, but for
  the gdb-reads-DWARF-from-exe path it'd be cleaner to suppress.
