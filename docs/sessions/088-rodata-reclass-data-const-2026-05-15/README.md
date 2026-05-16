# Session 088 — `.rodata` reclassification to `__DATA,__const` (gcc-4.0 parity)

**Date:** 2026-05-15
**HEAD at start:** `533200c` (v0.2.64-g3)
**HEAD at end:** new commit on this branch; tag `v0.2.65-g3`.
**Goal achieved:** Move `.rodata` sections that contain `R_PPC_ADDR32`
relocations against undef externs from `__TEXT,__const` to
`__DATA,__const`, mirroring gcc-4.0's convention; emit classic-format
external relocations so dyld writes the bound target VA into the slot
at load time. Closes the pointer-equality gap left open by v0.2.64.

## Arrival state

v0.2.64 made `static const fn_t arr[] = { extern_func, ... }` callable
by allocating a 16-byte stub per matching undef function-pointer
ADDR32 from `.rodata` and baking the stub VA into the table entry.
The fix solved the practical problem (sed's `dfa.c::prednames[]`,
SIGSEGV on POSIX character classes) but left pointer-equality wrong.

Empirically confirmed on imacg3 before any changes this session, with
a small probe program declaring three references to `_isalpha`:

```
table[0]    =0x1d30   (stub VA, v0.2.64 trick)
global_q    =0x1d30   (stub VA, same path through ADDR32 in .data)
local_q     =0x900b13dc  (libSystem VA, loaded via __nl_symbol_ptr indirection)
```

For comparison, gcc-4.0 on the same source gives:

```
table[0]    =0x900b13dc
global_q    =0x900b13dc
local_q     =0x900b13dc
```

The `q = isalpha` path in expression code resolves via the existing
`__nl_symbol_ptr` slot — tcc's code-gen for `fn_t q = extern_fn` emits
a `lis/lwz` indirect load, so `q` ends up with the dyld-bound target
VA. The static-const slot in `__TEXT,__const` couldn't go through the
same mechanism because dyld can't bind into a read-only segment, hence
the v0.2.64 stub trick.

gcc-4.0 on Tiger handles this by **placing rodata that has relocations
against undef externs in `__DATA,__const`** (writable at load time, so
dyld can bind into it). Then both `prednames[0]` and `q = isalpha`
end up with the same libSystem VA.

Confirmed empirically by building the probe with gcc-4.0 and inspecting
`otool -l`: gcc places `__const` between `__dyld` and `__bss` in the
`__DATA` segment.

## What landed

### Source

