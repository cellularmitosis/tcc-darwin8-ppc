# Session 054 — unsupervised work, 2026-05-09

## Arrival state

* HEAD `57ea9f0` from session 053.
* Two releases in 053: v0.2.38-g3 (DWARF in linked exe) and
  v0.2.39-g3 (`__eh_frame` + per-prolog CFI; default DWARF-2 on
  Darwin). The DWARF arc is functionally complete on the
  dwarfdump / lldb side.
* tests2 111/111. abitest-cc 24/24, abitest-tcc 24/24, libtest,
  dlltest all pass. Bootstrap fixpoint holds.
* Real-world demos all green; gzip/sed/Python investigation in
  053 surfaced 4 concrete bugs queued for this session.

## User's instruction

> "please proceed unsupervised. don't stop working until the
> roadmap is done or until I stop you. document everything,
> cut releases as appropriate, include a demo in each release
> if warranted. drop notes in chat so I can follow your
> progress."

## What landed

### v0.2.40-g3 — GNU sed 4.8 builds + works with tcc

Three substantive tcc fixes were needed to make sed 4.8 build
with `CC=tcc` and produce a working binary. Each was
discovered by trying to build sed and chasing the next
failure.

**Fix #1: VT_BOOL store via pointer.** REAL_WORLD_FINDINGS.md
from session 053 had this filed as an LDOUBLE issue (with the
type code `0xb` in the error message). 0xb is actually
VT_BOOL (VT_LDOUBLE is 0xa) — the previous session's findings
mis-labelled it. The bug is real: `ppc-gen.c::store()`'s GPR
branch had cases for VT_BYTE / VT_SHORT / VT_INT but no
VT_BOOL — `_Bool *p; *p = b;` aborted with "ppc-gen: store via
ptr of bt 0xb not yet supported". A second instance lived in
load-via-sym+off (the extra_off > 0 branch). Both fixed by
adding `case VT_BOOL:` alongside `case VT_BYTE:` (a `_Bool`
is one byte, so the same `stb`/`lbz` encoding works).

**Fix #2: `__attribute__((gnu_inline))` support +
`__GNUC_GNU_INLINE__=1` predefine.** gnulib's `lib/c-ctype.h`
defines all of `c_isdigit`, `c_isalpha` etc. inline via a
`_GL_INLINE` macro. On gcc-4.x in default mode, that expands
to `extern inline __attribute__((__gnu_inline__))` — the
gnu89-inline form: body for inlining only, no out-of-line
definition; the strong def comes from `c-ctype.c` which
redefines `_GL_INLINE` to `extern`. Without the attribute
handling, every TU that included `c-ctype.h` emitted its own
strong definition of `c_isspace` / `c_isdigit` / etc., and
linking against `libsed.a` failed with `'_c_isspace' defined
twice` (and 9 other duplicates). Fixed by:

  * Adding `TOK_GNU_INLINE1`/`TOK_GNU_INLINE2` to `tcctok.h`
    (`gnu_inline` and `__gnu_inline__`).
  * Mapping them to `func_alwinl` in `tccgen.c::parse_attribute`,
    which the existing logic at `tccgen.c:9118` already
    interprets as "rewrite `extern inline` → `static inline`"
    — exactly the gnu89 semantics we want.
  * Predefining `__GNUC_GNU_INLINE__=1` on `__APPLE__` in
    `tcc/include/tccdefs.h` so gnulib's `_GL_INLINE` actually
    uses the gnu_inline attribute path. (gcc-4.x defaults to
    `__GNUC_GNU_INLINE__=1` in default mode; tcc claims to
    be gcc-4.0 elsewhere, so this is consistent.)

**Fix #3: PIC SECTDIFF reloc translation for non-discarded
targets.** This is the deepest of the three. After fix #2,
sed built — but `s/h/H/` regex substitution silently produced
no match for ERE `(...)` and BRE `\{n,m\}`.

After standalone-regtest debugging that compared
`regcomp(REG_EXTENDED) + regexec` (worked) against
`re_set_syntax + re_compile_pattern + re_search` (broken),
the symptom narrowed to: inside `re_compile_pattern`, the
read of `re_syntax_options` (a global) returned 0, even
though the variable was correctly set to `0x7b27c` from
outside.

