# Handoff — end of session 052 (2026-05-07)

## TL;DR

One release shipped this session: **v0.2.37-g3**. Sixth release of
the evening. First half of the DWARF arc.

* **`tcc -c -g foo.c` now produces a Mach-O .o that dwarfdump
  reads.** Compile unit, types, line table, AT_producer = "tcc N.N"
  — all parse cleanly under Tiger's bundled dwarfdump.
* DWARF in the linked exe path (so lldb works on `tcc -o exe`
  output) is the natural follow-up — see "Open work" below.

* HEAD: `77def3b`.
* All commits and `v0.2.37-g3` tag on `origin/main`.

## Six-release evening summary (sessions 047–052)

| | Tag | Headline | Concrete win |
|---|---|---|---|
| 047 | v0.2.32-g3 | LD = IBM double-double | abitest-cc 22→24/24 |
| 048 | v0.2.33-g3 | libtcc callback PLT trampolines | upstream `libtest` |
| 049 | v0.2.34-g3 | Precision-correct dd arithmetic | LD precision polish |
| 050 | v0.2.35-g3 | libm LDBL128 redirect | test3 diff 45→13 |
| 051 | v0.2.36-g3 | `__builtin_fma` codegen | dd_two_prod 10→2 flops |
| 052 | v0.2.37-g3 | DWARF in `-c` .o | dwarfdump reads tcc .o |

All tests green at every step; bootstrap fixpoint holds; all
real-world demos pass.

## What landed since v0.2.36

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.37-g3 | `3ebc8b1`..`77def3b` (2) | 111/111 | **DWARF debug info in `-c` Mach-O output.** tccdbg.c PPC arch blocks (CIE: return-addr=65/LR, SP=r1, CAF=4, DAF=-4) plus target-endian write helpers (BE on PPC32 since DWARF follows target endianness); ppc-link.c learns R_PPC_REL32; ppc-macho.c's `classify_section` accepts `.debug_*` and maps each to `__DWARF,__debug_*` with `S_ATTR_DEBUG`, and the .o layout step keeps their `addr = 0` so cross-section R_PPC_ADDR32 relocations leave the addend in-place for the linker. |

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
| **DWARF in .o** | [`v0.2.37-dwarf-obj.sh`](../../../demos/v0.2.37-dwarf-obj.sh) | ✅ |

## Open work

### Closed across sessions 047-052

* ~~B1 — Apple PPC IBM double-double LD~~ ✅ v0.2.32.
* ~~libtest REL24 out of range~~ ✅ v0.2.33.
* ~~Lossy dd helpers~~ ✅ v0.2.34.
* ~~test3 LD-subnormal divergence (math.h LDBL128)~~ ✅ v0.2.35.
* ~~`__builtin_fma` not recognized / dd_two_prod 10-flop fallback~~
  ✅ v0.2.36.
* **First half of DWARF for PPC** ✅ v0.2.37 — `-c` .o path.

### DWARF — second half (suggested next pickup)

The exe path: `macho_output_exe` doesn't yet enumerate `.debug_*`
sections. To add:

1. **Section enumeration**: in `macho_output_exe`'s "find sections"
   loop (around line 1850), scan `s1->sections[]` for any with
   names matching `.debug_*` (or use `s1->dwlo .. s1->dwhi`) and
   add them to a parallel `dwarf_sects[]` array.

2. **Layout**: add the DWARF sections after `__LINKEDIT` in the
   exe layout. Their `vmaddr` should be 0 (debug data isn't
   loaded), but they DO need a file offset and file size.

3. **Relocation application**: when relocate_sections runs over
   DWARF sections, R_PPC_ADDR32 with target = .text resolves to
   `text.sh_addr + addend = real_runtime_va`. That's correct for
   exe DWARF — lldb reads VAs and translates to source via
   .debug_line. Should work as-is once enumeration includes them.