[`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — EXE writer changes
only (the dylib path stays on v0.2.64's stub trick; handoff #2
carries the parallel dylib work).

1. **Detection.** Right after the section-finding loop in
   `macho_output_exe`, walk `rodata->reloc`. If any `R_PPC_ADDR32`
   targets an undef sym, set `rodata_in_data = 1`. Defined-target
   ADDR32s stay in `__TEXT,__const`; they're link-time-baked.

2. **Section counting.** `n_text_sects` includes rodata only when
   `!rodata_in_data`. `n_data_sects` includes rodata when
   `rodata_in_data`. So the same merged rodata section gets
   accounted in exactly one of the two segments.

3. **Layout.** The `__TEXT` layout pass now conditions its rodata
   block on `!rodata_in_data`. The `__DATA` layout pass adds a new
   rodata block between `__dyld` and `__bss` (matching gcc-4.0's
   ordering on Tiger).

4. **LC_SEGMENT emission.** The `__TEXT` segment's section list
   emits `__TEXT,__const` only when `!rodata_in_data`. The `__DATA`
   segment's list adds `__DATA,__const` when `rodata_in_data`,
   placed between `__dyld` and `__bss`.

5. **Extrel toggle.** In the `exe_resolve_section_relocs` setup
   block, `ctx.extrels_out` is enabled before the rodata resolve
   call when `rodata_in_data` is set (and stays enabled for the
   writable data sections that follow, as before).

6. **`wants_extrel` predicate widened.** Was
   `(UNDEF && !STT_FUNC && !has_stub)`. Now just `(UNDEF)`. This
   was the key insight: the STT_FUNC exclusion existed because
   `v0.2.23` allocated stubs for STT_FUNC ADDR32 and the writer
   used the stub VA — but that produced a stub VA in the slot, not
   a libSystem VA. With extrel emission for STT_FUNC undefs in
   writable sections, the slot is bound to the libSystem function
   VA, matching gcc. The stub allocated by `collect_extern_stubs`
   stays useful for REL24 callers — the ADDR32 path just bypasses
   it.

7. **File-write order.** The rodata payload write moves with the
   layout: `if (rodata && !rodata_in_data)` writes it just after
   `text_data` (the old position); a new write in the `__DATA`
   sequence between `__dyld` and `__bss` handles the moved case.
   This change is non-cosmetic — see the bug story below.

### Bug story along the way

First build produced a binary that immediately SIGILL'd. The probe
program executed zero instructions of `main` — gdb reported the crash
at `0x1ba0`, inside `__symbol_stub`. Disassembly showed the **stubs
were all zeros**. The crash was the CPU trying to execute the zero
instruction (illegal).

Root cause: the file-write loop in `macho_output_exe` had a hardcoded
`if (rodata) { pad-to-file_off; write rodata; }` *before* the stub
write. The earlier file_off was in the `__TEXT` region; after my
layout change, rodata's file_off was in the `__DATA` region (much
higher). The `while (out.len < sects[sect_idx_rodata].file_off) put8(0)`
loop padded forward past every section between, including stubs.
Then the stub write hit `while (out.len < sects[sect_idx_stub].file_off)`
with `out.len` already past it (no-op) and `obuf_put(stubs)` just
appended the stub bytes at whatever buffer position out.len was, far
beyond the stub section's file_off. The on-disk stub bytes stayed at
their declared file_off — all zeros (the padding) — and the appended
stub bytes ended up somewhere in the file's tail, unreachable.

Fix: guard the early rodata write on `!rodata_in_data`, and add a
matching write in the `__DATA` section sequence after `__dyld` so
the file order stays monotonic.

This is the kind of bug the post-write linter (v0.2.60) is supposed
to catch — and it's worth noting that **it didn't catch this one**:
the section sizes and ranges all looked valid; only the *content* of
the stub bytes was wrong (zeros instead of `lis`/`lwz`/`mtctr`/`bctr`).
That's a content check the linter doesn't make. Not a regression of
the linter; more of a "post-write linters validate structure, not
semantics" observation. The crash was loud and immediate so the
debugging round-trip was quick.

### Docs

* [`docs/roadmap.md`](../../../docs/roadmap.md) — new v0.2.65-g3 row
  at the top; arrival note bumped to "shipped through v0.2.65-g3";
  release counter `42 → 43`.
* [`docs/sessions/088-rodata-reclass-data-const-2026-05-15/README.md`](README.md) —
  this file.
* `HANDOFF.md` (next).

### Demos

* [`demos/v0.2.65-rodata-data-const.{c,sh}`](../../../demos/v0.2.65-rodata-data-const.sh) —
  new demo. Compiles a program with `static const fn_t prednames[] = {isalpha}`,
  `static fn_t global_q = isalpha`, and a local `fn_t local_q = isalpha`
  via the `.o` roundtrip path. Asserts:
  * `__const` lives in `__DATA` (not `__TEXT`).
  * A classic-format external relocation targets `_isalpha` at an
    address inside `__DATA,__const`.
  * At runtime: all three pointers resolve to the same libSystem
    VA, and calls through each return the expected value.
* [`demos/v0.2.64-funcptr-const.sh`](../../../demos/v0.2.64-funcptr-const.sh) —
  updated. The old assertion pinned the v0.2.64 layout (slot points
  into `__symbol_stub`); v0.2.65 changed the layout. The demo now
  reports the placement for context and does a functional check on
  the program. The original assertion was a v0.2.64-specific
  implementation detail; the functional check is what actually
  matters for the bug class.
* [`demos/README.md`](../../../demos/README.md) — new v0.2.65 row,
  v0.2.64 row description updated.

### Memory updates

None. Session learnings are factual Mach-O / dyld details that
belong here in the session README, not user-spanning memory.

## Verification

On imacg3:

* `PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure && build` — clean.
* `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` — **fixpoint holds**.
* `./scripts/run-tests2.sh` — **111 / 111**.
* `(cd tcc/tests && make abitest CC=gcc-4.0)` — **48 successes**.
* `(cd tcc/tests && make abitest-tcc)` — **24 successes**.
* Real-world demos run end-to-end (lua, bzip2, cjson, gzip, sed
  including `[[:class:]]` tests, dylib-slide, alacarte,
  gdebug-extern, gdebug-link, v0.2.63-extern-data-ref). All PASS.
  (v0.2.23-sqlite-file demo fails for environmental reasons only —
  `$SQLDIR` not present on this host; unrelated to this change.)
* `./demos/v0.2.65-rodata-data-const.sh` — **PASS**:
  ```
  __const segname = __DATA
  extrel at 0x0000205c → _isalpha (inside __DATA,__const [0x205c,0x2219))
  prednames[0] = 0x900b13dc
  global_q     = 0x900b13dc
  local_q      = 0x900b13dc
  ```
* `./demos/v0.2.64-funcptr-const.sh` — **PASS** (reports
  `__const placed in __DATA (v0.2.65 layout — dyld binds via extrel)`).
* `DYLD_PRINT_BINDINGS=1 /tmp/probe2` shows:
  ```
  dyld: bind: probe2:0x0000205c = libSystem.B.dylib:_isalpha, *0x0000205c = 0x900b13dc
  dyld: bind: probe2:0x00002000 = libSystem.B.dylib:_isalpha, *0x00002000 = 0x900b13dc
  ```
  — one extrel for the `__const` slot, one for the `.data` slot,
  both bound to the same libSystem function VA.

## Status of session 087's open items

| # | Item | Status |
|---|---|---|
| (087 #1, NEW) | `.rodata` reclassification to `__DATA,__const` | **CLOSED → v0.2.65-g3** |
| (087 #1, sub) | `const T *const p = &extern_data` exotic case | closed too — the widened `wants_extrel` predicate covers it (no rodata-move required because the case is mostly in `.data` anyway; but if the user puts the const-pointer in rodata, that section now moves to `__DATA,__const` and the slot gets the libSystem data VA via extrel) |
| (087 #2, carried 086 #2) | Dylib writer extrel emission | unchanged — still the natural follow-up |
| (087 #3, carried 067 #3) | Sibling r11 watch | unchanged |
| (087 #4, carried) | AltiVec intrinsics | unchanged |
| (087 #5, carried) | bcheck.c port | unchanged |
| (087 #6, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (087 #7, optional, carried) | `rsync` exclude-list polish | unchanged |

## Findings worth carrying forward

* **`wants_extrel = (UNDEF)` is the right shape.** The previous
  STT_FUNC and no-stub exclusions were narrowing the path for cases
  that are themselves now correctly handled by extrel. Once you
  decide the slot is in a writable section, the writer should just
  let dyld bind whatever the symbol is — function or data — and not
  try to be clever about what kind of address belongs there. The
  stub-allocation logic in `collect_extern_stubs` continues to
  serve REL24 callers, but the ADDR32 path is now uniform.

* **`__DATA,__const` is normal-writable, not read-only-after-bind.**
  gcc-4.0 on Tiger emits `__DATA,__const` with `S_REGULAR` and no
  special protection; the entire `__DATA` segment is RW at runtime.
  The "const" promise is at the C/source level (the compiler won't
  emit stores into it), not enforced by the loader. This means we
  don't need any `mprotect`/segprot dance — moving a section from
  `__TEXT` to `__DATA` is free, modulo the layout shifts.

* **Post-write linters validate structure, not semantics.** The
  all-zero stubs bug got through v0.2.60 because section ranges
  and command counts all looked valid; only the bytes inside the
  stub section were wrong. Catching this would require an
  emit-side check that the first instruction of every stub
  matches the expected `lis r12, *` pattern. Not adding that
  here — the crash was loud and immediate, the diagnostic value
  of a structural check pre-shipping each release outweighs the
  cost of adding content checks. Worth noting if a future bug
  bypasses the structural sanity but corrupts content silently.

* **The file-write loop's section ordering is implicit
  monotonicity.** The early-versus-late writes for `rodata` /
  `stubs` / `eh_frame` / data sections assume each `file_off`
  is greater than the buffer position when we reach it. Adding
  a new section in the middle of the file order requires moving
  its write block to the matching position in the loop. The
  layout pass `cur_off` arithmetic is symmetric — both have to
  agree on the order, or padding goes wrong.

## What's next

Concrete follow-ups, in rough priority order:

1. **Dylib writer extrel emission (handoff #2 from 087/086).** The
   dylib equivalent of this session's work. Two extra wrinkles:
   dylibs are slid at load time so `r_address` semantics differ
   slightly, and `MH_DYLIB` extrel handling under Tiger dyld needs
   empirical verification. No in-tree dylib trips this today.

2. **Try a new real-world program.** With sed, gzip, lua, cjson,
   bzip2, sqlite all working, the next high-leverage exploration
   is something like `awk` (one-true-awk), `m4`, `make`, `expat`,
   `libpng`, or `vim` — see the menu in session 087's HANDOFF.

3. **AltiVec intrinsics / `bcheck.c` port / sibling r11 watch / `rsync`
   polish / dead backup deletion** — all unchanged from session 087.
