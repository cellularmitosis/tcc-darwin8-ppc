# Handoff — end of session 086 (2026-05-15)

## TL;DR

**v0.2.63-g3 shipped.** Closes the long-deferred extern-*data*-reference
correctness gap from session 044: `int *p = &optind;` (where `optind`
is `extern int` from libSystem) now resolves correctly to `&optind`
at load time, not to the address of the `__nl_symbol_ptr` slot. The
exe writer now emits classic Mach-O external relocations
(`struct relocation_info`, type=VANILLA, extern=1) in `__LINKEDIT`,
and Tiger's `dyld doBindExternalRelocations` fills them — matches
gcc-4.0's behavior exactly.

* **HEAD at session start:** `f4abf9e` (end of session 085).
* **HEAD at session end:** see commit log.
* **Tag:** `v0.2.63-g3`.
* **tests2:** 111 / 111 (unchanged).
* **Bootstrap fixpoint:** holds.
* **Real-world demos verified:** lua, bzip2, cjson, sed, gzip,
  dylib-slide, alacarte, gdebug-extern, gdebug-link. All PASS.

## What landed

### Source

[`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — ~80 line diff in the
EXE writer:

* New `struct exe_extrel` for collected external relocations.
* Three new fields on `exe_reloc_ctx`: `extrels_out`,
  `n_extrels_out`, `extrels_cap_out` (NULL means "skip extrel
  collection" — the dylib writer keeps this disabled for now).
* In `exe_resolve_section_relocs::R_PPC_ADDR32`: when the symbol
  is `SHN_UNDEF && !STT_FUNC && !has-stub`, push an extrel record
  and zero `target_addr` so the in-place addend stays (Tiger's
  dyld treats VANILLA externs as ADD, not OVERWRITE, so addends
  survive).
* In `macho_output_exe`: enable extrel collection per-section —
  only for writable sections (data / init_array / fini_array).
  Read-only sections (text / rodata→__const / eh_frame / DWARF)
  keep the legacy slot-address fallback; emitting an extrel for a
  location dyld can't write would SIGBUS during load.
* Resolve `elfsym_idx → machsym_idx` for each extrel during the
  undef-nlist construction pass (reuses `elfsym_to_undef[]`).
* Allocate `n_extrels * 8` bytes between nlist and indirect in
  `__LINKEDIT`; update `extreloff` / `nextrel` in LC_DYSYMTAB;
  emit the `relocation_info` bytes (BE, packed via the existing
  `pack_reloc_word` helper).
* `tcc_free(extrels)` at cleanup.

### Docs

* [`docs/roadmap.md`](../../../docs/roadmap.md) — new v0.2.63-g3
  row + updated item #10 wording (the deferred extern-data sub-item
  is now closed).
* [`docs/sessions/086-extern-data-ref-2026-05-15/README.md`](README.md) —
  full bug analysis, fix design, verification log.
* [`docs/sessions/086-extern-data-ref-2026-05-15/repro/`](repro/) —
  four C reproducers (primary, addend semantics, .o-roundtrip,
  multi-extrel, ctype-in-const that surfaced the writable-section
  gate need).
* This `HANDOFF.md`.

### Demo

* [`demos/v0.2.63-extern-data-ref.{c,sh}`](../../../demos/v0.2.63-extern-data-ref.sh)
  — compiles a program with `int *p = &optind; int *q = &optind + 3;`,
  verifies `nextrel = 2` in LC_DYSYMTAB, runs, asserts both
  pointers resolve correctly (including the +12 addend).
* [`demos/README.md`](../../../demos/README.md) — table row added.

### Memory updates

None. Session learnings are factual codegen / dyld details that
belong in the session's README, not user-spanning memory.

## Status of session 085's open items

| # | Item | Status |
|---|---|---|
| (085 #1, carried 067 #3) | Sibling r11 watch | unchanged |
| (085 #2, carried) | AltiVec intrinsics | unchanged |
| (085 #3, carried) | bcheck.c port | unchanged |
| (085 #4, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (085 #5, optional, carried) | `rsync` exclude-list polish | unchanged |
| (085, possible direction) | Extern-data-reference fix | **CLOSED → v0.2.63-g3** |

## Open work for next session

### 1. .o-roundtrip STT_FUNC loss (NEW)

Surfaced during this session's sed verification. Mach-O nlist has
no STT_FUNC bit, so when tcc compiles `foo.c → foo.o` and another
TU then references `&foo_func` via `R_PPC_ADDR32` in a `.o` file
that gets read back, the function-ness is lost on the read side.
This currently affects:

* sed's `dfa.c::prednames[]` (ctype function pointers in
  `__TEXT,__const`) — the table is broken at runtime (calling
  `prednames[i].func(c)` jumps to data), but sed's `--version`
  works because the table is never indexed. Pre-existing bug, my
  v0.2.63 fix had to gate around it (don't emit extrels into
  `__const` because that'd SIGBUS at load).

Concrete fix shape: at link time, walk all REL24 relocs across
all loaded sections; for each undef sym that appears as a REL24
target, mark `ELFW(ST_INFO)` to include `STT_FUNC`. Then
`collect_extern_stubs` allocates a stub for those even when their
`__const`-resident ADDR32 ref is the only direct evidence.

Multi-session-light task. Would also let v0.2.63's extrel gate
relax to include `__const`.

### 2. dylib writer extrel emission (NEW, deferred)

`macho_output_dylib` still falls back to slot-address for undef
data ADDR32 refs. Adding the same fix is harder because:
* Dylibs are slid by dyld; `r_address` semantics differ.
* The bind would happen relative to the dylib's load address,
  not a fixed VA.
* Tiger's dyld treats VANILLA in `MH_DYLIB` with slide-aware
  semantics — needs empirical verification.

No in-tree dylib trips this today (the dylib demos all use
function references, which go through stubs).

### 3. Sibling r11 watch (carried, 067 #3)

PPC sibling-register concern to v0.2.48's r12 fix. csmith hasn't
surfaced a repro yet.

### 4. AltiVec intrinsics (carried)

PowerPC SIMD. Multi-session lift.

### 5. `bcheck.c` port (carried)

PowerPC port of tcc's bounds-check runtime. Multi-session lift.

### 6. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3

### 7. (optional, low priority) `rsync` exclude-list polish

## How to pick up

### Quick sanity check on imacg3

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.63-extern-data-ref.sh
```

### Verify the fix end-to-end

```sh
ssh imacg3
T=/Users/macuser/tcc-darwin8-ppc/tcc
cat > /tmp/ed.c <<'EOF'
#include <unistd.h>
#include <stdio.h>
extern int optind;
int *p = &optind;
int main(void) {
    optind = 7;
    printf("p == &optind: %d\n", p == &optind);
    printf("*p == 7:      %d\n", *p == 7);
    return (p == &optind && *p == 7) ? 0 : 1;
}
EOF
$T/tcc -B$T -L$T -I$T/include -o /tmp/ed /tmp/ed.c && /tmp/ed
otool -rv /tmp/ed   # should show 1 VANILLA external reloc for _optind
```

### Pick a direction

The natural follow-ups are (a) close the .o-roundtrip STT_FUNC-loss
problem, which would tighten the v0.2.63 fix to cover `__const`
function-pointer tables too, or (b) port v0.2.63's external-reloc
emission to the dylib writer.

Neither blocks new real-world program builds; another "pick a
candidate program, try the build, fix what surfaces" session
(ncurses / expat / libpng / vim / make / m4 / awk) remains a
high-leverage choice as well.

## Subagent log

None this session — direct edits to a known file, single-pass
implementation with iterative verification on imacg3.

## Closing notes

Three takeaways worth carrying forward:

1. **Tiger's dyld treats VANILLA externs as ADD.** Empirically
   verified: `int *q = &optind + 3` baked the addend (12) into the
   in-place word, and the linked exe ran with `q == &optind + 12`.
   This matches the standard semantics but worth knowing for any
   future code that emits extrels with non-zero addends.

2. **Mach-O nlist drops STT_FUNC.** Anything that depends on
   "is this undef sym a function?" needs to be either (a) at
   codegen time (before the .o write) or (b) reconstructed from
   reloc shape (REL24 against the sym = function). The
   `collect_extern_stubs` STT_FUNC check is brittle for that
   reason — sed's `prednames[]` table is the canonical sufferer.

3. **Writable-section gating matters for runtime relocations.**
   dyld can write to `__DATA`, can't write to `__TEXT` (including
   `__const`). Any future writer feature that puts dyld bind
   instructions into a Mach-O image needs the same gate.

Next session: [docs/sessions/086-extern-data-ref-2026-05-15/HANDOFF.md](docs/sessions/086-extern-data-ref-2026-05-15/HANDOFF.md)
