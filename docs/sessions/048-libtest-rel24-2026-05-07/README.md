# Session 048 — libtest `R_PPC_REL24 out of range` fix

**Date:** 2026-05-07 (same evening as session 047)
**Build host:** imacg3

## Arrival state

* HEAD `470a922` — v0.2.32-g3 (just-shipped LD work).
* All previously-passing tests/demos still pass.
* Upstream `libtest` stage was failing — and had been failing
  silently for at least one prior session (the session-046 HANDOFF
  mistakenly listed it as passing; session 047 verified it was
  already broken pre-LD-work by stashing the diff and re-running).

## Root cause

`tcc_add_symbol(s, name, addr)` registers `name` as an SHN_ABS
symbol with `st_value = addr` (the absolute runtime address of the
callback). When JIT-compiled code calls that symbol via `bl name`,
the relocator emits a `R_PPC_REL24` and computes `disp = addr - bl_pc`.

PPC32's `bl` instruction has a 24-bit signed displacement, so disp
must fit in ±32MB. In practice the libtcc binary's text and the
mmap'd JIT region routinely land >32MB apart; the `bl` then can't
reach the callback.

tcc has the right machinery to handle this — `create_plt_entry`
synthesizes a 4-instruction PLT trampoline (`lis/ori/mtctr/bctr`)
that does an absolute indirect jump, and `relocate_plt` patches its
immediates from the symbol's runtime value once known. The PLT lives
in the JIT region itself, so a `bl plt_stub` from the JIT'd code is
always in REL24 range.

What was missing: `build_got_entries` had a guard that skipped
synthesizing PLT entries for SHN_ABS symbols whenever
`PTR_SIZE != 8`:

```c
} else if (sym->st_shndx == SHN_ABS) {
    if (sym->st_value == 0) /* from tcc_add_btstub() */
        continue;
#ifndef TCC_TARGET_ARM
    if (PTR_SIZE != 8)
        continue;
#endif
    /* from tcc_add_symbol(): on 64 bit platforms these
       need to go through .got */
}
```

The comment's reasoning ("on 64 bit platforms these need to go
through .got") is half-right: x86_64's PLT32 has the same ±2GB
limitation. But it's also true on PPC32 (24-bit) for the >32MB case.
ARM was already an exception for the same reason. PPC32 needs to
join.

## Fix

One line, in `tccelf.c`'s `build_got_entries`:

```diff
-#ifndef TCC_TARGET_ARM
+#if !defined TCC_TARGET_ARM && !defined TCC_TARGET_PPC
     if (PTR_SIZE != 8)
         continue;
 #endif
```

The existing PPC `create_plt_entry` / `relocate_plt` machinery
already handles emission. Just needed to reach it.

## Verification

| Stage | Pre-session | Post-session |
|---|---|---|
| upstream `libtest` | ❌ R_PPC_REL24 out of range | ✅ |
| tests2 | 111/111 | 111/111 |
| abitest-cc | 24/24 | 24/24 |
| abitest-tcc | 24/24 | 24/24 |
| dlltest | passes | passes |
| Bootstrap fixpoint | holds | holds |
| All real-world demos | pass | pass |

## Released as v0.2.33-g3.

## Side polish

While diagnosing, I also added the failing symbol's name to the
`R_PPC_REL24 out of range` error message in `ppc-link.c`. With the
PLT machinery now in place this error should no longer fire for
SHN_ABS callbacks, but if it ever does (e.g., for a symbol
build_got_entries doesn't cover for some reason), the message now
identifies the offender immediately instead of just printing the raw
addr/val/disp triple.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents — this was a fast diagnose-and-fix that benefited from
the earlier session-047 context (knowing the symptom and that
session-046's claim of libtest passing was wrong).
