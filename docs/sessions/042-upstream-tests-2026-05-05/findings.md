# Findings — session 042

Surprises, ABI corners, and lessons that will matter in future
sessions.

## 1. Apple PPC32 ABI uses up to 13 FPR slots, not 8

`gfunc_prolog` and `gfunc_call` in `ppc-gen.c` cap FP-arg passing at
8 FPRs (f1..f8). That matches SysV/Linux PowerPC convention but is
*wrong* for Apple. Apple's PowerPC32 ABI is the AIX/PowerOpen
variant, which uses **f1..f13** for FP args. abitest's
`ret_8plus2double_test` (9 doubles + 1 GPR-passed struct) trips the
8-FPR cap with a hard `tcc_error("ppc-gen: parameters exceed 8 FPR
slots")`.

Bumping the cap to 13 isn't a one-liner: `NB_REGS` is sized for 8
FPRs (10..17), `RC_F(x)` lives in bits 12..19 of a 32-bit
class-mask, and `reg_classes[]` only enumerates 8 FP entries. To
extend, all of those need re-sizing in lockstep, plus the
prolog-spill area (currently `8 * 4` bytes) needs to hold up to
13 spilled FPRs. Documented as "structural item" in the roadmap.

## 2. `tcc -b -run` looks up `bcheck.o` by name, not from libtcc1.a

Our home-grown `scripts/run-tests2.sh` uses `NORUN=true` so tests2
goes through the `compile-then-./exe` path. Upstream `make test`
runs without that override and uses `tcc -run`. The two paths
behave differently for `-b` (bounds-check):

* NORUN compile path: `-b` → tcc emits `__bound_*` calls; the link
  resolves them from `lib-ppc.c` in libtcc1.a. ✓
* `-run` path: `-b` → tcc additionally calls `tcc_add_support(s1,
  "bcheck.o")` and `"bt-log.o"` from `tccelf.c::tcc_add_runtime`,
  which looks up the files BY NAME in the support directory (the
  `-B` argument). If they're missing, tcc bails before doing
  anything. ✗

These are *positional* lookups, not symbol lookups, so providing
the symbols inside libtcc1.a doesn't satisfy the check. The fix is
to ship the files themselves, even if they're empty stubs. The
actual `__bound_*` symbols still come from libtcc1.a — the .o
files just have to exist on disk.

This explains the gap between our internal "111/111 pass" and the
upstream `make test` reporting failures on the same three tests
(121, 122, 132) — we were silently dodging the `-b -run` path.

## 3. Tiger 10.4 dyld can't resolve undefined-weak symbols

Upstream `tcctest.c`'s `weak_test` declares `weak_v1` etc. as
`extern int __attribute__((weak))` (no definition). On Linux, ELF
weak undefs resolve to NULL at load time. On macOS, this works in
10.5+. **On 10.4 (Tiger), dyld bails before main() with "Symbol
not found: _weak_v1"** even with `-undefined dynamic_lookup` and
`MACOSX_DEPLOYMENT_TARGET=10.4`. It's a fundamental limitation of
the Tiger flat-namespace dynamic linker.

This blocked `make test.ref` (and therefore `test1`/`test2`/`test3`)
because the gcc-built `tcctest.gcc` couldn't load. The fix is to
gate the weak_test section off in `tcctest.c` for Apple PPC
(necessarily Tiger or 10.5 PPC — we cover both with `defined
__ppc__ || defined __ppc64__`).

## 4. `__builtin_*` and `alloca` aren't auto-provided on PPC

Other tcc backends pull `lib/builtin.c` and `lib/alloca.S` into
their `OBJ-$T` lists (via `$(COMMON_O)`). Our `OBJ-ppc-osx` was
hand-rolled and missed both. As a result, code that uses
`__builtin_ffs/clz/ctz/popcount/parity` or `alloca()` linked
fine when those symbols were in libc (e.g. `_ffs` is in Tiger
libSystem) but failed when they weren't. macOS specifically
**does not provide `_alloca` in libSystem** — alloca is a
compiler builtin on Apple. tcctest.c uses `alloca` explicitly,
so the error showed up immediately during test3.

The portable C reference implementations in upstream
`lib/builtin.c` work fine under tcc-on-PPC. For alloca we wrote
a tiny `lib/alloca-ppc.S` using the standard PPC leaf-function
trick (no prolog/epilog of its own; bumps sp, replicates back
chain, returns new_sp+32 so the caller can still call other
functions without stomping the alloca'd region).

## 5. `__fixsfdi` / `__fixunssfdi` were missing from lib-ppc.c

We had `__fixdfdi` (double → int64) and `__fixunsdfdi` (double →
uint64) but not the float variants. tcctest.c casts `(long
long)(some_float)` somewhere, which emits `__fixsfdi`. Trivial
one-liner: widen to double, forward.

## 6. `load()`'s `extra_off != 0` branch missed VT_STRUCT

`tccgen.c::struct_get_member_ptr` and similar paths produce
sym-relative loads with both a symbol and a non-zero offset (e.g.
`pts[3]` where `pts` is a global array of struct point). Our PPC
backend's `load()` had every other VT_BTYPE case in the
extra_off != 0 path but no VT_STRUCT — it errored out with "load
via sym+off of bt 0x7 not yet supported". The matching extra_off
== 0 case below already handled it correctly: "loading" a struct
value just means computing its address, so we just need an `addi
r, addr_gpr, extra_off` (since addr_gpr already contains &sym
after the existing addi at line 826-829).

This was a long-latent bug that didn't surface in tests2 because
none of those tests pass a struct from a global array through
varargs.

## 7. `ppc_pic_pairs` was a static-lifetime leak

The PIC pair side-table (mapping reloc-offset → anchor-offset for
SECTDIFF) is `tcc_realloc`'d on demand and never freed. The
existing `ppc_pic_pairs_reset()` only resets the count to allow
reuse across translation units. In MEM_DEBUG=2 builds this
showed up as "512 bytes leaked" (= 64 entries * 8 bytes/entry,
the initial allocation). Add a `ppc_pic_pairs_free()` that
actually frees, hooked into `tcc_delete()` under
`#ifdef TCC_TARGET_PPC`.
