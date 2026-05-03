# Handoff — end of session 040 (2026-05-03)

Captured for the next session. Resume cold from this doc.

## TL;DR

- HEAD: `1a163a8` (ppc-macho diagnostics polish)
- Latest release: **v0.2.13-g3** (two releases shipped this session:
  v0.2.12, v0.2.13).
- tests2: **110 / 111 (99.1%)** under `-o exe` path. Only
  `96_nodata_wanted` (BE bitfield) remains.
- Bootstrap fixpoint holds; `tcc -run hello.c` works.
- **lua 5.4.7 builds and runs end-to-end** — first non-trivial
  third-party program that works.
- sqlite3 amalgamation builds; `./sqlite3 -version` works;
  `./sqlite3 :memory: "select 1+1"` still crashes (separate
  codegen bug under investigation).
- All work committed and tagged on `origin/main`.

## What landed since session 039 (v0.2.11-g3)

| Tag | Commits | tests2 | Headline |
|---|---|---|---|
| v0.2.12-g3 | `44c8647`..`8522a94` (2) | 109/111 = 98.2% | struct-deref-by-value + cross-TU PIC reloc translation. lua 5.4.7 builds + runs end-to-end. |
| v0.2.13-g3 | `a0d9e7b`..`1a163a8` (3) | 110/111 = 99.1% | BE Sym-union enum_val clobber fix (60_errors_and_warnings test_scope_1). New scripts/bench.sh: tcc 2s vs gcc-O0 17s vs gcc-Os 41s on lua 5.4.7. |

Roadmap items closed: **#7 (self-link diagnostics)** — partial:
better REL24-OOR error, "no PIC anchor recorded" message includes
section + sym, EXE_MAX_SECTS overflow check.

## Bugs fixed in detail

### 1. Struct-pass-by-value-from-deref (`ppc-gen.c:660`)

`*p` (struct via pointer deref) when passed by value errored
`ppc-gen: deref of basic type 0x7 not yet supported`. gv()
correctly leaves the struct's address in a register; "loading"
should be a register move (`mr`), not a memory load. Fix mirrors
the existing VT_LOCAL+VT_LVAL+VT_STRUCT case at line 968.

Repro:
```c
struct Tok { const char *z; int n; };
void use(struct Tok a);
void f(struct Tok *p) { use(*p); }   // failed before; works now
```

Surfaced trying to compile sqlite3.c at line 176213 (
`sqlite3AddColumn(pParse, yymsp[-1].minor.yy0, ...)`).

### 2. Cross-TU PIC reloc translation (`ppc-macho.c:3754`)

`macho_translate_relocs` unconditionally skipped all scattered
Mach-O relocs, dropping the HA16/LO16 SECTDIFF pairs we ourselves
emit for extern data refs. Result: when linking multiple
tcc-built .o files, the second TU's references to extern data
symbols (`__sF`/stderr, env vars, etc.) read from random `__TEXT`
addresses — the addis/lwz immediates were never reloced.

Fix: translate scattered HA16_SECTDIFF / LO16_SECTDIFF relocs to
R_PPC_HA16_PIC / LO16_PIC, resolve the slot via the input
indirect symtab to the underlying extern, and record the
per-reloc PIC anchor (`ppc_pic_pair_record` promoted from static
to ST_FUNC). Only fires for OBJ/EXE output — `-run` mode keeps
the old skip behavior.

### 3. BE Sym-union enum_val clobber (`tccgen.c:730`)

`sym_link` unconditionally wrote `local_scope` into
`s->sym_scope`. For enum constants, `sym_scope` shares storage
with the low half of `enum_val` (Sym union). On big-endian PPC,
that write clobbered the visible value: every parameter-scope
enum constant came back as 1.

Surfaced via `60_errors_and_warnings test_scope_1`:
```c
int bar(enum ee { a = 12, b = 34 } i) {
    printf("bar %d %d %d\n", i, a, b);  // was "bar 15 1 1", now "bar 15 12 34"
}
```

Tcc already half-acknowledged this on the read side
(`sym_scope_ex` reads scope from the enum tag for IS_ENUM_VAL
syms); fix was the matching guard on the write side.

This is a long-standing tcc upstream bug for BE targets — worth
flagging upstream.

### 4. tests2 60 + 96 pinned to -run on PPC (`tests/tests2/Makefile`)

Both tests use `-dt` (per-test_X discrimination), which only
expands in `TCC_OUTPUT_MEMORY` / `PREPROCESS` modes (see tcc.c:362).
Under our default `NORUN=true` (`-o exe`) path they fail with
"_main not defined". Pin them to `-run` like the existing 125
treatment.

## What's verified at HEAD

