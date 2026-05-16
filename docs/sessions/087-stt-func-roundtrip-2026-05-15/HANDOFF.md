# Handoff — end of session 087 (2026-05-15)

## TL;DR

**v0.2.64-g3 shipped.** Closes the residual `.o`-roundtrip
STT_FUNC-loss problem flagged by session 086 — sed's
`dfa.c::prednames[]` and similar function-pointer tables in
`__TEXT,__const` finally work end-to-end.

* **HEAD at session start:** `9a0ad5d` (end of session 086).
* **HEAD at session end:** `533200c`.
* **Tag:** `v0.2.64-g3`.
* **tests2:** 111 / 111 (unchanged).
* **Bootstrap fixpoint:** holds.
* **Real-world demos verified:** lua, bzip2, cjson, sed (now with
  `[[:class:]]` tests), gzip, dylib-slide, alacarte, gdebug-extern,
  gdebug-link, v0.2.63-extern-data-ref. All PASS.

## What landed

### Source

[`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — ~30 line addition
at the top of `collect_extern_stubs`, right after `stub_for[i] = -1`
initialization. A pre-pass walks all sections' reloc tables once
and paints `STT_FUNC` on every undef sym matching either:

* `R_PPC_REL24` target — `bl` is always a function call.
* `R_PPC_ADDR32` target from a `.rodata`-class section
  (`.rodata`, `.rodata.X`, `.data.ro` — the inputs to
  `__TEXT,__const`).

With STT_FUNC restored, the existing v0.2.23 stub-allocation logic
catches these symbols on its main pass; the writer's ADDR32 path
returns the stub VA via `exe_sym_addr`, baking a callable address
into the table entry instead of the `__nl_symbol_ptr` slot's
address.

The mutation is link-time-only: `classify_sym` continues to emit
undef syms as `N_EXT|N_UNDF` so re-emitted `.o` files don't
accumulate spurious FUNC bits.

### Docs

* [`docs/roadmap.md`](../../../docs/roadmap.md) — new v0.2.64-g3
  row; item #10 sub-bullet updated to reflect the closed sed
  prednames[] case.
* [`docs/sessions/087-stt-func-roundtrip-2026-05-15/README.md`](README.md) —
  full bug analysis, why the handoff's single-heuristic prescription
  was insufficient for sed (Tiger's ctype macros), the
  two-heuristic fix, verification, and follow-ups.
* [`docs/sessions/087-stt-func-roundtrip-2026-05-15/repro/funcptr_const.c`](repro/funcptr_const.c) —
  minimal reproducer.
* This `HANDOFF.md`.

### Demos

* [`demos/v0.2.64-funcptr-const.{c,sh}`](../../../demos/v0.2.64-funcptr-const.sh) —
  new demo. Compiles via `.o` roundtrip, parses `otool -l` to find
  `__symbol_stub` / `__nl_symbol_ptr` VA ranges, reads the first
  `fn` pointers out of `__TEXT,__const`, asserts they fall inside
  `__symbol_stub` (not `__nl_symbol_ptr`), runs the program.
* [`demos/v0.2.40-sed.sh`](../../../demos/v0.2.40-sed.sh) — three
  new POSIX-character-class regex tests appended (`[[:alpha:]]`,
  `[[:digit:]]`, `[[:space:]]`), each tagged "was SIGSEGV
  pre-v0.2.64".
* [`demos/README.md`](../../../demos/README.md) — table row added
  for v0.2.64-funcptr-const.

### Memory updates

None. Session learnings are factual codegen / Mach-O ABI details
that belong in the session's README, not user-spanning memory.

## Status of session 086's open items

| # | Item | Status |
|---|---|---|
| (086 #1, NEW) | `.o`-roundtrip STT_FUNC loss | **CLOSED → v0.2.64-g3** |
| (086 #2, NEW deferred) | Dylib writer extrel emission | unchanged |
| (086 #3, carried 067 #3) | Sibling r11 watch | unchanged |
| (086 #4, carried) | AltiVec intrinsics | unchanged |
| (086 #5, carried) | bcheck.c port | unchanged |
| (086 #6, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (086 #7, optional, carried) | `rsync` exclude-list polish | unchanged |

## Open work for next session

### 1. `.rodata` section reclassification for full gcc parity (NEW)

The v0.2.64 stub-trick fix makes calling through `static const`
function-pointer tables work, but doesn't give pointer-equality:

```c
const fn_t *p = prednames[0].fn;   /* = stub VA */
fn_t      *q = isalpha;            /* = libSystem VA via __nl_symbol_ptr */
assert(p == q);                    /* FAILS — different addresses */
```

gcc-4.0 on Tiger gives `p == q` by placing `__const` sections that
contain relocations in **`__DATA,__const`** (writable at load
time) instead of `__TEXT,__const`, then emitting classic-format
extrels so dyld writes the same libSystem VA into both
locations.

To match: at link time, walk every `.rodata` section's reloc table;
if it has any `R_PPC_ADDR32` against an undef sym, route the
section to `__DATA,__const` instead of `__TEXT,__const`. Then
v0.2.63's existing extrel-emission path (now extended to cover
the new writable __const) handles the rest. Touches
`classify_section`, segment grouping, vmaddr assignment.

Multi-session-medium task. Also closes the
`const T *const p = &extern_data` exotic case (currently still
wrong — slot-address fallback or stub-address fallback depending
on heuristic).

### 2. Dylib writer extrel emission (carried from 086 #2)

`macho_output_dylib` still falls back to slot-address for undef
data ADDR32 refs. Same harder-than-it-sounds set of issues:
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
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.64-funcptr-const.sh
./demos/v0.2.40-sed.sh    # now exercises [[:class:]] paths
```

### Verify the fix end-to-end

```sh
ssh imacg3
T=/Users/macuser/tcc-darwin8-ppc/tcc
cat > /tmp/fc.c <<'EOF'
#include <ctype.h>
#include <stdio.h>
#undef isalpha
extern int isalpha(int);
typedef int (*pred_t)(int);
static const struct { const char *name; pred_t fn; } prednames[] = {
    { "alpha", isalpha },
};
int main(void) {
    return prednames[0].fn('A') ? 0 : 1;
}
EOF
$T/tcc -B$T -L$T -I$T/include -c -o /tmp/fc.o /tmp/fc.c
$T/tcc -B$T -L$T -I$T/include -o /tmp/fc /tmp/fc.o
/tmp/fc; echo "exit=$?"
# Expected: exit=0 (was SIGSEGV pre-fix)
```

### Pick a direction

The natural follow-ups are:

(a) **Section reclassification** — move `.rodata` with relocs to
`__DATA,__const`. Gives full gcc-pointer-equality semantics and
also closes the exotic `const T *const p = &extern_data` case.
Concrete: inspect each `.rodata` section's reloc table at link
start; if it has `R_PPC_ADDR32` against any undef sym, override
its `__TEXT` placement to `__DATA`. The writable-section gate in
v0.2.63 then opens up for these sections, and extrel emission
kicks in. Probably 1–2 sessions; needs care in segment grouping
and vmaddr assignment.

(b) **Dylib writer extrel emission** — extend v0.2.63's fix to
`macho_output_dylib`. Harder than EXE because dylibs are slid;
needs empirical verification of Tiger dyld semantics under
`MH_DYLIB`. No in-tree dylib trips this today.

(c) **Try another real-world program** — ncurses / expat / libpng /
vim / make / m4 / awk. With sed character classes working, the
sed demo now exercises a lot more of the regex stack — but other
programs may still surface new bugs.

Neither (a) nor (b) blocks new real-world program builds; (c)
remains a high-leverage exploratory option.

## Subagent log

None this session — direct edits with iterative verification on
imacg3.

## Closing notes

Three takeaways worth carrying forward:

1. **The handoff's heuristic was right but incomplete.** The
   single REL24-target rule from session 086 handles most
   real-world cases (any function called directly somewhere in the
   link). It misses the case where the *only* references are
   ADDR32 from `.rodata` — and Tiger's `<ctype.h>` macros make sed
   exactly that case (no direct `bl _isalpha` exists; every call
   expands to `bl ___istype`). Heuristic (b) catches it.

2. **Mach-O `__const` placement varies by gcc convention.** gcc-4.0
   on Tiger inspects the section's contents and splits: pure RO
   data goes in `__TEXT,__const`, RO data with relocs goes in
   `__DATA,__const`. tcc currently uses `__TEXT,__const` for all
   `.rodata`. Closing this gap is the natural follow-up (#1 above)
   and would unify the fix shape with gcc.

3. **In-place sym mutation is safe within a single link.** The
   pre-pass mutates undef `st_info` to add the STT_FUNC bit, but
   the change is purely link-time — `classify_sym` only consults
   `st_shndx` for the undef path, so re-emitted `.o` files don't
   accumulate spurious FUNC bits. Worth knowing for any future
   pass that needs to refine sym classification at link time.

Next session: [docs/sessions/087-stt-func-roundtrip-2026-05-15/HANDOFF.md](docs/sessions/087-stt-func-roundtrip-2026-05-15/HANDOFF.md)
