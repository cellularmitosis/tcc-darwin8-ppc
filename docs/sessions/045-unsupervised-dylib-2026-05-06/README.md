# Session 045 — unsupervised dylib + beyond (2026-05-06)

## Arrival state

- HEAD: `2f078dd` (v0.2.24-g3 default — `tcc -ar` native Mach-O).
- tests2: 111 / 111 (100%).
- Bootstrap fixpoint holds.
- Five real-world programs work end-to-end (lua, zlib, bzip2, cJSON, sqlite-with-file).
- `tcc -ar` is now native Mach-O.
- `tcctest.c` diff vs gcc reference: 33 lines.

## Exit state (after v0.2.25-g3)

- v0.2.25-g3 cut. **First Mach-O dylib output on Tiger PPC.**
- `tcc -shared -o foo.dylib foo.c` produces a real PPC dylib;
  dyld loads it via `dlopen`; dlsym finds exported functions.
- New demo: [`v0.2.25-dylib.sh`](../../../demos/v0.2.25-dylib.sh) —
  builds a libgreet.dylib with `greet(const char *)` and
  `get_count(void)`; links a tcc-built exe that dlopens it; prints
  three greetings.
- tests2 still 111/111. Bootstrap fixpoint still holds.

## What landed

### v0.2.25-g3 — Mach-O dylib output

- New `macho_output_dylib()` in `tcc/ppc-macho.c` (~600 lines,
  mirrors `macho_output_exe()` with these dylib-specific
  differences):
  - `MH_DYLIB` filetype (was `MH_EXECUTE`).
  - No `__PAGEZERO` segment (host process owns page 0, not the
    dylib).
  - No `LC_UNIXTHREAD` (dylibs are not entry points).
  - No crt-shim, no `_main` lookup, no auto-injected
    `__tcc_start_main`.
  - `LC_ID_DYLIB` describes this dylib's install name.
  - `__mh_dylib_header` (vs `__mh_execute_header`) at __TEXT base.
  - Default __TEXT vmaddr `0x40000000` (high enough to avoid
    common conflicts).
  - Function-call stubs are **PIC** (8 instructions / 32 bytes
    per stub). Old absolute stubs would break under dyld's
    whole-image slide; new PIC stubs use `mflr / bcl / mflr / addis
    / mtlr / lwz / mtctr / bctr` with a `(slot - .L1)` SECTDIFF
    anchor that's invariant under sliding.

- Output dispatch in `macho_output_file` now routes
  `TCC_OUTPUT_DLL` to `macho_output_dylib`.
- `tcc.c` — when no `-o` is supplied, `-shared` produces a
  `.dylib` extension on Mach-O.
- `tccelf.c` — for `TCC_OUTPUT_DLL` skip crt1.o auto-link, skip
  `__tcc_start_main` injection, skip the keymgr stub. Still link
  libtcc1.a (for libgcc helpers like `__muldi3` etc.).

- Tested end-to-end on ibookg37:
  - Simple functions (`int double_it(int)`).
  - Functions calling printf/strlen via PIC stubs.
  - Static initialized data (`static int call_count`).
  - `__attribute__((constructor))` runs at dlopen time.
  - pthread mutex usage.
  - Long-long arithmetic (libtcc1.a auto-linked).

### Debugging note: dyld faults without LC_DYSYMTAB

First attempt produced a dylib with everything **except**
`LC_DYSYMTAB`. dyld then faulted with `KERN_PROTECTION_FAILURE`
in `ImageLoaderMachO::doBindExternalRelocations()` during
`dlopen`. Resolution: always emit `LC_DYSYMTAB` (and
`LC_LOAD_DYLINKER`) for dylibs, even when all the counts are
zero. Without `LC_DYSYMTAB` dyld walks uninitialized state when
processing the dylib's external relocations.

### v0.2.26-g3 — link-time dylib support

Landed in the same session as v0.2.25. `tcc -lz hello.c` now
actually works at runtime:

- `macho_load_dll()` (was a no-op stub) now parses the dylib's
  Mach-O header (with fat-binary handling), walks `LC_SYMTAB`,
  and registers each defined-external symbol as UNDEF in our own
  symtab via `set_elf_sym(SHN_UNDEF)`. The Mach-O leading
  underscore is stripped to match tcc's bare-C-name internal
  convention. Captures the install name from `LC_ID_DYLIB` and
  adds a `DLLReference` via `tcc_add_dllref()`.
- Both `macho_output_exe` and `macho_output_dylib` now walk
  `s1->loaded_dlls` and emit one `LC_LOAD_DYLIB` per loaded dll
  (libSystem first when externs exist, extras after). Total
  `cmdsize` and `ncmds` updated accordingly.