* `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` → fixpoint holds.
* `./scripts/run-tests2.sh` → 110/111.
* `tcc -run hello.c` and similar small programs.
* `./demos/v0.2.12-lua.sh` → builds + runs lua 5.4.7.
* `./scripts/bench.sh` → reports lua compile-time benchmark
  numbers.

## One test failure remains

**96_nodata_wanted test_data_suppression_off** — BE bitfield
layout bug. Test:
```c
struct __attribute__((packed)) {
    unsigned x : 12;
    unsigned char y : 7;
    unsigned z : 28, a: 4, b: 5;
} s = { 0x333, 0x44, 0x555555, 6, 7 };

printf("%x %x %x %x %x\n", s.x, s.y, s.z, s.a, s.b);
// expected: 333 44 555555 6 7
// got:      2aa 40 555545 6 7   (x and y collide; z low bits damaged)
```

Two stacked issues per session 039 findings:
1. **Bit ordering**: gcc puts the first declared field at the
   high (MSB) end of the storage word — Apple PPC ABI / standard
   BE bitfield ordering. Tcc puts it at the low (LSB) end.
2. **Mixed-type packing**: `unsigned char y : 7` after
   `unsigned x : 12` placed at the next *byte* boundary after
   x's first byte, not after x ends. Bits collide.

Fix lives in `tccgen.c::struct_layout` (line 4279+). Worth
comparing against tcc's other BE targets (mips, sparc) before
assuming PPC-specific. **Deferred** — deep change with narrow
real-world impact (lua, sqlite, most programs don't use mixed-type
packed bitfields).

## sqlite3_open crash — partial diagnosis

`./sqlite3 -version` and `./sqlite3 .help` work, but anything
involving `sqlite3_open` (memory or file) crashes at SIGSEGV.

Crash location: PC 0x3a45c (varies between builds, but always in
the same nearest-exported region between `sqlite3_status64` and
`sqlite3_deserialize`). The crashing function:
```
0x3a444: lwz r3, -12(r31)    ; load local
0x3a448: li  r4, 0x4
0x3a44c: add r3, r3, r4      ; r3 = local + 4
0x3a450: lwz r3, 0(r3)       ; r3 = *(local+4)
0x3a454: li  r4, -4
0x3a458: add r3, r3, r4      ; r3 -= 4
0x3a45c: lwz r4, 0(r3)       ; CRASH: r3 = 0xfffffffc
```

Pattern: function takes a struct ptr arg, accesses
`*(arg+20)` (treating as int*), subtracts 4 (i.e., ptr[-1]),
dereferences. The function is LL-returning (caller and callee both
do the r3↔r4 swap). The loaded value at `*(arg+20)` is 0
(NULL), leading to `0 - 4 = 0xfffffffc` and a deref crash.

Hypotheses to explore next session:
- The struct field at offset 20 should not be NULL — something
  earlier in the open path returned NULL where it shouldn't.
- Could be related to the **PPC32 shift-count >= 32 bug** flagged
  by the parallel golang/PPC backend session: `uint64(1) << 64`
  returning 1 instead of 0 broke their page allocator. Worth
  checking if any sqlite path computes sizes via shifts that
  would collapse to 0 on PPC32 if the shift count semantics
  differ.
- Stub out subsystems individually until the crash disappears,
  then narrow.
- Try `-DSQLITE_DEBUG=1` for assertion firing (tested briefly
  this session — same crash, different nearest-export name in bt).

Takes sustained focus; left for a future session.

## Improvements to the harness this session

* `scripts/bench.sh` — compile-time benchmark scaffolding that
  builds lua 5.4.7 (33 .c files) twice with each of tcc /
  gcc -O0 / gcc -Os and reports the steady-state second-run
  wall-clock seconds. First run discarded for cache warming.
  Initial numbers on a 900 MHz iBook G3 (PowerPC 750):
  - tcc:         2 s
  - gcc-4.0 -O0: 17 s
  - gcc-4.0 -Os: 41 s

* `demos/v0.2.12-lua.sh` — first runnable third-party demo.
  Fetches lua 5.4.7, compiles all 30+ files with tcc, links to a
  ~565 KB ppc Mach-O exe, runs a small lua program.

* `ppc-macho.c` diagnostic upgrades (REL24-OOR error includes
  sym name + offsets; "no PIC anchor recorded" includes section
  + sym; section count overflow check via EXE_MAX_SECTS macro).

## Other open work (in roughly decreasing impact)

### Roadmap structural items still open

* **#3 — Mach-O `tcc -ar` driver.** tcc's built-in `-ar` only
  knows ELF. Need to parse Mach-O nlist + write
  `__.SYMDEF SORTED` BSD-style with #1/N long names. ~150-300
  lines. Localized.
