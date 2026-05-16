# Handoff — end of session 088 (2026-05-15)

## TL;DR

**v0.2.65-g3 shipped.** Closes the pointer-equality gap left open by
v0.2.64. `.rodata` carrying `R_PPC_ADDR32` against undef externs now
moves at link time from `__TEXT,__const` to `__DATA,__const`, and
classic-format external relocations get emitted so dyld binds the
slot values at load time. Matches gcc-4.0 on Tiger: `prednames[0]`,
a `.data`-resident `static fn_t global_q = isalpha;`, and a local
`fn_t q = isalpha;` all resolve to the same libSystem function VA.

* **HEAD at session start:** `533200c` (end of session 087).
* **Tag:** `v0.2.65-g3`.
* **tests2:** 111 / 111 (unchanged).
* **abitest:** cc 48 + tcc 24 (unchanged).
* **Bootstrap fixpoint:** holds.
* **Real-world demos verified:** lua, bzip2, cjson, sed (incl
  `[[:class:]]`), gzip, dylib-slide, alacarte, gdebug-extern,
  gdebug-link, v0.2.63-extern-data-ref, v0.2.64-funcptr-const
  (updated), v0.2.65-rodata-data-const (new). All PASS.

## What landed

### Source

[`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — EXE writer changes
only (the dylib writer keeps the v0.2.64 stub trick for now). The
diff has six coupled parts:

1. New `rodata_in_data` decision flag, computed once after the
   section-finding loop. Set if `rodata->reloc` carries any
   `R_PPC_ADDR32` against an undef sym.

2. Section counting: `n_text_sects` counts rodata only when
   `!rodata_in_data`; `n_data_sects` counts rodata when
   `rodata_in_data`.

3. `__TEXT` and `__DATA` layout passes split on the flag. Moved
   rodata sits between `__dyld` and `__bss` (gcc-4.0 placement).

4. LC_SEGMENT cmds and section descriptors emit `__const` in
   exactly one segment based on the flag.

5. `exe_resolve_section_relocs` extrel toggle enables collection
   for moved rodata.

6. `wants_extrel` predicate widened from
   `(UNDEF && !STT_FUNC && !has_stub)` to just `(UNDEF)`. The
   stub allocated by `collect_extern_stubs` stays useful for
   REL24 callers; the ADDR32 path bypasses it now.

A separate file-write coordination change moves the rodata payload
write from after `text_data` to after `__dyld` whenever
`rodata_in_data` is set, so file offsets stay monotonic. First build
crashed with SIGILL because that change was missing: padding ran
forward over the stub bytes, leaving the stub section all zeros on
disk. Fix obvious in hindsight; documented in the session README.

### Docs

* [`docs/roadmap.md`](../../../docs/roadmap.md) — new v0.2.65-g3 row,
  bump "shipped through" + release counter.
* [`docs/sessions/088-rodata-reclass-data-const-2026-05-15/README.md`](README.md) —
  full narrative including the all-zero-stubs SIGILL story.
* This `HANDOFF.md`.

### Demos

* [`demos/v0.2.65-rodata-data-const.{c,sh}`](../../../demos/v0.2.65-rodata-data-const.sh) —
  new. Compiles a program with three references to `isalpha`
  (static-const table, writable static, local var), asserts `__const`
  is in `__DATA` and that an external relocation targets `_isalpha`
  inside `__DATA,__const`, runs the program, verifies all three
  pointers resolve to the same libSystem VA.
* [`demos/v0.2.64-funcptr-const.sh`](../../../demos/v0.2.64-funcptr-const.sh) —
  updated to drop the v0.2.64-specific file-layout pinning (slot
  points into `__symbol_stub`) since v0.2.65 changed the layout.
  Now reports the placement for context and does a functional check.
* [`demos/README.md`](../../../demos/README.md) — table row added
  for v0.2.65, v0.2.64 description updated.

### Memory updates

None.

## Status of session 087's open items

| # | Item | Status |
|---|---|---|
| (087 #1, NEW) | `.rodata` reclassification to `__DATA,__const` | **CLOSED → v0.2.65-g3** |
| (087 #1, sub) | `const T *const p = &extern_data` exotic case | **CLOSED** via widened `wants_extrel` predicate |
| (087 #2, carried 086 #2) | Dylib writer extrel emission | unchanged |
| (087 #3, carried 067 #3) | Sibling r11 watch | unchanged |
| (087 #4, carried) | AltiVec intrinsics | unchanged |
| (087 #5, carried) | bcheck.c port | unchanged |
| (087 #6, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged |
| (087 #7, optional, carried) | `rsync` exclude-list polish | unchanged |

## Open work for next session

### 1. Dylib writer extrel emission (carried from 087 #2 / 086 #2)

`macho_output_dylib` still falls back to slot-address or stub-VA for
undef ADDR32 refs in dylib-resident `__const`. The dylib path didn't
pick up either v0.2.63's `.data` extrel emission or this session's
`__const`-to-`__DATA` routing. Same harder-than-it-sounds set of
issues remains:
* Dylibs are slid by dyld; `r_address` semantics differ.
* The bind would happen relative to the dylib's load address, not a
  fixed VA.
* Tiger's dyld treats `VANILLA` in `MH_DYLIB` with slide-aware
  semantics — needs empirical verification.

No in-tree dylib trips this today (the dylib demos all use function
references that go through the v0.2.25 PIC stubs). Easiest way to
make a repro is to put a function-pointer table in a dylib's
`static const` and try to call through it from a `dlopen`'d driver.

### 2. Sibling r11 watch (carried, 067 #3)

PPC sibling-register concern to v0.2.48's r12 fix. csmith hasn't
surfaced a repro yet.

### 3. AltiVec intrinsics (carried)

PowerPC SIMD. Multi-session lift.

### 4. `bcheck.c` port (carried)

PowerPC port of tcc's bounds-check runtime. Multi-session lift.

### 5. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3

### 6. (optional, low priority) `rsync` exclude-list polish

## How to pick up

### Quick sanity check on imacg3

```sh
ssh imacg3
cd ~/tcc-darwin8-ppc
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh configure
PATH=/opt/make-4.3/bin:$PATH ./scripts/build-tiger.sh
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh
./demos/v0.2.65-rodata-data-const.sh
./demos/v0.2.64-funcptr-const.sh
./demos/v0.2.40-sed.sh
```

### Verify the change end-to-end

```sh
ssh imacg3
T=/Users/macuser/tcc-darwin8-ppc/tcc
cat > /tmp/eq.c <<'EOF'
#include <ctype.h>
#include <stdio.h>
#undef isalpha
extern int isalpha(int);
typedef int (*fn_t)(int);
static const fn_t prednames[] = { isalpha };
static       fn_t global_q    = isalpha;
int main(void) {
    fn_t local_q = isalpha;
    printf("%p %p %p match=%d/%d call=%d\n",
           (void*)prednames[0], (void*)global_q, (void*)local_q,
           prednames[0] == global_q, prednames[0] == local_q,
           prednames[0]('A'));
    return prednames[0] == global_q && prednames[0] == local_q ? 0 : 1;
}
EOF
$T/tcc -B$T -L$T -I$T/include -c -o /tmp/eq.o /tmp/eq.c
$T/tcc -B$T -L$T -I$T/include -o /tmp/eq /tmp/eq.o
/tmp/eq; echo "exit=$?"
# Expected: three matching libSystem VAs (e.g. 0x900b13dc), match=1/1, call=1, exit=0.
otool -l /tmp/eq | grep -A1 "sectname __const"
# Expected: segname __DATA (not __TEXT).
otool -rv /tmp/eq | head
# Expected: External relocations, including _isalpha targeting __DATA,__const.
```

### Pick a direction

(a) **Dylib writer extrel emission** — the parallel work to this
session's EXE-side change. Harder than EXE because dylibs slide;
needs empirical Tiger dyld verification. No in-tree dylib trips
this today, so the first task is to write a repro (function-pointer
table in a dylib, called from a `dlopen`'d driver).

(b) **Try another real-world program** — `awk` (one-true-awk),
`m4`, `make`, `expat`, `libpng`, `vim`. Each new program is high
leverage either way: passes (validates our work) or surfaces a real
bug to fix. With v0.2.65's gcc-4.0 parity for `__const` slots, more
real-world C should "just work" on the first try.

(c) **AltiVec intrinsics** — PowerPC SIMD. Multi-session lift, low
priority unless a specific program needs it.

(a) is the cleanest follow-up to this session. (b) is the
high-variance bet. Neither blocks the other.

## Subagent log

None this session — direct edits with iterative verification on
imacg3.

## Closing notes

Three takeaways worth carrying forward (also in the session README):

1. **`wants_extrel = (UNDEF)` is the right shape.** The STT_FUNC
   and no-stub exclusions from v0.2.63 were narrowing the path for
   cases that are themselves now correctly handled by extrel. Once
   you decide the slot is in a writable section, the writer should
   just let dyld bind whatever the symbol is. The stub-allocation
   logic in `collect_extern_stubs` still serves REL24 callers; the
   ADDR32 path is uniform.

2. **`__DATA,__const` is normal-writable, not read-only-after-bind.**
   gcc-4.0 emits it with `S_REGULAR` and no special protection. No
   `mprotect`/segprot dance needed when moving a section from
   `__TEXT` to `__DATA`. The "const" promise is C-level, not
   loader-enforced.

3. **Post-write linters validate structure, not semantics.** The
   all-zero-stubs bug got through v0.2.60 because section ranges and
   command counts all looked valid; only the bytes inside the stubs
   were wrong. Future stub-content corruption that doesn't break
   structure would similarly slip through. Worth knowing.

Next session: [docs/sessions/088-rodata-reclass-data-const-2026-05-15/HANDOFF.md](docs/sessions/088-rodata-reclass-data-const-2026-05-15/HANDOFF.md)
