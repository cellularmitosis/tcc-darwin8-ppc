# Session 039 — unsupervised cleanup pass, 2026-05-03

Picking up at v0.2.8-g3 from session 038. The user said "continue
unsupervised, document everything, cut releases as appropriate."
Roadmap had five remaining structural items (#1–#7 minus the two
just shipped) plus 13 concrete test failures. This session knocks
off three structural items, picks up one test, and lands a build-
infrastructure improvement that unlocks future work.

## Headline result

| | start | end |
|---|---|---|
| HEAD | `56996ae` | (this session's HEAD, see exit state) |
| tests2 NORUN=true | 105 / 118 (89.0%) | **106 / 118 (89.8%)** |
| Bootstrap fixpoint | holds | holds |
| Roadmap structural items | 5 open | 2 open (#1, #5, #6 closed) |

## What landed

### `00751c8` — Roadmap #5: delete dead `ppc-macho-stubs.c`

The stubs file had been dead since session 009 (`ppc-macho.c`
subsumed all its symbols), but had been left in place as
historical scaffolding. Verified no live references in the build
(`tcc/Makefile`'s `ppc-osx_FILES = $(ppc_FILES) ppc-macho.c` —
no `ppc-macho-stubs.c`). Deleted the file, refreshed the now-
confusing comments inside `ppc-macho.c`, updated the README's
source-layout block.

Also opportunistically bumped the README headline + status table
to v0.2.8-g3 (the real-atomics release was already shipped from
session 038, but the README hadn't caught up due to the user's
in-flight logo work).

### `dc7a05d` — Roadmap #6: dedup UNDEF nlist entries

Verified the bug:
```
$ tcc 'extern void exit(int); int main(){exit(0);}'
$ nm a.out | grep '^         U'
   U _atexit  ← duplicated
   U _atexit
   U _exit
   U _exit
```

Root cause: in the EXE writer's symtab build, two independent
loops emit UNDEF nlist entries — one per `stubs[]` element (for
function-call references via R_PPC_REL24) and one per `nlptrs[]`
element (for data-pointer references via R_PPC_ADDR32). When the
same elfsym is in both lists — as `_atexit`/`_exit` are because
crt1.o has both `bl atexit` calls and ADDR32 references in its
static-init machinery — both loops emit a fresh UNDEF entry with
its own strtab allocation.

Wasteful but not actually broken: each duplicate cost a strtab
byte + an nlist entry + a redundant dyld lookup.

Fix: build a per-pass `elfsym_idx → nlist_idx` map; emit one
UNDEF per unique elfsym, with `stub_sym_idx[]` and
`data_sym_idx[]` sharing the same nlist index. After fix: one
`_atexit`, one `_exit`. ~50 lines of refactoring.

### `a4bb448` — 104_inline test fix: emit N_WEAK_REF for STB_WEAK undefs

`104_inline` had been failing under `-o exe` (passing under
`-run`), one of the inverse-direction failures noted in session
037. Diagnosis:

```
$ tcc 104+_inline.c 104_inline.c -o 104.exe
$ ./104.exe
dyld: Symbol not found: _inline_inline_2decl_only
  Referenced from: 104.exe
  Expected in: /usr/lib/libSystem.B.dylib
```

The test declares various inline functions (some defined, some
forward-declared only) and uses `__attribute__((weak))` in a
separate TU to test which ones got externally emitted:

```c
#define GOT(f) \
    __attribute__((weak)) void f(void); \
    printf("%d %s\n", !!((__SIZE_TYPE__)f & ~0u), #f);
```

The expected behavior: weak undef references resolve to address
0 if not found, programs detect via `!!&f`. Our Mach-O EXE writer
was emitting all UNDEF nlist entries with `n_desc = (libord << 8)`
regardless of binding. For STB_WEAK symbols, dyld saw a hard UNDEF
and aborted before main.

Fix: when emitting a UNDEF, check `ELFW(ST_BIND)(esym->st_info)`,
and if STB_WEAK, OR `N_WEAK_REF` (0x40) into n_desc's low byte.
Trivial change, single test win.

### `ba7848b` + `defd38c` — Roadmap #1: lwarx/stwcx atomics (1, 2, 4-byte)

Replace the pthread_mutex atomics from session 038 with lock-free
`lwarx`/`stwcx.` implementations.

Build infrastructure was the gating issue: tcc's PPC backend
doesn't emit inline asm, and its built-in assembler doesn't cover
lwarx/stwcx. Solution: add a per-file Makefile rule that routes
`atomic-ppc.S` through `gcc-4.0` instead of tcc, overriding the
generic `%.S → %.o` rule. This unblocks future asm work too.

`ba7848b` ships 4-byte atomics. `defd38c` extends with 1- and
2-byte via word-RMW with masking — PPC32 has no
`lbarx`/`sbarx`, so byte/short ops `lwarx` the containing 4-byte
word, mask out the byte/short region, OR in the new value, and
`stwcx` the whole word. Reservation tracks the word, so
concurrent RMW of the same byte from multiple threads serializes
correctly.

Big-endian byte/short addressing handled via:
* byte at A: `shift = ((A^3) & 3) << 3 = (3-(A&3))*8`
* half at A: `shift = ((A^2) & 2) << 3 = (1-((A>>1)&1))*16`

Coverage in `atomic-ppc.S`:
* `__atomic_{load,store,exchange,compare_exchange}_{1,2,4}`
* `__atomic_fetch_{add,sub,and,or,xor,nand}_{1,2,4}`

`atomic_flag_test_and_set` / `clear` in lib-ppc.c rewrapped to
call `__atomic_exchange_1` / `__atomic_store_1` — they're
lock-free too.

What stays in `lib-ppc.c` under the global pthread_mutex:
* 8-byte ops (PPC32 has no `ldarx`/`stdcx` — those are PPC64).

**Performance**: `124_atomic_counter` (16 threads × 65535 ops ×
4 widths):

| | runtime |
|---|---|
| v0.2.8-g3 (mutex-only) | 6m23s |
| `ba7848b` (4-byte lock-free, byte/short still mutex) | 5m29s |
| `defd38c` (1, 2, 4-byte all lock-free) | **2.4s** |

137× speedup vs v0.2.8 baseline. The full tests2 suite wall time
drops with it.

## Files touched

* `tcc/ppc-macho-stubs.c` — deleted
* `tcc/ppc-macho.c` — UNDEF dedup, N_WEAK_REF emission, comment cleanup
* `tcc/lib/atomic-ppc.S` — new file, lwarx/stwcx 4-byte atomics
* `tcc/lib/lib-ppc.c` — drop 4-byte atomic stubs, refresh comment
* `tcc/lib/Makefile` — per-file rule for atomic-ppc.S via gcc-4.0
* `README.md` — status table to v0.2.8-g3, layout block refresh
* `docs/roadmap.md` — mark #5 + #6 done

## Findings see findings.md

PPC ABI / Mach-O details that should outlast this session.

## Recommended next session

* Roadmap #2: 73_arm64 long-double struct GPR-overflow fix.
* Roadmap #3: Mach-O `tcc -ar` driver.
* Roadmap #4: archive alacarte loader.
* Roadmap #7: better self-link diagnostics.
* The big lift remaining: bcheck.c port (5 tests + 3 unblocks).
