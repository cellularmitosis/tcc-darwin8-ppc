# 026 — libgcc helpers in libtcc1.a

## Arrival state

Coming out of [025](../025-macho-o-reader/README.md), `tcc -o exe`
worked end-to-end for normal programs (auto-loading `/usr/lib/crt1.o`
and producing dyld-compatible Mach-O executables). But
`bootstrap-tcc-self.sh` still used `gcc-4.0` for the link step
because `tcc.c` references libgcc runtime helpers — `__udivdi3`,
`__ashldi3`, `__fixdfdi`, etc. — that our `libtcc1.a` didn't
provide. v0.2.0's plan called for bundling those helpers ourselves
so `tcc-self` builds with no `gcc-4.0` involvement at all.

## What landed

`tcc/lib/lib-ppc.c` was already wired into `OBJ-ppc-osx` as of
session 021, but contained only `__floatundidf`. This session
expanded it to ten helpers, all written as portable C against
explicit 32-bit halves (no `DWunion` struct trick — the union's
field-order is endian-dependent and the conventional naming in
`libtcc1.c` is wrong on big-endian PPC):

```
__floatundidf      unsigned long long -> double
__fixunsdfdi       double -> unsigned long long  (manual bit work)
__fixdfdi          double -> long long           (sign-flip via raw bits)
__ashldi3          long long << int
__lshrdi3          unsigned long long >> int
__ashrdi3          long long >> int               (arithmetic, sign-extend)
__udivdi3          unsigned long long / unsigned long long
__umoddi3          unsigned long long % unsigned long long
__divdi3           long long / long long
__moddi3           long long % long long
```

The signed variants reduce to the unsigned helpers via sign-split.
`__fixdfdi` deliberately avoids `-double` (we have a known PPC
codegen bug where `fneg` on a parameter double can corrupt the low
bits — see [019](../019-be-ll-fixes/README.md)) and instead flips
the sign bit in the raw IEEE 754 bit pattern.

After this session, `libtcc1.a` exports:

```
___floatundidf  ___fixunsdfdi  ___fixdfdi
___ashldi3      ___lshrdi3     ___ashrdi3
___udivdi3      ___umoddi3     ___divdi3   ___moddi3
```

Verified end-to-end on ibookg37: a test program that does both a
shifted long-long literal and a signed long-long divide compiles
through `tcc -o exe foo.c libtcc1.a` and runs correctly:

```
$ ./tcc/tcc -B./tcc -I./tcc -o /tmp/ll /tmp/ll.c ./tcc/libtcc1.a
$ /tmp/ll
shift=2468acf13579bde0
div=81985529
```

## What we tried, what didn't work, and what we punted

The original plan also called for dropping the `gcc-4.0` link from
`bootstrap-tcc-self.sh` once the helpers were in libtcc1.a. A
sub-agent dispatched for that scope did three things:

1. ✅ Implemented the helpers above (initial 7 — we added the
   missing `__divdi3`/`__moddi3`/`__ashrdi3` afterwards).
2. ✅ Edited `scripts/bootstrap-tcc-self.sh` to use `$TCC` for the
   link step instead of `gcc-4.0`.
3. ❌ Discovered that the resulting `tcc-self` crashes with SIGBUS
   before `main` runs, and spent the rest of its run trying to fix
   it with structural changes to `tcc/ppc-macho.c` (split
   `__nl_symbol_ptr` into lazy-vs-non-lazy sections;
   `MH_TWOLEVEL → MH_FORCE_FLAT` flag swap). Neither fix actually
   resolved the crash.

**The actual root cause** (caught after I took over): the crash is
a Tiger libc initialization-order bug. Crt1.o's `_start` calls
`__keymgr_dwarf2_register_sections` (DWARF unwind setup), which
calls `_dyld_register_func_for_add_image`, which synchronously
invokes the `dwarf2_unwind_dyld_add_image_hook` callback for
already-loaded images, which tries to `calloc` — but
`_malloc_initialize` hasn't run yet, so it dies with `EXC_BAD_ACCESS`:

