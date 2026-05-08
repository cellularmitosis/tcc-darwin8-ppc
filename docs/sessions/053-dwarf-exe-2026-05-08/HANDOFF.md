# Handoff — end of session 053 (2026-05-08)

## TL;DR

Two releases shipped: **v0.2.38-g3** (DWARF debug info in linked
Mach-O exe) and **v0.2.39-g3** (`__eh_frame` + per-prolog CFI;
default DWARF version → 2 on Darwin). The DWARF arc that opened
in v0.2.37 is now functionally complete on the dwarfdump / lldb
side. `tcc -g foo.c -o foo` produces a binary whose
`__DWARF,__debug_*` and `__TEXT,__eh_frame` sections both read
clean.

* HEAD: `d0d749a`.
* All commits and `v0.2.38-g3` / `v0.2.39-g3` tags on `origin/main`.

## What landed since v0.2.37

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.38-g3 | `a8a36f4`..`d1216ba` (2) | 111/111 | **DWARF debug info in linked Mach-O exe.** macho_output_exe walks `s1->sections[]` for `S_ATTR_DEBUG`, lays them into a `__DWARF` segment after `__LINKEDIT` (vmaddr=0/vmsize=0; real file_offset/file_size; per-section S_ATTR_DEBUG headers). Cross-DWARF R_PPC_ADDR32 → "0+addend"; DWARF→.text R_PPC_ADDR32 → text_runtime_va+addend. Adds R_PPC_REL32 case to `exe_resolve_section_relocs` (used by `.eh_frame` FDEs); fixes matching addend-loss in ppc-link.c::relocate. |
| v0.2.39-g3 | `aeb1e2a`..`43b0c8a` (2) | 111/111 | **`__eh_frame` + per-prolog CFI in linked exe; default to DWARF-2.** Three coupled changes: (a) tcc.h enables TCC_EH_FRAME for TCC_TARGET_MACHO; tccelf.c keeps unwind_tables on for non-ELF + -g; classify_section maps .eh_frame → __TEXT,__eh_frame; macho_output_exe lays out + relocates + emits eh_frame with the standard plumbing. (b) tccdbg.c PPC FDE block emits per-FDE `advance_loc 96; def_cfa_offset frame_size; offset_extended 65, (frame_size-8)/4`. ppc-gen.c stashes frame_size in `ppc_last_frame_size`. ppc-link.c marks R_PPC_REL32 as NO_GOTPLT_ENTRY. (c) Mid-section .eh_frame terminator strip: each input .o ends its eh_frame with a 4-byte zero record, the merger left them embedded mid-section, dwarfdump stopped at the first one. Walks records, drops length=0, fixes up FDE CIE_pointers, re-appends a single clean terminator. Bonus: configure now defaults `-g` to DWARF-2 on Darwin (Tiger tools fully understand DWARF-2; only partially DWARF-4). |

Also pushed: docs/roadmap.md updated with v0.2.38 + v0.2.39
entries; the DWARF item retired from the "larger scope" list.

## Real-world programs

