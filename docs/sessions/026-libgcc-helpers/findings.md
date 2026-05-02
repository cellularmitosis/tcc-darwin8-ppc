# Findings — 026

Things discovered during 026 that are worth carrying into future
sessions.

## Open issue: keymgr / DWARF init-order bug blocks tcc-self self-link

**Symptom:** `tcc-self` linked entirely by tcc (no `gcc-4.0`)
crashes with `SIGBUS` before `main` runs.

**Stack at crash:**

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
#9  0x0006e2d4 in start ()
```

**Diagnosis:** `crt1.o`'s `_start` invokes
`__keymgr_dwarf2_register_sections` (which lives in libSystem on
Tiger) before any `mod_init_func` has run. That function calls
`_dyld_register_func_for_add_image`, which synchronously invokes
the registered callback for already-loaded images. The callback
calls `calloc`, but `_malloc_initialize` faults because the malloc
subsystem hasn't been set up yet.

**Why the gcc-link path works:** `gcc-4.0` pulls extra `.o` members
from `libgcc.a` that contribute a keymgr support layer:
`__init_keymgr`, `___keymgr_global`,
`__keymgr_get_and_lock_processwide_ptr`,
`__keymgr_set_and_unlock_processwide_ptr`. With those in place, the
init order works out.

**Differential** (UNDEF symbols missing in our tcc-link binary
relative to the gcc-link one):

```
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

**Possible fixes, in increasing order of effort:**

1. **Stub `__keymgr_dwarf2_register_sections` in our crt1.o reader.**
   Define a no-op symbol of that name when reading crt1.o, so the
   `_start` reference resolves to a do-nothing function. We don't
   use C++ exceptions in tcc-built code, so DWARF unwind
   registration is unnecessary anyway. Smallest possible patch.

2. **Auto-link a "keymgr-glue.o" shim from libtcc1.a.** Provide
   the same symbols that gcc-4.0 contributes from libgcc.a, written
   as no-op stubs in our libtcc1.a. Slightly more elaborate; may
   subtly differ from the libgcc behavior.

3. **Pull the relevant members from `libgcc.a` directly via our
   archive loader.** Requires the alacarte archive loader (which
   we punted in 025). Most "correct" but largest scope.

**Other observation:** our broken binary has duplicate UNDEF entries
for `___keymgr_dwarf2_register_sections`, `_atexit`, `_exit`. Looks
like the Mach-O reader is creating multiple slots for the same
symbol when both crt1.o and another input reference it. Worth
checking the `macho_translate_sym` coalescing path while in there.

## Open issue: pre-existing fixpoint regression

**Symptom:** the canonical self-host fixpoint that
[020](../020-self-host/README.md) achieved is broken at HEAD.

**Test:**

```
gcc-built tcc compiling tcc.c     -> 656938 bytes (.o)
tcc-self  compiling tcc.c         -> 656906 bytes (.o)
                                     differ at byte 60, 32-byte size delta
                                     __text section: -44 bytes (= 11 PPC instructions)
```

Both .o files have identical Mach-O headers (3 cmds, 1783 syms, 156
indirect-syms, flags `0x00002000`). The divergence is purely in the
text section.

**Confirmed not introduced by 026:** The same exact delta (656906
vs 656938, char 60) reproduces with both HEAD's `lib-ppc.c` (just
`__floatundidf`) and the 026-expanded version. So this regression
was introduced sometime in sessions 023, 024, or 025.

**Investigation plan for the next session:**

1. `git bisect` between commit `7e2a4ae` (020, fixpoint reached)
   and HEAD to identify the offending commit.
2. Disassemble the missing 44 bytes of `__text` to see what
   instructions are gone — likely a small codegen change that
   gcc-built tcc emits one way and tcc-self-built tcc emits
   another.
3. Fix or document.

This doesn't block normal use of tcc — both binaries are
functionally working compilers — but it breaks the headline 020
result and is worth restoring before any v0.2.0 release notes claim
"byte-identical fixpoint."

## Notes on the sub-agent dispatch experience

This session was originally dispatched to a Sonnet 4.6 sub-agent
with a self-contained prompt covering steps 1-3 of the v0.2.0 plan.
The agent did the literal scope (helpers + bootstrap script edit)
in roughly the first hour, then discovered the SIGBUS crash and
spent the next ~2 hours doing forensic Mach-O work — comparing
binaries, disassembling crt1.o, dumping symbol tables, running gdb
under ssh. It implemented two structural changes to `ppc-macho.c`
(lazy-pointer section split and `MH_FORCE_FLAT` flag swap) that did
not actually solve the crash, and silently switched its test host
from `ibookg37` to `imacg3` somewhere along the way.

The agent did not write a session README or capture findings. The
"Background task stopped" final state of the run looks like the
agent was killed or hit a context limit mid-investigation rather
than completing successfully.

**Lesson:** the well-scoped portion of 026 (add helpers) was a
good fit for sub-agent dispatch. The "verify the resulting binary
works" portion was not, because verification turned into open-ended
debugging the moment the binary didn't work — and Sonnet didn't
escalate. For sessions that *might* turn into research mid-flight,
either:

1. Split the dispatch: one task for the mechanical work, hand back
   for review, then dispatch a separate "investigate X" task with a
   tight scope.
2. Tell the agent explicitly: "If verification fails, stop and
   report; do not begin debugging."

The agent's actual diagnosis (init-order, lazy-pointer ordering)
was on the right track but two layers of indirection away from the
real bug. Captured here in case the lazy-pointer split turns out to
be useful in a future session, but I'd start from a clean
`ppc-macho.c` rather than its work-in-progress diff.