- When extra (non-libSystem) dylibs are loaded, the writer
  switches to **FLAT namespace** (clears `MH_TWOLEVEL`) so dyld
  searches all loaded dylibs at runtime. This avoids the
  per-symbol two-level-ordinal tracking that strict TWOLEVEL
  would require — a side-table mapping each undef sym to its
  source dll. UNDEF n_desc gets ordinal=0 (DYNAMIC_LOOKUP) under
  flat; ordinal=1 (libSystem) under two-level (preserves
  existing libSystem-only behavior).
- **Closes upstream `dlltest`** (multi-session deferred).
  tcc compiles libtcc.c into libtcc2.dylib, links a tcc2 exe
  against that dylib (the libtcc.c symbols come from the
  parsed `LC_SYMTAB`), and tcc2 -run prints "Hello World"
  through the full round trip.
- Demo: [`v0.2.26-link-dylib.sh`](../../../demos/v0.2.26-link-dylib.sh)
  builds a tiny exe that calls into Tiger's bundled
  `/usr/lib/libz.1.dylib` (`zlibVersion`, `adler32`) at link
  time. `otool -L` shows the libz `LC_LOAD_DYLIB`. Runs at
  runtime printing zlib 1.2.3.

Pre-v0.2.26, that demo failed at runtime with:
```
dyld: Symbol not found: _zlibVersion
  Referenced from: /tmp/testz
  Expected in: /usr/lib/libSystem.B.dylib
```
because the exe never emitted `LC_LOAD_DYLIB libz.1.dylib`.

### v0.2.27-g3 — JIT heisenbug fixed (5+ sessions deferred)

The long-deferred JIT heisenbug — `tcc_relocate` + repeated
`tcc_new`/`tcc_delete` cycles producing SIGILL / SIGBUS /
SIGSEGV / silent-wrong-result at random iterations — turned out
to be a one-line bug.

**Root cause:** In `tcc/ppc-macho.c::__clear_cache`, the gcc
build path uses real PPC `__asm__ volatile("dcbst.../icbi...")`.
The TCC build path (`__TINYC__` set, since tcc has no PPC
inline-asm parser yet) fell through to a no-op stub. The
comment had even acknowledged this:

> When tcc compiles itself: tcc has no PPC inline-asm parser
> yet (ppc-asm.c deferred). Stub it so the bootstrap completes;
> the resulting tcc-self can produce object files but its own
> -run mode would skip cache flushing.

So `protect_pages(..., 0/3)` in `tccrun.c` called `__clear_cache`,
the stub did nothing, and JIT page reuse across iterations
(same address each time, e.g. 0xa7000) saw stale instructions
from icache. The signature was layout-sensitive: simple
`int f(int)` rarely tripped (instructions might fit in already-
flushed cache lines), but `two_float f(two_float)` (~40
instructions, multiple cache lines) hit it consistently.

**Verification path:**
1. Reproduced 30-iteration loop crashes with `tcc`-compiled
   harness (random SIGILL/SIGBUS/SIGSEGV).
2. Recompiled the SAME harness with `gcc-4.0`. 10/10 runs at
   30/30 iterations succeeded — proving the bug was on the
   compiler side, not in the JIT'd code.

**Fix:** delegate to `sys_icache_invalidate(start, length)`
which is exported by Tiger libSystem and performs the
dcbst/sync/icbi/sync/isync dance internally. One-line patch.

**Test impact:**
* `tests2`: still 111/111. Bootstrap fixpoint still holds.
* `abitest-tcc`: was failing variably at iteration 5–19 of
  ~24 sub-tests. Now passes 20 sub-tests deterministically;
  fails at `many_struct_test_3` — a separate, reproducible
  bug we can now actually chase.
* `test3`: was SEGV at line ~770/4500 of tcctest. Now runs to
  completion; only fails on the known content diffs (`_Bool`
  size, promote-char/short-funcret UB).
* `libtest_mt`: makes substantial progress (gets through fib);
  still fails later.
* `dlltest`: now stable across the full upstream test run
  (was sometimes failing under `make -k test` due to earlier
  JIT failures cascading).

**Demo:**
[`v0.2.27-jit-heisenbug.sh`](../../../demos/v0.2.27-jit-heisenbug.sh)
loops the original `two_float` repro 30× and confirms 30/30 OK.

### v0.2.28-g3 — abitest-tcc 24/24 + Tiger PPC backtraces

After the JIT heisenbug fix, abitest-tcc was deterministically
failing at `many_struct_test_3` (was previously
heisenbug-failing at random earlier points). The new failure
exposed a separate bug, fixed in v0.2.28.