Reason: in `regcomp.c` (sed bundles gnulib's regex source),
`extern reg_syntax_t re_syntax_options;` is declared in
`regex.h`, then referenced inside `re_compile_pattern` (line
217), then tentatively defined at line 243 (`reg_syntax_t
re_syntax_options;`). When tcc compiles the file:

  * At the reference (line 217), tcc sees an extern UNDEF
    symbol — `ppc_need_pic_for_sym` returns 1, codegen emits
    `addis r12, r30, ha16(sym - func_anchor); lwz r12, lo16(sym
    - func_anchor)(r12)` (the standard PIC indirection through
    a `__nl_symbol_ptr` slot).
  * At the tentative def (line 243), `put_extern_sym` updates
    the existing elfsym entry's `st_shndx` from `SHN_UNDEF` to
    BSS. The reloc still references the same symtab index; at
    link time, `collect_extern_nlptrs` correctly skips it (no
    `__nl_symbol_ptr` slot needed for a defined sym).

The PIC SECTDIFF reloc the .o emits is a scattered
HA16_SECTDIFF / LO16_SECTDIFF pair against the symbol's
in-.o address. The .o file contains the .o-internal
displacement `(re_syntax_options_addr - func_anchor_addr)`
baked into the addis/addi instruction immediates.

`ppc-macho.c::macho_translate_relocs` translates these
scattered relocs into `R_PPC_HA16_PIC` / `R_PPC_LO16_PIC` for
the linker — but only when the SECTDIFF target was a
"discarded" section (a `__symbol_stub1` or `__nl_symbol_ptr`
slot from libSystem-style stubs). For other targets — i.e.,
real merged sections like `__bss` — the code at line 5576
hit:

```c
if (ctx->sec_to_tcc[target_sect_idx])
    goto skip_pair_after;
```

and silently dropped the reloc translation. The .o-internal
displacement remained baked into the instruction immediates
in the linked exe, so the addis/addi computed a bogus
address. `re_compile_pattern` read garbage as
`re_syntax_options`.

Fix: in the case where the SECTDIFF target lives in a real
merged tcc section, synthesize an anonymous local anchor
symbol pointing at the target's offset within that merged
section, and emit `R_PPC_HA16_PIC` / `R_PPC_LO16_PIC`
against the anchor (mirroring the `R_PPC_ADDR32` anchor
pattern already used elsewhere in `macho_translate_relocs`).
The linker's existing PIC fallback path
(`exe_resolve_section_relocs`, the `slot_idx < 0` branch)
then walks `all_sects[]` to find the target section,
computes `delta = target_va - anchor_va`, splits into
ha16/lo16, rewrites the LO16's `lwz` opcode to `addi`, and
patches the immediates. Also zeros the immediate field in
`s->data[off+2..off+3]` so the resolver writes a fresh
delta into clean bits rather than ORing with stale ones.

Net: `re_compile_pattern` now reads `re_syntax_options`
correctly; sed's regex compiler honours the syntax flags;
ERE `(...)` and BRE `\{n,m\}` work.

This bug class shows up any time tcc emits a forward
reference to an extern symbol that later gets tentatively
defined in the same TU. Other gnulib programs (gzip,
coreutils, ...) likely hit it too. The fix is general — it
covers `__bss`, `__data`, `__const`, and `__text` SECTDIFF
targets uniformly.

#### Demo

`demos/v0.2.40-sed.sh` downloads sed-4.8 source, pulls
leopard.sh's binpkg-cache for the `config.cache`, strips the
env-locked entries (including the gcc-4.9-flavored
`ac_cv_func_acl_*`), pins gnulib's known-bad `ac_cv_func`
values via env, runs `./configure --disable-dependency-
tracking --disable-acl` with `CC=tcc`, builds, and runs 7
functional checks across BRE / ERE / address ranges /
backrefs / intervals / line delete / append. PASS.

#### Regression

* `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` — fixpoint
  holds.
* `./scripts/run-tests2.sh` — 111 / 111.
* `make abitest-cc abitest-tcc libtest dlltest` in
  `tcc/tests/` — all pass.
* All v0.2.32–v0.2.39 demos still pass. Real-world demos
  (lua, bzip2, cJSON) still pass.

### v0.2.41-g3 — gzip 1.11 round-trips at all sizes

While trying to extend real-world program coverage to gzip
1.11, found that tcc-built gzip produced corrupt deflate
streams at inputs >= 64 KB. Self-decompression and system
`/usr/bin/gunzip` both reported CRC errors. Smaller inputs
(under ~63 KB) round-tripped fine.

#### Bisection

Per-`.o` swap test (replace one tcc-built `.o` with a
gcc-4.0-built version, re-link, retest): isolated the bug to
`deflate.o`.

Within `deflate.c`, the bug only triggered at input sizes
that crossed `WSIZE + MAX_DIST = 65274`, i.e. input >= 64 KB.
`fill_window`'s slide path runs only when `strstart` reaches
that threshold; under that threshold, the function only
computes `more` and calls `read_buf`. Compressible-data
test (`yes "abcdefgh" | dd bs=1024 count=N`) gave exactly:
input 63 KB and below → OK; 64 KB and above → CRC error.

Per-function swap (extracted `fill_window` into a separate
`gcc_fw.c` rebuilt with gcc-4.0, made `static ulg
window_size` non-static so the gcc copy could `extern` it,
linked the gcc fill_window in place of tcc's): bug
disappeared. So fill_window's tcc compilation was the
culprit.

#### Diagnosis

Reading the asm of `fill_window`'s slide loop revealed:

```
00000a18  lwz r3, 0xfff4(r31)   ; r3 = n
00000a1c  li  r4, 0x1
00000a20  slw r3, r3, r4         ; r3 = n*2
00000a24  addis r12, r30, 0x0
00000a28  lwz r12, 0x1820(r12)    ; r12 = &prev (PIC slot deref)
00000a2c  addi r4, r12, 0x0       ; r4 = &prev + 0  (!!!)
00000a30  add  r4, r4, r3         ; r4 = &prev + n*2
00000a34  lhz  r4, 0x0(r4)        ; r4 = prev[n] (NOT head[n])
00000a38  stw  r4, 0xfff0(r31)    ; m = r4
```

For `head[n]` where `head` is `#define head (prev+WSIZE)`,
the C expression decomposes to `*(prev + WSIZE + n)` —
pointer = prev, byte offset = `WSIZE * sizeof(Pos)` =
`32768 * 2` = `0x10000`, plus `n*2` runtime. tcc's load()
PIC path used `(extra_off & 0xffff)` to encode the D-form
immediate. With `extra_off = 0x10000`, this masked to 0 —
the high half disappeared. The `addi r4, r12, 0x0`
instruction added zero offset, leaving r4 pointing at
prev[0] instead of head[0]. The slide loop then iterated
over prev[] (not head[]) for both halves of the slide, so
head[] was never updated and prev[] got partial correct +
partial garbage updates. Subsequent hash lookups returned
stale offsets pointing into the post-slide window's lower
half, deflate emitted invalid back-references, gunzip
detected the corruption.

#### Fix

`ppc_adjust_extra_off(int addr_gpr, int extra_off)` helper:
when `extra_off` doesn't fit in 15-bit-signed (the D-form
immediate range), emit `addis addr_gpr, addr_gpr,
ha16(extra_off)` and return the lo16 portion. The caller
uses the returned (smaller) lo16 in the load/store
displacement, addr_gpr having been pre-adjusted.

Applied at the top of all four PIC paths in `ppc-gen.c`:

* `load()`'s extern-data PIC path (the `head[n]` read).
* `load()`'s `lis/addi`-with-LO-reloc path (when
  `extra_off != 0`, an addi materializes the full sym
  address; the typed load that follows uses `extra_off`).