| Program | Demo | Status |
|---|---|---|
| Lua 5.4.7 | [`v0.2.12-lua.sh`](../../../demos/v0.2.12-lua.sh) | ✅ |
| zlib 1.3.1 | (no demo) | ✅ |
| bzip2 1.0.8 | [`v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) | ✅ |
| cJSON 1.7.18 | [`v0.2.21-cjson.sh`](../../../demos/v0.2.21-cjson.sh) | ✅ |
| HTTP server | [`v0.2.21-httpd.c`](../../../demos/v0.2.21-httpd.c) | ✅ |
| sqlite3 (file) | [`v0.2.23-sqlite-file.sh`](../../../demos/v0.2.23-sqlite-file.sh) | ✅ |
| Tiger libz via tcc -lz | [`v0.2.26-link-dylib.sh`](../../../demos/v0.2.26-link-dylib.sh) | ✅ |
| LD ABI tcc-vs-gcc match | [`v0.2.32-longdouble.sh`](../../../demos/v0.2.32-longdouble.sh) | ✅ |
| JIT callback into host | [`v0.2.33-libtcc-callback.sh`](../../../demos/v0.2.33-libtcc-callback.sh) | ✅ |
| dd-precision invariants | [`v0.2.34-dd-precision.sh`](../../../demos/v0.2.34-dd-precision.sh) | ✅ |
| libm LDBL128 redirect | [`v0.2.35-libm-ldbl128.sh`](../../../demos/v0.2.35-libm-ldbl128.sh) | ✅ |
| `__builtin_fma` codegen | [`v0.2.36-builtin-fma.sh`](../../../demos/v0.2.36-builtin-fma.sh) | ✅ |
| DWARF in .o | [`v0.2.37-dwarf-obj.sh`](../../../demos/v0.2.37-dwarf-obj.sh) | ✅ |
| **DWARF in linked exe** | [`v0.2.38-dwarf-exe.sh`](../../../demos/v0.2.38-dwarf-exe.sh) | ✅ |
| **__eh_frame + per-prolog CFI** | [`v0.2.39-eh-frame.sh`](../../../demos/v0.2.39-eh-frame.sh) | ✅ |
| **GNU gzip 1.11** | [`v0.2.39-gzip.sh`](../../../demos/v0.2.39-gzip.sh) | ✅ |
| **GNU sed 4.8** | [`v0.2.39-sed.sh`](../../../demos/v0.2.39-sed.sh) | ✅ |

Real-world build attempts this session: gzip 1.13 + dash 0.5.12
both hit gnulib/autoconf shim bugs that aren't tcc's fault (the
1.13 gnulib `stdlib.h` shim transitively includes
`__darwin_mcontext64_t` from the system header; dash's `mknodes`
build helper uses the configured CC and chokes on `<stdarg.h>`
recursion). **gzip 1.11** and **sed 4.8** (both from leopard.sh)
both built clean with the binpkg-bundled `config.cache` trick —
sixth and seventh real-world programs verified end-to-end (lua,
zlib, bzip2, cJSON, sqlite, gzip, sed).

The cache trick generalizes across leopard.sh's catalog. Programs
that fail with "`stdio_ext.h` not found" or "implicit declaration
of `__freading`" need a config.cache that pre-records the absent
glibc-isms as `=no`. The leopard.sh `tiger.cache` +
`tiger.32.cache` base caches give optimistic `=yes` defaults that
gcc-4.0's `-Werror=implicit-function-declaration` corrects during
the AC_CHECK_FUNC re-probe; tcc's permissive implicit-decl
handling accepts the cached `=yes` and the build breaks at compile
time when gnulib's `fseterr.h` tries to include the missing
`stdio_ext.h`. Each leopard.sh binpkg bundles a `config.cache`
with the corrected values baked in; pulling that cache out of the
binpkg via `tar xzf <pkg>.tiger.g3.tar.gz <pkg>/share/tiger.sh/<pkg>/config.cache.gz`,
gunzipping, and stripping env-locked entries
(`grep -v -E "^ac_cv_env_|..."`) gives a tcc-friendly cache. The
demo scripts for gzip and sed both follow this pattern — easy to
clone for the next real-world program demo.

## Open work

### Closed across sessions 047-053

* ~~Apple PPC IBM double-double LD~~ ✅ v0.2.32.
* ~~libtest REL24 out of range~~ ✅ v0.2.33.
* ~~Lossy dd helpers~~ ✅ v0.2.34.
* ~~test3 LD-subnormal divergence (math.h LDBL128)~~ ✅ v0.2.35.
* ~~`__builtin_fma` not recognized / dd_two_prod 10-flop~~ ✅ v0.2.36.
* ~~DWARF in `-c` .o output~~ ✅ v0.2.37.
* ~~DWARF in linked exe~~ ✅ v0.2.38.
* ~~`__eh_frame` + per-prolog CFI~~ ✅ v0.2.39.
* ~~Default DWARF version (Tiger tooling compatibility)~~ ✅ v0.2.39.

### DWARF — final mile (gdb-on-Tiger)

`dwarfdump` and `lldb` (later macOS releases) read tcc's emitted
DWARF directly. Apple's `gdb 6.3` on Tiger does NOT — it expects
either:

1. **OSO STAB entries** in the linked exe pointing back at the
   .o files (Apple's "debug map" convention). `dsymutil exe`
   then reads the OSO entries and pulls DWARF from each .o into
   a `.dSYM` bundle. `gdb exe` or `gdb exe.dSYM` then
   step-debugs.
2. **A separate `.dSYM` bundle** sitting next to the exe, with
   the DWARF wrapped in a Mach-O wrapper.

Either path requires non-trivial scaffolding:

* OSO STABs: needs `.o` files to persist (tcc compiles in-memory
  for `tcc -g foo.c -o foo`; would need to either write a temp
  `.o` or skip OSO emission for in-memory compiles). Plus
  per-symbol N_OSO/N_FUN/N_BNSYM/N_ENSYM emission keyed off
  which `.o` each function originated in. ~200-400 lines, one
  session of careful work.
* `.dSYM` generation: a separate Mach-O writer that wraps the
  in-memory DWARF as a standalone debug bundle. tcc owns the
  pipeline end-to-end. Probably similar scope.

Either is the obvious next session pickup if step-debugging on
Tiger is desired.

### Other open items

* **#7 Self-link diagnostics** — when the EXE writer fails the
  user gets opaque dyld errors with no context. Better
  pre-checks would shorten future debugging.
* **#9 AltiVec intrinsics** — multi-session, niche.
* **#11 Real bcheck.c port** — multi-session, unblocks 3 test
  stages.
* **test3 structural curation** — 13 lines of intentional
  divergence (Apple ABI alignof, _Bool size, UB cast).

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
git fetch https://github.com/cellularmitosis/tcc-darwin8-ppc.git main
git reset --hard FETCH_HEAD          # only safe if working tree clean
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh   # FIXPOINT HOLDS
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.32-longdouble.sh                 # PASS
./demos/v0.2.33-libtcc-callback.sh            # PASS
./demos/v0.2.34-dd-precision.sh               # PASS
./demos/v0.2.35-libm-ldbl128.sh               # PASS
./demos/v0.2.36-builtin-fma.sh                # PASS
./demos/v0.2.37-dwarf-obj.sh                  # PASS
./demos/v0.2.38-dwarf-exe.sh                  # PASS
./demos/v0.2.39-eh-frame.sh                   # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### Try the new DWARF/CFI directly

```sh
echo 'int sq(int n){return n*n;} int main(void){return sq(5);}' > /tmp/d.c
./tcc/tcc -g -B./tcc -L./tcc -I./tcc/include /tmp/d.c -o /tmp/dexe
dwarfdump /tmp/dexe                # compile unit, types, line table
dwarfdump --eh-frame /tmp/dexe     # CIE + per-prolog CFI per FDE
otool -l /tmp/dexe | grep sectname # __DWARF + __eh_frame visible
/tmp/dexe; echo $?                 # 25
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. Two releases over one evening; the DWARF arc is
done from a "tools that read DWARF can read tcc binaries"
perspective. The remaining gap (Apple gdb 6.3 step-debugging on
Tiger) is its own arc — see "DWARF — final mile" above.

## Closing notes

Three substantive bugs hit this session and worked through:

1. **TCC_EH_FRAME guard.** `tcc.h` excluded `ELF_OBJ_ONLY`
   targets — including Mach-O — so the per-FDE machinery never
   compiled in. Added `|| defined TCC_TARGET_MACHO`. The runtime
   ELF-only paths (`.eh_frame_hdr`, `dl_iterate_phdr`
   registration) only fire on the dynamic-link ELF code path,
   so they don't trigger under Mach-O — adding the macro is
   safe. The diagnostic fingerprint was: a debug printf in
   `tcc_eh_frame_start` never fired even with `-g
   -fasynchronous-unwind-tables`, which only made sense if the
   function wasn't compiled in at all.

2. **R_PPC_REL32 not in gotplt_entry_type.** Once REL32 first
   appeared in real .o relocs (the moment `.eh_frame` made it
   to the link), `build_got_entries` complained "Unknown
   relocation type for got: 26" because `gotplt_entry_type`
   returned -1 for REL32. Marked REL32 as `NO_GOTPLT_ENTRY`
   (the relocator handles it directly; no GOT/PLT involvement).
   Test `125_atomic_misc` regressed until this was added.

3. **Mid-section .eh_frame terminators.** Every input `.o` that
   tcc compiled with `unwind_tables=1` (every `.o` from `tcc -c`,
   since `output_format=ELF` for OBJ output) ends its `.eh_frame`
   with a 4-byte zero record. The section merger naively
   concatenates, leaving the chunks' inner terminators
   mid-section. dwarfdump and lldb both stop at the first one;
   Tiger's dwarfdump Bus-errored continuing past it. Solved with
   a post-reloc cleanup pass in `macho_output_exe` that strips
   length=0 records and adjusts FDE `CIE_pointer` values for the
   shifts. The strip happens after reloc resolution so reloc
   `r_offset`s aren't invalidated.

The "obvious next session" is OSO STABs / `.dSYM` generation if
the goal is gdb-on-Tiger step-debugging. Otherwise: AltiVec,
bcheck, or another real-world program build.