4. **Load command**: emit an LC_SEGMENT for `__DWARF` with
   vmaddr=0/vmsize=0/file_offset/file_size, plus per-section
   headers with S_ATTR_DEBUG. Mirror the existing __TEXT /
   __DATA / __LINKEDIT segment-write code at line 2545+.

Estimated scope: 100-200 lines in `macho_output_exe`. Probably
one session.

### After DWARF works in exe

* **Per-prolog CFI** so lldb can unwind tcc-built frames properly.
  Currently the CIE has minimal initial state (CFA=r1+0); after
  stwu the actual CFA is r1+frame_size. Real fix: emit per-FDE
  DW_CFA_advance_loc + DW_CFA_def_cfa_offset annotations
  describing the prolog's frame moves. Requires sleb128-encoded
  offsets and per-insn advance_loc bookkeeping keyed off
  ppc-gen.c's variable-length prolog backfill.

* **DWARF version negotiation**: tcc emits DWARF-4 by default;
  Tiger's dwarfdump fully understands DWARF-2/3 but only
  partially DWARF-4 (warns "Unknown DW_FORM constant: 0x17" =
  DW_FORM_sec_offset). The `-gdwarf-2` flag works as a workaround;
  could default to DWARF-2 or 3 on PPC for tooling
  compatibility.

### Other open items

* **libtcc thread safety** (OOS per CLAUDE.md).
* **Real bcheck.c port** — multi-session, unblocks 3 test stages.
* **AltiVec intrinsics** — multi-session, niche.
* **Self-link diagnostics** — small QoL.
* **test3 structural curation** — 13 lines of intentional
  divergence (Apple ABI alignof, _Bool size, UB cast).

## How to pick up

### Sanity baseline

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh                          # 111/111
./demos/v0.2.32-longdouble.sh                    # PASS
./demos/v0.2.33-libtcc-callback.sh               # PASS
./demos/v0.2.34-dd-precision.sh                  # PASS
./demos/v0.2.35-libm-ldbl128.sh                  # PASS
./demos/v0.2.36-builtin-fma.sh                   # PASS
./demos/v0.2.37-dwarf-obj.sh                     # PASS
cd tcc/tests
PATH=/opt/make-4.3/bin:$PATH /opt/make-4.3/bin/make abitest-cc abitest-tcc libtest dlltest
# all pass
```

### Try the new DWARF directly

```sh
echo 'int sq(int n) { return n*n; } int main(void) { return sq(5); }' > /tmp/d.c
./tcc/tcc -c -gdwarf-2 -B./tcc -L./tcc -I./tcc/include /tmp/d.c -o /tmp/d.o
dwarfdump /tmp/d.o
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents in any of sessions 047-052. Six releases over one
evening, all green; the "deep dive on DWARF" arc completed half
of its scope (the .o path) cleanly. The exe path is genuinely a
separate session worth.

## Closing notes

Six releases. Two non-trivial bugs hit this session and worked
through:

1. **DWARF endian.** Tcc's BE target wrote LE DWARF, which Tiger's
   dwarfdump on a BE Mach-O read as garbage. The "empty contents"
   symptom was easy to misdiagnose as "tcc didn't emit anything"
   until I dumped the bytes with `otool -s`. Solved by routing all
   DWARF integer writes through target-endian helpers.

2. **Pre-applied .o relocations.** Tcc's macho writer applies
   relocations during .o write (in-place value gets section base
   added). For DWARF sections, that meant the cross-section
   abbrev_offset, str_offset, etc. all had pre-link section bases
   baked in — dwarfdump tried to seek to garbage offsets and
   bailed out. Solved by leaving DWARF sections' addr = 0 in the
   layout step, so R_PPC_ADDR32 applied to them resolves to
   "0 + addend = addend" — the standard .o convention.

The exe-side DWARF is structured similarly but routes through
`macho_output_exe`'s dedicated layout / relocation code, which
hasn't been touched. Picking up there is the obvious next session.