* `load()`'s `!want_load` `lis/addi`-with-LO-reloc path
  (also computes a sym+off address for store-target use).
* `store()`'s extern-data PIC path (the `head[n] = ...`
  write). Plus `store()`'s `lis/addi`-with-LO-reloc path
  for completeness.

Net: gzip 1.11 round-trips at all input sizes, including
500 KB and `/etc/services` (~570 KB). System `/usr/bin/
gunzip` accepts tcc-built gzip's output.

#### Demo

`demos/v0.2.41-gzip.sh` builds gzip 1.11 with strict tcc
enforcement (same `leopard.sh-binpkg-cache` + env-pin
pattern as the sed demo, minus `--disable-acl` since gzip
doesn't use ACLs), confirms `CC=tcc` and `libSystem`-only
linkage, and runs round-trips at 10 sizes from 1 KB to
500 KB plus a real-world `/etc/services` round-trip,
verifying both self-decompression AND system gunzip
acceptance.

#### Regression

* Bootstrap fixpoint holds.
* tests2 111/111.
* abitest-cc 24/24, abitest-tcc 24/24, libtest, dlltest:
  all pass.
* All v0.2.32–v0.2.40 demos still pass. Real-world demos
  (lua, bzip2, cJSON, sed): all pass.

#### Notes for future work

The PIC load/store paths still have other places where
`extra_off & 0xffff` could be problematic if used at
extremes (LLONG `+4` offset added on top of a lo16-fitting
base; constant-propagated 32-bit offsets). The current fix
covers the cases gzip exposes; extending it to LLONG
high-half access and the `addend + 4` pattern in the
LLONG-store paths is a straightforward follow-up if a
real-world program surfaces it.

## Exit state

HEAD: TBD on commit. Tag `v0.2.40-g3` to be created.

## Open work for next session

The roadmap's next items, in rough priority:

1. **gzip 1.11 large-input codegen bug** — round-trips inputs
   up to ~4 KB but breaks at 64 KB+ with CRC and length
   errors. Bisect by switching individual .o's between gcc
   and tcc.

2. **Strict implicit-decl warning** — `-Werror=implicit-
   function-declaration` analog so AC_CHECK_FUNC fails compile
   when the function is undeclared, mirroring gcc. Would close
   the entire gnulib AC_CHECK_FUNC mis-detection class
   (`__freading`, `__fseterr`, `fdopendir`, etc.) without
   needing the per-func env pin.

3. **Python 2.7.18 `PyType_Ready` codegen bug** — bisect to
   TU/function. Multi-session likely.

4. **OSO STAB emission for gdb-on-Tiger** (roadmap #7.5) —
   ~200-400 lines, one session of careful work.

5. **Self-link diagnostics** (roadmap #7) — quality-of-life.

6. **AltiVec intrinsics** — multi-session, niche.

7. **Real bcheck.c port** — multi-session, unblocks 3 test
   stages.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents — single Opus context for the whole session.