```
#0  0x9012c6f0 in _malloc_initialize ()
#1  0x90008998 in calloc ()
#2  0x9002d264 in dwarf2_unwind_dyld_add_image_hook ()
#3  0x8fe026c0 in __dyld imageNotification ()
#4  0x8fe0de14 in __dyld ImageLoader runNotification ()
#5  0x8fe03728 in __dyld dyld registerAddCallback ()
#6  0x900010b8 in _dyld_register_func_for_add_image ()
#7  0x90189c10 in __keymgr_dwarf2_register_sections ()
#8  0x0006e3c8 in _start ()
```

This is structurally tcc-link-specific. The gcc-4.0 link path works
because gcc-4.0 also pulls in extra `.o` members from `libgcc.a`
that bring in `___keymgr_global`, `__init_keymgr`,
`__keymgr_get_and_lock_processwide_ptr`, etc. — i.e. a whole keymgr
support layer that initialises the DWARF/exception state in the
right order.

Comparing undefined symbols of the two binaries:

```
gcc-link tcc-self has 9 extra UNDEF symbols our tcc-link version doesn't:
  ___keymgr_global
  ___sF
  __dyld_register_func_for_add_image
  __dyld_register_func_for_remove_image
  __init_keymgr
  __keymgr_get_and_lock_processwide_ptr
  __keymgr_set_and_unlock_processwide_ptr
  _abort
  _calloc
```

We also noticed that *our* binary has duplicate UNDEF entries for
`___keymgr_dwarf2_register_sections`, `_atexit`, `_exit` — likely a
bug in the Mach-O reader's symbol coalescing path.

**Decision:** revert the agent's `ppc-macho.c` and bootstrap-script
changes. The lazy-pointer split is plausible architecture but
unverified, and `MH_FORCE_FLAT` is regressive. The actual fix for
self-link involves either (a) shipping our own `keymgr` glue
analogous to what `libgcc.a` contributes on the gcc-link path, or
(b) avoiding the `__keymgr_dwarf2_register_sections` call entirely,
e.g. by stubbing it out in our crt1 reader. That's its own session
(027 in the new sequence; the original "027 = testsuite baseline"
moves to 028).

## Side discovery: pre-existing fixpoint regression

While testing, I noticed that the byte-identical self-host fixpoint
that 020 achieved is **broken at HEAD**, with the exact same delta
on both HEAD's `lib-ppc.c` (`__floatundidf` only) and the expanded
version we're shipping in 026:

```
gcc-built tcc compiling tcc.c     -> 656938 bytes (.o)
tcc-self  compiling tcc.c         -> 656906 bytes (.o)
                                     differ at byte 60, 32 bytes shorter
                                     __text section: -44 bytes (-11 instructions)
```

Both files have identical Mach-O headers (3 cmds, 1783 syms, 156
indirect-syms, same flags `0x2000` = `MH_SUBSECTIONS_VIA_SYMBOLS`),
so this is a content-only divergence somewhere in the .text section.

Importantly: the diff is byte-neutral with respect to my
`lib-ppc.c` expansion — it's *exactly* the same with HEAD's
`lib-ppc.c` and the expanded version. So 026 didn't introduce this.
It must have been introduced by 023, 024, or 025 (post-020 work)
and went unnoticed because no one re-ran the fixpoint test.

This regression deserves its own session. Captured in
[`findings.md`](findings.md).

## Exit state

* `tcc/lib/lib-ppc.c` — 10 libgcc helpers, ready for self-link as
  soon as the keymgr init-order issue is resolved.
* `tcc/ppc-macho.c` — unchanged from 025 HEAD.
* `scripts/bootstrap-tcc-self.sh` — unchanged from 025 HEAD
  (still uses `gcc-4.0` for the link step).
* The gcc-link bootstrap path still produces a working `tcc-self`
  that runs `-v` correctly.
* Two open issues for follow-up sessions:
  1. **keymgr / DWARF init-order bug** (blocks tcc-self self-link).
  2. **fixpoint regression** introduced somewhere in 023-025 (does
     not block normal use, but breaks the canonical 020 result).

## Files touched

| | |
|---|---|
| `tcc/lib/lib-ppc.c` | +194 lines (was 29, now 223): nine new helpers |
| `docs/roadmap.md` | rewritten to reflect the 023-025 work and the v0.2.0 plan |
| `docs/blog/025-self-link.md` | new: public-facing writeup of the 025 "eight bus errors" story |
| `docs/sessions/026-libgcc-helpers/README.md` | (this file) |
| `docs/sessions/026-libgcc-helpers/findings.md` | open issues for follow-up sessions |