* **#4 — Mach-O archive alacarte loader.** Currently
  force-whole-archive; would also fix the long-standing
  `libgcc.a` whole-archive crash from session 025. Substantial.
* **#7 — Self-link diagnostics.** Continued from this session
  (REL24, no-PIC-anchor, sect overflow already done). Future:
  un-resolved-relocation site reporting in the EXE writer (when
  the writer can't resolve a target, print sym name + file +
  offset).

### Larger scope

* **bcheck.c port.** Multi-session lift. Would un-skip 6 tests.
* **DWARF debug info emission.** `tccdbg.c` has no PPC support.
  Multi-session.
* **AltiVec intrinsics.** None today. Plausible but big project.

### Real-world program builds

* **sqlite3 amalgamation.** Builds, `-version` works, but
  `select 1+1` crashes (see hypothesis above).
* **lua 5.4.7.** ✅ Works.

## How to pick up

### Quick sanity check
```sh
ssh ibookg37
cd ~/tcc-darwin8-ppc
git status                  # should match origin/main
FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh
./scripts/run-tests2.sh     # ~5 min, expect 110/111
./scripts/bench.sh          # ~2 min, expect tcc<gcc-O0<gcc-Os
./demos/v0.2.12-lua.sh      # ~30 sec, expect "fib(20) = 6765"
```

## Codegen quirks burned in (informational; don't try to "fix" without understanding)

(carrying forward from session 039 + additions this session)

1. **`put_nlist` narrow-int args** — bypassed via inlined byte
   writes in `ppc-macho.c::put_nlist`.
2. **Warning emission via `fprintf+fflush`** — bypassed via
   direct `write(2)` syscall in tcc.c-side warning paths.
3. **64-bit const-fold sign-ext** — partial workaround in
   `tccgen.c::gv`. Deeper fix is the open question from session 031.
4. **char/short param big-endian offset** — `+3 / +2` adjustment
   in `ppc-gen.c::gfunc_prolog`.
5. **LL-helper return r3↔r4 swap** — limited to FP-to-LL helpers.
6. **VLA × callee param-spill safety buffer** — 128 bytes below
   each VLA in `ppc-gen.c::gen_vla_alloc`.
7. **`@hi` vs `@ha` in `lis+ori` vs `lis+addi`** — `ori` is
   zero-extending so paired with `@hi`; `addi` sign-extends so
   paired with `@ha`. See session 037 findings.
8. **N_WEAK_REF in n_desc low byte** for STB_WEAK undef externals.
9. **Variadic FP arg shadow spill** when `gslot >= 8` — must
   write FP value to outgoing param stack.
10. **Struct lvalue with addr in register**: in `ppc-gen.c::load`
    the deref path now treats VT_STRUCT as a register move (`mr`)
    rather than a memory load — the address is already in the
    GPR (NEW THIS SESSION).
11. **Cross-TU SECTDIFF input relocs**: the Mach-O reader
    translates scattered HA16/LO16 SECTDIFF pairs to
    R_PPC_HA16_PIC / LO16_PIC and registers their anchors via
    `ppc_pic_pair_record`. Without this, second-TU extern-data
    refs use stale offsets baked in by the assembler (NEW THIS
    SESSION).
12. **BE Sym-union sym_scope vs enum_val**: `sym_link` skips the
    sym_scope write for IS_ENUM_VAL syms; the canonical scope
    lives on the enum tag (`sym_scope_ex` already read from
    there) (NEW THIS SESSION).

## Hosts (unchanged)

* **ibookg37** — primary build / test host (iBook G3, 900 MHz
  PowerPC 750, Tiger 10.4.11). `git` not in default `$PATH`;
  rsync from uranium.
* **imacg3** — secondary; less-used. Same configuration.
* **uranium** — main Mac (this one). Edit + commit + cut releases
  here.

## Closing notes

This session shipped two releases (v0.2.12, v0.2.13), one real-
world program (lua), introduced the per-release benchmark
discipline (scripts/bench.sh), and fixed three real backend bugs
(struct-deref-by-value, cross-TU PIC reloc translation, BE
sym_scope/enum_val clobber). tests2 climbed from 109/111 (98.2%)
to 110/111 (99.1%).

The two next-best targets are:
1. The sqlite3_open crash — high impact, real-world, likely a
   single codegen bug somewhere (the diagnosis scaffolding from
   the new ppc-macho diagnostics may help triangulate).
2. The 96 bitfield bug — only test failure left, narrow real-
   world impact, but completing 111/111 would be nice.

The PPC32 shift-count-≥32 hint from the parallel golang/PPC
session (uint64(1)<<64 returning 1) is worth keeping in mind for
both — if it applies to our backend, it'd be both a tests2 fix
and potentially explain weird sqlite behavior.

Good luck.
