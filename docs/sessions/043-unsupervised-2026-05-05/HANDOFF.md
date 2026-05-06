# Handoff — end of session 043 (2026-05-05)

Captured for the next session. Resume cold from this doc.

## TL;DR

* HEAD: `ec7f4a5` (httpd demo).
* Latest release: **v0.2.21-g3** (5 patch releases shipped this
  session: v0.2.17, v0.2.18, v0.2.19, v0.2.20, v0.2.21).
* tests2: still **111 / 111 (100%)**.
* Bootstrap fixpoint still holds.
* tcctest.c (4500-line stress test) runs to completion every
  time; diff vs gcc reference is **44 lines** (was 255 at the
  start of session 043), all remaining are known structural
  items (see "Remaining diff" below).
* upstream `make test` improvements:
  * `abitest-tcc` jumped from 5 → 14 passing (13-FPR support
    unblocked the whole post-`ret_8plus2double` cluster).
  * `memtest` passes (since session 042's leak fix).
  * `tests2-dir` 111/111 under `-run` (since session 042's
    bcheck.o/bt-log.o stubs).
* **Four real-world programs working end-to-end**: lua, zlib,
  bzip2, cJSON (one per session 042/043 release).
* **First interactive network program**: a tcc-built HTTP
  server (`demos/v0.2.21-httpd.c`).
* All commits and tags on `origin/main`.

## What landed since session 042 close (v0.2.16-g3)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.17-g3 | `e330cea`..`d2565cf` (3) | 111/111 | **alloca() actually works.** Epilog back-chain restore + 256-byte safety zone in new `alloca-ppc.S`. Full tcctest now runs to completion (was SEGV at line 770/4500 in stdarg_test). |
| v0.2.18-g3 | `bf6c7a7`..`f30731d` (4) | 111/111 | Variadic FP-past-8 off-by-one + `fneg` for big-endian. **bzip2 1.0.8 builds + round-trips.** |
| v0.2.19-g3 | `db9114a`..`9f7d244` (3) | 111/111 | NaN-aware FP `>=`/`<=` + ptr→int cast follows dst signedness + LL straddle gslot=7. tcctest diff 122 → 44 lines. |
| v0.2.20-g3 | `3fcdf34`..`f978034` (3) | 111/111 | **Apple PPC ABI 13-FPR support.** abitest-tcc jumped 5 → 14 passing. Plus libgcc helper stubs for tcc-built programs that link gcc-built objects. |
| v0.2.21-g3 | `a323c59`..`ec7f4a5` (2) | 111/111 | `__builtin_fabs/inf` math stubs (Tiger `<math.h>` macros need them). **cJSON 1.7.18 builds + parses.** |

Roadmap items closed: T3.1 (13-FPR support).

## Bugs fixed in detail

### 1. alloca() corruption + epilog can't recover from moved sp (v0.2.17)

Two coupled bugs that together broke any function that called
alloca() and then made any further call:

* The `gfunc_epilog` used `addi r1, r1, frame_size` to restore SP,
  which assumes r1 hasn't moved during the function body. alloca
  drops r1 lower; this addi then puts r1 in the wrong place. New
  epilog uses back-chain restore (`lwz r1, 0(r1)`) and saved-reg
  loads via FP (r31, which alloca never touches).

* `lib/alloca-ppc.S`'s safety zone between the new SP and the
  user's region was 32 bytes — only enough for the 24-byte
  linkage area. Subsequent `printf(...args...)` in the same
  function spilled outgoing GPR args to `r1+24+slot*4`, landing
  inside the alloca'd region and clobbering user data. Bumped to
  256 bytes.

### 2. Variadic FP arg past 8th FPR off-by-one (v0.2.18)

In `gfunc_call` for an FP arg with `fslot >= 8`, the code did
`fpr = TREG_TO_FPR(vtop->r) + 1`. But `TREG_TO_FPR` already
returns the 1-indexed PPC FPR number (f1..f8) -- the extra `+ 1`
made us emit an stfd that stored a *different* FPR than the one
gv() loaded the value into. Surfaced by printf with 8 ints + 10
doubles where args 9 and 10 came out as huge garbage values.

### 3. Float negation on big-endian (v0.2.18)

The generic `gen_negf` in tccgen.c negates a float by reading the
byte at offset (size-1) from the value's address and XOR-ing with
0x80. This is little-endian only -- on big-endian PPC the sign
bit lives in byte 0, not byte 7. Solution: route TOK_NEG through
PPC's `gen_opf` for PPC, which emits a real `fneg` instruction.

### 4. NaN-aware FP `>=` and `<=` (v0.2.19)

Per IEEE: any compare involving NaN returns false except `!=`.
Our `gjmp_cond` for FP `>=` tests "branch when LT bit clear",
but for NaN-vs-anything cr0 has only the FU/SO bit set (LT and
GT both clear). The inverted test fired on NaN, returning true.
Fix: emit `cror cr0_LT, cr0_LT, cr0_FU` after fcmpu for `>=`
(and the GT-equivalent for `<=`) so the inverted-bit test
correctly returns false on unordered.

### 5. Pointer → wider int cast follows destination signedness (v0.2.19)

`(unsigned long long)(char*)0xFFFFFFBE` was sign-extending to
0xFFFFFFFFFFFFFFBE. gcc on Apple PPC zero-extends to
0x00000000FFFFFFBE here, following the destination's signedness.
Fixed by `sbt = (sbt & ~VT_UNSIGNED) | (dbt & VT_UNSIGNED)` in
the pointer-cast branch of `gen_cast`.

### 6. Long-long arg straddling r10/stack ABI boundary (v0.2.19)

For an LL arg with gslot=7 (HIGH in r10, LOW spilled to stack),
the inner `gslot + 1 < 8` check was false, so we fell through to
`gv(RC_R(gslot))` which loaded only ONE half (LO) into r10 and
silently dropped HIGH and stack-LO. Added an explicit branch
that materializes as a register pair, mr's HIGH into r10, and
stw's LOW to caller_r1+24+(gslot+1)*4.

### 7. Apple PPC ABI 13-FPR support (v0.2.20)

Apple PPC32 ABI uses f1..f13 for FP arg passing (the AIX/
PowerOpen variant), not f1..f8 (SysV/Linux). Bumped NB_REGS
18 → 23, RC_F bitmask range, reg_classes[], prolog FP-spill
area, fpr_index/fslot caps, ppc_fp_param_off / is_double array
sizes. abitest-cc::ret_8plus2double_test now passes (had been
failing since v0.2.0).

### 8. libgcc helper stubs (v0.2.20, v0.2.21)

Tiger libSystem doesn't ship `__gcc_qadd / qsub / qmul / qdiv`
(IBM double-double arithmetic), `__cmpdi2 / __ucmpdi2` (64-bit
int compare), `__builtin_fabs / fabsf / inf / inff` (gcc math
builtins). Programs that link gcc-built objects or include
Tiger's `<math.h>` (which uses these via macros) hit "Symbol
not found". Added stubs to `lib/lib-ppc.c`. The double-double
helpers fall back to plain double arithmetic (lossy but
link-compatible since our PPC tcc treats long double as plain
8-byte double).

## Remaining tcctest.c diff (44 lines, all known)

* `aligntest3 sizeof=16 alignof=4` (vs gcc's 8). Apple PPC ABI
  alignment for double in structs — we use 4-byte (per session
  041 fix). Won't change.
* `aligntest4 sizeof=0 alignof=4` (same reason).
* `promote char/short funcret 137 -1` (vs gcc's
  `-1987475063 -1`). Undefined behavior in tcctest's `csf`
  macro — we're more conservative (proper truncation), gcc-4.0
  is implementation-defined garbage. Won't change.
* `promote char/short fumcret VA 52685 -4113 0 1` (similar UB).
* `sizeof(_Bool) = 1` (vs gcc's 4). gcc-4.0 quirk; we follow
  C99. Won't change.
* `relocation_test *rel1=1 *rel2=1` (deferred — see Open work).
* (a few minor float-format-precision differences with `1.000000`
  vs `0.000000` in last-position long-double formatting, all in
  the float_test section; LD-double-gate took most of these).

## Open work (in roughly decreasing impact)

### A. Bugs we identified but couldn't crack

1. **`relocation_test` static-init `&arr[N]` addend** (4 attempts,
   all broke bootstrap). The in-place value's contents differ
   between the in-memory single-TU path (init_putv writes
   user-addend only, then NOTHING patches it) and the .o roundtrip
   path (somewhere in tcc's post-compile pipeline, in-place gets
   sym->st_value added). Three different formulas tried; all hit
   the same wall. The right fix probably needs a pre-link
   normalization pass that aligns in-place values across loaded
   sections, OR a small change in init_putv that ALSO adds
   sym->st_value. Both would need careful handling of section
   concatenation offsets (`sm->offset` in tccelf.c::3635). See
   ppc-macho.c around line 1540 for the TODO comment.

2. **abitest-tcc post-`reg_pack_longlong_test` Bus error**
   (`sret_test` and beyond). Standalone reproducer with the
   exact sret pattern works fine — the bug is specific to the
   abitest harness's libtcc-JIT context. Likely a JIT-context
   ABI subtlety (e.g., the JIT'd memory mapping doesn't match
   what the harness's call expects). Worth a deeper bisect.

3. **sqlite CREATE TABLE → SQLITE_CORRUPT** (deferred from
   session 041, retested unchanged). Pinpointed to
   `OP_ParseSchema` finding 0 rows in sqlite_schema after the
   CREATE. VDBE-level bug — needs single-stepping the bytecode.

4. **sqlite file open SEGV** (deferred from session 041, retested
   unchanged). Wild jump via bad function pointer. Could be
   related to #1 (static-init addend) but also might be a
   different codegen issue.

### B. Structural items (each is a multi-hour or multi-session lift)

5. **Mach-O `-shared` dylib output**. Currently only `-c` and
   `-o exe`. Unblocks `dlltest` and many real-world programs
   that ship as .dylib. Substantial work.

6. **Mach-O `tcc -ar` driver**. Currently fall back to system
   `ar`. ~150-300 lines, well-scoped (port `tcctools.c`'s
   ELF-based ar to handle Mach-O `.o` symbol extraction +
   `__.SYMDEF SORTED` BSD-style index).

7. **DWARF debug info for PPC**. tccdbg.c has no PPC blocks.
   Multi-target plumbing (~10 places need PPC additions).
   Unblocks gdb / lldb debugging of tcc-built programs.

8. **128-bit IBM double-double long double**. Apple PPC `long
   double` is 16-byte; we treat as 8-byte double. Needs ABI
   plumbing + arithmetic helpers. Multi-session.

9. **Real bcheck.c port**. Our stubs satisfy link but don't
   actually detect overruns. Multi-session.

### C. Multi-thread / advanced

10. **`libtest_mt` SEGV**. Multi-threaded libtcc usage.

11. **AltiVec intrinsics**. None today; tcc emits scalar code.

## Real-world programs

| Program | Demo | Status |
|---|---|---|
| Lua 5.4.7 | [`v0.2.12-lua.sh`](../../../demos/v0.2.12-lua.sh) | ✅ Builds + runs |
| zlib 1.3.1 | (no separate demo) | ✅ Builds + full test suite passes |
| bzip2 1.0.8 | [`v0.2.18-bzip2.sh`](../../../demos/v0.2.18-bzip2.sh) | ✅ Builds + round-trips byte-identical |
| cJSON 1.7.18 | [`v0.2.21-cjson.sh`](../../../demos/v0.2.21-cjson.sh) | ✅ Builds + parses |
| HTTP server (own) | [`v0.2.21-httpd.c`](../../../demos/v0.2.21-httpd.c) | ✅ BSD sockets work |
| sqlite3 (in-memory SELECT) | (no separate demo) | ⚠️ SELECT works; CREATE TABLE corrupt; file open SEGV |

## How to pick up

### Quick sanity check
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
git log -1 --oneline                    # should match origin/main
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh   # fixpoint should hold
./scripts/run-tests2.sh                       # 111/111
./demos/v0.2.18-bzip2.sh                      # PASS at the end
./demos/v0.2.21-cjson.sh                      # name=alice, sum=15
```

### tcctest single-pass
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc/tcc/tests
../tcc -B.. -I../include -I.. -I.. -w -o /tmp/tcctest tcctest.c
/tmp/tcctest > /tmp/test.outexe 2>&1
diff -u test.ref /tmp/test.outexe | wc -l    # 44 lines
```

### Upstream `make test`
```sh
cd ~/tcc-darwin8-ppc/tcc/tests
/opt/make-4.3/bin/make -k test 2>&1 | tail -100
```

## Subagent log

Considered using a Sonnet subagent twice this session, both for
mechanical bound-stub additions. Decided against both times — the
work was 5-10 lines each and faster to do directly than brief a
subagent. No subagents spawned in session 043.

If a future session has long-running mechanical work (e.g.
porting bcheck.c, adding many DWARF arch-specific blocks), Sonnet
would be a reasonable choice.

## Closing notes

This session shipped 5 patch releases, fixed 8+ codegen bugs,
unlocked 2 new real-world programs (bzip2, cJSON), and proved
network-server programs work end-to-end. The tcctest.c diff went
from 255 → 44 lines — the remaining 44 are all in "won't fix"
buckets (Apple ABI / gcc quirks / undefined behavior).

The most impactful next single item is the relocation_test
addend bug (4 attempts and counting). After that, the structural
items (Mach-O dylib output, tcc -ar, DWARF) all unlock specific
classes of programs.

Bootstrap fixpoint and tests2 100% throughout the session — no
regressions.
