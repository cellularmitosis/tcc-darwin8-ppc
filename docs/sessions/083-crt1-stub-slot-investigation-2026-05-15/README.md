# Session 083 — crt1.o stub-slot investigation (082 #4 + #5)

**Started:** 2026-05-15
**Arrival HEAD:** `f55983b` (end of session 082, v0.2.61-g3).
**Target:** v0.2.62-g3.

## Arrival state

End of session 082 landed the `-vv` reader diagnostic (v0.2.61-g3),
the symmetric counterpart to the v0.2.50 / v0.2.59 / v0.2.60
writer-side blocks. On its first real-world link (hello-world against
Apple's `/usr/lib/crt1.o`), the diagnostic immediately surfaced four
"non-extern stub-slot resolve failed" lines:

```
tcc: reader: crt1.o: reloc-drop: section __text reloc 21: non-extern stub-slot resolve failed (target sect 3, value 0x330)
tcc: reader: crt1.o: reloc-drop: section __text reloc 23: non-extern stub-slot resolve failed (target sect 3, value 0x340)
tcc: reader: crt1.o: reloc-drop: section __text reloc 29: non-extern stub-slot resolve failed (target sect 3, value 0x350)
tcc: reader: crt1.o: reloc-drop: section __text reloc 85: non-extern stub-slot resolve failed (target sect 3, value 0x370)
```

The hello-world program ran correctly regardless, so the dropped
relocs weren't catastrophic — but the question "is this a latent
bug or is dyld papering over it?" was now askable. Session 082
HANDOFF items #4 and #5 carried that question into this session.

## Investigation

### crt1.o anatomy (from `otool -lv` and `otool -Iv`)

```
__text         addr 0x000  size 0x3a8  reloc[127]
__symbol_stub  addr 0x3a8  size 0x000  (empty placeholder)
__picsym_stub  addr 0x3a8  size 0x000  (empty placeholder)
__symbol_stub1 addr 0x3b0  size 0x040  reserved1=0  reserved2=16  reloc[16]
__cstring      addr 0x3f0  size 0x0e0
__data         addr 0x4d0  size 0x014
__nl_sym_ptr   addr 0x4e4  size 0x00c  reserved1=4
__la_sym_ptr   addr 0x4f0  size 0x010  reserved1=7  reloc[4]
__dyld         addr 0x500  size 0x008

Indirect symtab (11 entries):
  [0..3]  __symbol_stub1: _exit, ___keymgr_dwarf2_register_sections, _main, _atexit
  [4..6]  __nl_symbol_ptr: _mach_init_routine, _errno, __cthread_init_routine
  [7..10] __la_symbol_ptr: _exit, ___keymgr_dwarf2_register_sections, _main, _atexit
```

So `__symbol_stub1` lives at `0x3b0..0x3f0`, contains 4 stubs of 16
bytes each, with `reserved1 = 0` meaning `indirect[0..3]` are the
four targets.

### The 4 failing relocs — what they actually point at

From `otool -rv /usr/lib/crt1.o`:

```
0x32c JBSR sym=4 (__symbol_stub1)  PAIR other_part = 0x000003b0   (= _exit's stub)
0x328 JBSR sym=4 (__symbol_stub1)  PAIR other_part = 0x000003d0   (= _main's stub)
0x304 JBSR sym=4 (__symbol_stub1)  PAIR other_part = 0x000003e0   (= _atexit's stub)
0x120 JBSR sym=4 (__symbol_stub1)  PAIR other_part = 0x000003c0   (= ___keymgr...'s stub)
```

The values the diagnostic printed (0x330, 0x340, 0x350, 0x370) are
NOT the stubs in `__symbol_stub1` — they're inside `__text`'s
address range (0..0x3a8). Direct inspection of the PPC slice (file
offset 0x1000+1004 = 0x13ec) confirms a five-instruction local
thunk at each:

```
0x32c: 48000005      bl 0x330           ; JBSR (BR24 = +4, link bit set)
0x330: 3d80 0000     lis r12, 0         ; HI16 reloc → _exit stub (0x3b0)
0x334: 618c 03b0     ori r12, r12, 0x3b0; LO16 reloc
0x338: 7d89 03a6     mtctr r12
0x33c: 4e80 0420     bctr               ; jump to _exit's stub at 0x3b0
```

So **the BR24 immediate points at a local 5-instruction thunk, not
at the stub directly.** The PAIR record's `r_address` field carries
the actual far-call target (the stub VA). This is the Mach-O
**JBSR** (Jump to Branch Subroutine) convention: the assembler
can't reach the real target with a 24-bit displacement, so it emits
a local trampoline within range and aims BR24 at the trampoline.
The PAIR record tells the linker the real target so the linker can
optionally bypass the trampoline if it can reach the target itself.

### Why tcc resolved 0 of the 4

tcc's `macho_translate_relocs` was treating non-extern JBSR
identically to BR24 — extracting the displacement from the
instruction:

```c
} else if (r_type == PPC_RELOC_BR24 || r_type == PPC_RELOC_JBSR) {
    disp = insn & 0x03fffffc;
    if (disp & 0x02000000) disp |= 0xfc000000;
    target_value = sec->addr + r_address + disp;
}
```

For BR24 that's correct. For JBSR it gives the **local thunk's
address** (0x330, 0x340, etc.), not the actual stub VA. Then
`macho_resolve_stub_slot` computed `entry_idx = (0x330 - 0x3b0)/16`
on `__symbol_stub1` (addr 0x3b0), which underflows to a huge
positive number, exceeds `n_indirect = 11`, and the lookup fails.

The fix is to read the **PAIR record's full 32-bit r_address**
(not just the low 16 bits, the way HA16/LO16/HI16 do) and use it
as `target_value`.

### The HI16/LO16 relocs at the thunk worked correctly

The local thunk at 0x330 also has HI16 (at 0x330) and LO16 (at
0x334) relocs whose PAIRs carry `half = 0x03b0`. The existing
HA16/LO16/HI16 path correctly resolves those to target_value
0x000003b0 (via `(this_half << 16) | other_half`), which falls
inside `__symbol_stub1`'s address range and indexes `indirect[0]`
= _exit. So those four relocs (and their PAIRs) translated cleanly
— they're not in the failure list. The thunk gets the right
address baked in, but it's dead code in the linked binary because
the new JBSR routing patches the BR24 to bypass the thunk entirely.

### Was hello-world genuinely broken before?

No — but it was nearly so. Pre-fix, the 4 JBSR relocs were silently
dropped, leaving the BR24 instructions in __text with their
original `bl +4` displacement. That branched into the local thunk
at 0x330, which had its HI16/LO16 patched to load _exit's stub
address (0x3b0 in crt1's address space), then jumped through r12.
But _exit's stub at 0x3b0 was *not* emitted into the linked binary
either (crt1's `__symbol_stub1` was section-skipped by tcc), so the
thunk's `mtctr/bctr` would jump to whatever address tcc happened to
relocate 0x3b0 to via the HI16/LO16 relocs — which was... a stub
synthesized by tcc's own writer for _exit. Coincidence: tcc's
extern-stub machinery sees _exit referenced (through the
HI16/LO16 anchor that the loader translates via the indirect
symtab) and synthesizes a __picsymbolstub1 entry for it. So the
thunk's hi/lo load resolves to tcc's freshly-emitted stub, and
hello-world reaches _exit through *that*. The four BR24's
were dead-branches-to-live-thunks-targeting-tcc-stubs. Fragile but
accidentally correct.

Post-fix, the JBSR routes through the indirect-symtab to _exit
directly: tcc emits a stub for _exit (NEEDS_STUB tag via line 7324)
and the BR24 patches to that stub. The local thunk becomes truly
dead code (still emitted, never reached). Same end result, but now
the path is explicit and traceable.

## The fix

One block in `tcc/ppc-macho.c::macho_translate_relocs`:

```c
} else if (r_type == PPC_RELOC_BR24) {
    disp = insn & 0x03fffffc;
    if (disp & 0x02000000) disp |= 0xfc000000;
    target_value = sec->addr + r_address + disp;
} else if (r_type == PPC_RELOC_JBSR) {
    /* JBSR encodes the real far-call target in the PAIR's
     * r_address field (full 32-bit, not just the low 16
     * like HA16/LO16/HI16). The BR24 immediate in the
     * instruction itself points at a local thunk used as
     * a 24-bit-reachable trampoline ... */
    pair_w0 = (k + 1 < sec->nreloc) ? mach_get32(r + 8) : 0;
    target_value = pair_w0;
}
```

The existing post-resolution path already does the right thing:
- `target_sect = r_symnum - 1 = 3` (= __symbol_stub1)
- `macho_resolve_stub_slot(target_sect, target_value=0x3b0)`:
  - `entry_idx = (0x3b0 - 0x3b0) / 16 = 0`
  - `indirect_idx = 0 + 0 = 0` → indirect[0].sym_idx = 27 (_exit)
- `slot_kind = S_SYMBOL_STUBS` → tag _exit with `ST_PPC_NEEDS_STUB`
- elf_type = `R_PPC_REL24` (JBSR shares BR24's elf mapping)
- Emit reloc at sec_off + r_address pointing at the resolved _exit sym

Same logic resolves _main (0x3d0 → indirect[2]), _atexit (0x3e0 →
indirect[3]), and ___keymgr_dwarf2_register_sections (0x3c0 →
indirect[1]).

## What landed

### `tcc/ppc-macho.c`

* Split the `PPC_RELOC_BR24 || PPC_RELOC_JBSR` arm of the non-extern
  reloc dispatch into two separate arms. BR24 keeps the instruction-
  displacement path; JBSR reads the PAIR's full 32-bit r_address.
  ~20 lines added (comment + body).

### Demo

Not adding a new demo. The v0.2.61 demo already asserts the
diagnostic machinery works on a simple link; the `crt1.o`
behavior change is observable via `-vv` on hello-world (the four
"non-extern stub-slot resolve failed" lines should be absent).

### Docs

* `docs/roadmap.md` — header bumped, v0.2.62-g3 row prepended.
* `docs/sessions/083-crt1-stub-slot-investigation-2026-05-15/`
  — this README, plus HANDOFF.
* `demos/README.md` — *(unchanged; no new demo)*

## Results on imacg3

* `bash scripts/run-tests2.sh` → **Total: 111 Pass: 111 Fail: 0**
* `make abitest` in `tcc/tests/` → **48/48 success** (24 cc + 24 tcc)
* `FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh` → **FIXPOINT HOLDS**
* Ten v0.2.25..v0.2.61 demos — all PASS *(v0.2.61's assertions are
  loose enough — count ≥ 5, both categories present — that they
  pass with the 4 stub-slot lines now absent).*

`-vv` on hello-world post-fix produces 7 reader lines (was 11),
just the expected section-skips + their cascading wholesale
reloc-drops; the 4 stub-slot resolve failures are gone:

```
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub (sect 1, ...)
tcc: reader: crt1.o: section-skip: __TEXT,__picsymbol_stub (sect 2, ...)
tcc: reader: crt1.o: section-skip: __TEXT,__symbol_stub1 (sect 3, ...)
tcc: reader: crt1.o: section-skip: __DATA,__nl_symbol_ptr (sect 6, ...)
tcc: reader: crt1.o: section-skip: __DATA,__la_symbol_ptr (sect 7, ...)
tcc: reader: crt1.o: reloc-drop: section __TEXT,__symbol_stub1 skipped: 16 relocs dropped wholesale
tcc: reader: crt1.o: reloc-drop: section __DATA,__la_symbol_ptr skipped: 4 relocs dropped wholesale
```

The 5 section-skips and 2 wholesale reloc-drops are all expected
— Apple-internal sections that tcc's link path re-synthesizes from
the indirect symtab.

## Findings worth remembering

### JBSR vs. BR24 in Mach-O PPC

* `PPC_RELOC_BR24` (type 3): direct branch. BR24 displacement is
  the truth; the PAIR is not used for the target (and in fact a
  BR24 reloc has no PAIR).
* `PPC_RELOC_JBSR` (type 13): long-branch with linker-optional
  trampoline. BR24 immediate points at a local 5-insn thunk;
  PAIR's **full 32-bit `r_address`** is the real far-call target.
  Linker may bypass the thunk if it can reach the target in 24
  bits. tcc's reader now reads from the PAIR.

### Mach-O PAIR record's r_address is a 32-bit field

For HA16/LO16/HI16, only the low 16 bits matter (the high 16 came
from the leader instruction). For JBSR, the entire 32-bit
`r_address` is the address. `otool` confirms by printing
`other_part = 0x000003b0` (4-byte form) for JBSR PAIRs and
`half = 0x03b0` (truncated form) for HA16/LO16/HI16 PAIRs.

### The diagnostic from v0.2.61 paid for itself on day one

This bug was latent for the entire life of the PPC backend. Every
hello-world link silently dropped 4 relocs that happened to be
correct-by-coincidence (the local thunks rerouted to tcc's
synthesized stubs through HI16/LO16). v0.2.61's `-vv` surfaced
the silent decision; one session later it's a real fix. That's the
diagnostic's value proposition exactly.

### Session 082 README's analysis was almost right

The session 082 README correctly identified that the addresses
0x330, 0x340, 0x350, 0x370 lay inside `__text`, not inside
`__symbol_stub1`, and correctly conjectured "branches from crt1's
code to its own stub section" plus "patched by dyld's stub binder
at runtime, not tcc's static link." The actual mechanism is
slightly different — the branches are JBSR-routed through local
trampolines that load the stub address from a PAIR-carried
constant, not direct branches into the stub section — but the
spirit (Apple-internal routing that tcc was misinterpreting) was
correct. The diagnostic + a 30-min `otool` session was enough to
nail the precise mechanism.

## Open work for next session

### 1. Sibling r11 watch (carried, 067 #3)

PPC sibling-register concern to v0.2.48's r12 fix. csmith hasn't
surfaced a repro yet. If a future seed does, the symmetric fix is
`reg_classes[8] = 0`. Just keep an eye on it.

### 2. AltiVec intrinsics (carried)

PowerPC SIMD. Would unlock vector-heavy programs (libpng's filter
loops, openssl's AES-NI fallback paths on PPC, gimp's pixel ops).
New parser front-end work + codegen. Multi-session lift.

### 3. `bcheck.c` port (carried)

PowerPC port of tcc's bounds-check runtime. Currently stubbed out
via `bcheck-ppc.c` (no-op shims). Real port would need to intercept
allocations, maintain a redzone map, and emit per-load/store
bounds-check calls. Multi-session lift.

### 4. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after twelve+ confirmed
rebuild cycles since 069 (most recent: this session, post-fix).

### 5. (optional, low priority) `rsync` exclude-list polish

`tcc/c2str.exe` (built on uranium for testing, arm64) is not in
the current rsync exclude list. Sidestepped this session and
the prior three by single-file `scp tcc/ppc-macho.c`. If sync
becomes a recurring pain point in a session that touches many
files, a `scripts/sync-to-imacg3.sh` wrapper with the canonical
exclude list is the right move.
