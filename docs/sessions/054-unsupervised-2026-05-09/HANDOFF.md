# Handoff — end of session 054 (2026-05-09)

## TL;DR

Five releases shipped this evening, taking tcc from v0.2.39 to
**v0.2.44**. Three real-world programs unblocked (sed 4.8, gzip
1.11, Python 2.7.18), four codegen / linker bugs fixed. tests2
holds at 111/111 throughout. Bootstrap fixpoint holds.

* HEAD: `f44255b` (post-Python-demo cleanup).
* All commits and `v0.2.40-g3` … `v0.2.44-g3` tags on
  `origin/main`.

## What landed

| Tag | Commits | Headline |
|---|---|---|
| v0.2.40-g3 | `5478279`..`87554c3` (5) | **GNU sed 4.8 works.** Three fixes: (1) VT_BOOL store/load via pointer in `ppc-gen.c`. (2) `__attribute__((gnu_inline))` recognised + `__GNUC_GNU_INLINE__=1` predefined on Apple — gnulib's `_GL_INLINE` now picks the gnu89-inline path tcc honours; without this, every TU including c-ctype.h tripped "defined twice" link errors. (3) PIC SECTDIFF reloc translation when the target is a non-discarded merged section: when tcc parses `regex.c` and sees `extern reg_syntax_t re_syntax_options` first then a tentative def of the same sym later, the .o writer baked the .o-internal SECTDIFF displacement into the addis/addi immediates, but the loader silently dropped the reloc translation when its target lived in a real merged section (BSS here) — leaving the wrong constant in the linked exe. `re_compile_pattern` then read 0 for the syntax flag, ERE `(...)` and BRE `\{n,m\}` patterns silently failed. Fix: synthesise an anonymous local anchor sym in `macho_translate_relocs`. |
| v0.2.41-g3 | `db78ee4`..`bcb2d8b` (2) | **GNU gzip 1.11 round-trips at all sizes.** Pre-fix, tcc-built gzip produced corrupt deflate streams at >= 64 KB inputs (CRC errors). Root cause: `(extra_off & 0xffff)` truncation in the 4 PIC load/store paths in `ppc-gen.c` — gnulib's gzip uses `#define head (prev+WSIZE)` with `WSIZE=32768`, byte offset = `WSIZE * sizeof(Pos) = 0x10000`, masked to 0. fill_window's slide loop updated prev[] twice and never head[]. Fix: `ppc_adjust_extra_off` helper that emits `addis addr_gpr, addr_gpr, ha16(extra_off)` for offsets > 15-bit signed, returns lo16. Applied at the top of all 4 affected PIC paths in `load()` and `store()`. |
| v0.2.42-g3 | `14532e7` | **Python 2.7.18 builds + runs end-to-end** — eighth real-world program. No new tcc fix; Python was unblocked by v0.2.41's `extra_off` fix as a side effect (PyTypeObject is large and its slot offsets exceed 15-bit signed). The demo runs a Python program exercising list comprehensions, recursion, classes/methods, string ops, dicts, exceptions, float formatting. |
| v0.2.43-g3 | `7ec15d9`..`0ae71c0` (2) | **`tcc -g` no longer SIGBUSes on `extern int v; int *p = &v;`** — NULL deref of `smap[j].elf->sh_num` in `classify_sym`'s section-lookup loop when smap entries were synthesized stub / la_ptr / nl_ptr (which have `.elf == NULL`). One-line NULL check matching the four other identical loops in the same file. Path: tccgen.c::init_putv → R_DATA_PTR reloc against extern sym → put_extern_sym2 → tcc_debug_extern_sym → DWARF DW_TAG_variable with DW_AT_location → at OBJ write time, classify_sym walks smap[] for the sym's section and crashes on NULL elf. |
| v0.2.44-g3 | `19acc6b`..`3063c84` (2) | **`tcc -g` linking cross-TU extern refs no longer produces SEGV-ing exes.** After v0.2.43 fixed the .o-build crash, the next problem appeared at link: `tcc -g -o exe a.o b.o c.o` produced an exe whose `*p` (extern data ref) read garbage. Root cause: in `macho_translate_relocs`, the SECTDIFF target-section lookup found the FIRST section whose `[addr, addr+size)` matched. With -g, debug sections (`__debug_info` etc.) start at addr=0 with large sizes and shadowed the real `__nl_symbol_ptr` (addr 0x180 size 4). `macho_resolve_stub_slot` then returned -1 with an out-of-range entry index; the SECTDIFF reloc was silently dropped. Fix: filter the section search to real merged tcc sections + stub-like sections (S_SYMBOL_STUBS / S_LAZY_SYMBOL_POINTERS / S_NON_LAZY_SYMBOL_POINTERS); debug sections are skipped. Side effect: Python 2.7.18 with -g enabled now works (the v0.2.42 demo dropped its `-g`-stripping workaround in commit `f44255b`). |