**Bug 1: save_regs(nb_args + 1) skipped the func-ptr slot.**
In `gfunc_call` (ppc-gen.c), `save_regs(nb_args + 1)` saves
vstack entries up to `vtop - (nb_args + 1)` — but the function
pointer / call target lives at `vtop - nb_args`. So the
func-ptr slot was NOT included in the save range. For most
calls that's fine (func ptr is a CONST symbolic, no register
involved). But for indirect calls where the func ptr is an
LVAL with its address in a volatile GPR — e.g.
`(*(s2->f2 = &f) + 0)(v,v,v,v,v,v,1.0)` from upstream
many_struct_test_3 — the arg-pass-2 code overwrote that GPR
with v.a (=1) before the indirect-call site could `gv()` the
function pointer. The result: `lwz r11, 0(r4)` with r4 = 1
→ Bus error.

Disassembled abitest-tcc to confirm: just before the crash,
the test driver does `lwz r4, 0x0(r12)` (loading v.a=1 into
r4), then `lwz r11, 0(r4)` (CRASH at addr 0x1).

Fix: `save_regs(nb_args)` instead, matching x86_64-gen.c. The
func-ptr slot is now included in the save range so its address
register survives arg setup.

Five tests flip from fail/crash to pass:
many_struct_test_3, stdarg_test, stdarg_many_test,
stdarg_struct_test, arg_align_test. **abitest-tcc is now
clean at 24 / 24.**

**Bug 2: PPC backtrace signal handling was a no-op.**
`tccrun.c::rt_get_caller_pc` had no PPC case (just a
`#warning add arch specific` no-op). And `rt_getcontext`
didn't extract `ip`/`fp` from Tiger's `mcontext`. Result:
when a JIT'd function crashed, the signal handler couldn't
walk the stack to find the owning TCCState, so longjmp out
through `tcc_setjmp`'s saved jmpbuf never fired —
`libtest_mt`'s "producing some exceptions" stage failed.

Implemented:
- `rt_getcontext`: read PC from `uc->uc_mcontext->ss.srr0`,
  SP from `ss.r1`. Tiger uses non-underscore field names
  (`ss.r1` vs Leopard+'s POSIX `__ss.__r1`).
- `rt_get_caller_pc`: PPC back-chain walk. `0(SP)` is the
  back chain (caller's SP). `8(SP)` (= `((addr_t *)SP)[2]`)
  is the saved LR slot, set by the caller's `bl` and held
  through the function's lifetime. So at level=1 we read
  `((addr_t *)rc->fp)[2]`; at level≥2 we walk back-chain
  (level-1) times then read `[2]` of the resulting frame.

After this, `libtest_mt "producing some exceptions"` passes
(prints `89! 144! 233! ...`), `test1b` reports real
`RUNTIME ERROR: invalid memory access` with backtrace
addresses, and the signal-handler longjmp path is finally
functional on PPC.

**Demo:** [`v0.2.28-clean-abitest.sh`](../../../demos/v0.2.28-clean-abitest.sh)
runs `abitest-tcc` and `abitest-cc` and shows the former is
clean while the latter still has its known long-double diffs.

### Out of scope this session (deferred to follow-ups)

1. **Local relocation entries for dylib sliding** — currently
   our dylibs work only when dyld loads them at the preferred
   vmaddr. If dyld slides them, absolute references in `__data`
   (e.g. `int *p = &arr[N]` static initializers) won't be
   patched. Function calls survive (PIC stubs). For an MVP this
   is acceptable; programs hit the slide path rarely with a high
   default vmaddr.

2. **JIT heisenbug** (carried from session 044). Untouched this
   session.

3. **Two-level namespace with multi-dylib** — currently we
   switch to flat namespace whenever extra dylibs are loaded.
   That works but is slightly less efficient at dyld lookup
   time and is more permissive (any dylib's symbol can shadow
   another). Strict two-level requires per-symbol ordinal
   tracking. Future polish.

## Subagent log

See [subagents.md](subagents.md). No subagents used in v0.2.25;
the codebase is small enough and the dylib writer is enough of a
careful design (PIC stubs, layout, dyld quirks) that solo work
was simpler than briefing a subagent.

## Files touched (v0.2.25-g3)

- `tcc/ppc-macho.c` — added `macho_output_dylib()` function and
  dispatcher entry; added `MH_DYLIB`, `LC_ID_DYLIB` constants.
- `tcc/tcc.c` — `.dylib` default extension when `-shared` and no
  explicit `-o` on Mach-O.
- `tcc/tccelf.c` — skip crt1/__tcc_start_main/keymgr injection for
  DLL output type; still link libtcc1.a.
- `demos/v0.2.25-dylib.{c,sh}` — runnable demo.
- `demos/README.md` — table row.
- `docs/roadmap.md` — release row + structural item #3.5 closed.
- `scripts/build-release-tarball.sh` — VERSION default + release
  notes for v0.2.22 / v0.2.23 / v0.2.24 / v0.2.25.