## Real-world programs

After this session: **eight working real-world programs**.

| Program | Demo | Status |
|---|---|---|
| Lua 5.4.7 | [`v0.2.12-lua.sh`](../../../demos/v0.2.12-lua.sh) | ✅ |
| zlib 1.3.1 | (no demo) | ✅ |
| bzip2 1.0.8 | [`v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) | ✅ |
| cJSON 1.7.18 | [`v0.2.21-cjson.sh`](../../../demos/v0.2.21-cjson.sh) | ✅ |
| sqlite3 (file) | [`v0.2.23-sqlite-file.sh`](../../../demos/v0.2.23-sqlite-file.sh) | ✅ |
| **GNU sed 4.8** | [`v0.2.40-sed.sh`](../../../demos/v0.2.40-sed.sh) | ✅ NEW |
| **GNU gzip 1.11** | [`v0.2.41-gzip.sh`](../../../demos/v0.2.41-gzip.sh) | ✅ NEW |
| **Python 2.7.18** | [`v0.2.42-python.sh`](../../../demos/v0.2.42-python.sh) | ✅ NEW |

## Open work for next session

The roadmap's remaining items, in rough priority:

### 1. OSO STAB emission for gdb-on-Tiger (roadmap #7.5)

`tcc -g` now emits clean DWARF that dwarfdump and lldb (later
macOS) read. Apple's `gdb 6.3` on Tiger doesn't read embedded
DWARF — it expects either:

* **OSO STAB entries** in the linked exe pointing back at the
  `.o` files. `dsymutil exe -o exe.dSYM` then reads the OSO
  entries, pulls DWARF from each .o into a `.dSYM` bundle, and
  `gdb exe` step-debugs.
* **Standalone `.dSYM` bundle** sitting next to the exe, with
  the DWARF wrapped in a Mach-O wrapper.

Either path requires non-trivial scaffolding:

* OSO: needs `.o` file paths kept in scope through linking. tcc
  compiles in-memory for `tcc -g foo.c -o foo`; would need to
  either write a temp `.o` or skip OSO emission for in-memory
  compiles. Plus per-symbol N_OSO/N_FUN/N_BNSYM/N_ENSYM emission
  keyed off which `.o` each function originated in.
  Format roughly:

      N_SO  source_directory       <addr>
      N_SO  source_basename        <addr>
      N_OSO object_file_full_path  <mtime>
        N_BNSYM                    <function_addr>
        N_FUN  funcname:F<type>    <function_addr>
        N_FUN  ""                  <function_size>
        N_ENSYM                    <function_addr>
      N_SO  ""                     <addr>

  ~200-400 lines, one careful session.

* `.dSYM` generation: a separate Mach-O writer that wraps the
  in-memory DWARF as a standalone debug bundle. tcc owns the
  pipeline end-to-end. Probably similar scope.

The OSO path is more useful: it integrates with existing Apple
toolchain (dsymutil), and lldb on later macOS also handles OSO
debug maps.

Implementation entry points:
* `ppc-macho.c::macho_output_exe` — where the linked exe's
  symtab is emitted.
* `ppc-macho.c::put_nlist` — already used to emit nlist entries.
* `ppc-macho.c::build_symtab` — currently doesn't emit STABs.
* Need a new mechanism to track .o file paths through linking
  (currently we lose the input filename once macho_load_object_file
  has consumed it).

### 2. Self-link diagnostics (roadmap #7)

When the EXE writer fails the user gets opaque dyld errors with
no context. Better pre-checks would shorten future debugging
considerably. Quality-of-life improvement.

### 3. AltiVec intrinsics (roadmap #9)

None today; tcc emits scalar code for everything. Plausible but
a large project — would unlock vector-using libraries.

### 4. Real bcheck.c port (roadmap #11)

Multi-session lift; unblocks 3 test stages currently classified
as skipped on PPC. Codegen hooks in `ppc-gen.c` partially exist
(`func_bound_offset` / `func_bound_ind` are unused statics);
the no-op stubs in `lib-ppc.c` need replacing with a real port.

### 5. (Other small bugs surfaced this session)

* `tcc -Werror=implicit-function-declaration` already works (it
  was always there, just not on by default). Could consider
  making it default-error in C99 mode (matches modern gcc / clang
  behaviour), but that's a behaviour change with breakage risk.
  Filed for discussion.

* The PIC load/store paths still have edge cases where the LLONG
  high-half access (`addend + 4`) might overflow into the high
  bits if the base addend is close to 0x7fff. Real-world programs
  haven't surfaced this yet; a paranoid fix would also apply
  `ppc_adjust_extra_off` to the `addend + 4` case in the LLONG
  store paths.

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
./demos/v0.2.40-sed.sh                        # PASS  (NEW)
./demos/v0.2.41-gzip.sh                       # PASS  (NEW)
./demos/v0.2.42-python.sh                     # PASS  (NEW)
./demos/v0.2.43-gdebug-extern.sh              # PASS  (NEW)
./demos/v0.2.44-gdebug-link.sh                # PASS  (NEW)
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### Try the new -g support directly

```sh
echo 'extern int v; int *p = &v;' > /tmp/data.c
echo 'int v = 42;' > /tmp/data_def.c
echo 'extern int *p; #include <stdio.h>
int main(){printf("*p=%d\n", *p); return *p == 42 ? 0 : 1;}' > /tmp/data_main.c
./tcc/tcc -g -B./tcc -L./tcc -I./tcc/include -c /tmp/data.c -o /tmp/data.o
./tcc/tcc -g -B./tcc -L./tcc -I./tcc/include -c /tmp/data_def.c -o /tmp/data_def.o
./tcc/tcc -g -B./tcc -L./tcc -I./tcc/include -c /tmp/data_main.c -o /tmp/data_main.o
./tcc/tcc -g -B./tcc -L./tcc -I./tcc/include -o /tmp/exe /tmp/data.o /tmp/data_def.o /tmp/data_main.o
/tmp/exe          # prints "*p=42"
echo $?           # 0
dwarfdump /tmp/exe | head -10   # readable DWARF
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents. Five releases over one long evening of unsupervised
work. The hard part of this session was reproducing and bisecting
the gzip / Python codegen bugs (which both turned out to be PIC
load/store offset truncation, surfacing differently); the easy
part was shipping the demos that prove they work.

## Closing notes

The codegen bugs fixed this session are a big deal:

1. The `extra_off` truncation (v0.2.41) had been silently
   miscompiling any program that referenced struct/array members
   at offsets > 0x7fff bytes — extremely common for large structs
   like `PyTypeObject`. The Python startup death was the loudest
   symptom, but anything substantial was probably suffering
   silent miscompilation in subtle ways.

2. The `-g` cross-TU extern-ref bug (v0.2.44) meant tcc-with-`-g`
   was effectively unusable for any multi-file program with extern
   data refs. The single-source-link path masked it.

After this session, tcc's codegen is meaningfully more robust on
real codebases. The DWARF emission is good enough to read with
modern tools (lldb, dwarfdump); only Apple gdb 6.3 still wants
its OSO STAB layout, which is the next session's pickup if we
want step-debugging on Tiger.
