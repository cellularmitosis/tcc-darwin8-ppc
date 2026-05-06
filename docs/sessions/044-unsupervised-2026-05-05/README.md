# Session 044 — unsupervised stretch (2026-05-05)

User asked for a long autonomous run: "make tcc on darwin/tiger
100% bug free… don't worry about what will fit in one session."

Picking up from session 043's HANDOFF: HEAD `05dd9a3`, latest
release v0.2.21-g3, tests2 111/111, tcctest diff 44 lines, 4
real-world programs working (lua, zlib, bzip2, cJSON), open
work item #1 = `relocation_test` static-init `&arr[N]` addend
bug (4 prior attempts in session 043 broke bootstrap).

## Arrival state

* `git log -1`: `05dd9a3 docs/sessions/043: HANDOFF for next session`.
* Bootstrap fixpoint holds; tests2 111/111.
* Open work item #1: relocation_test `*rel1=1 *rel2=1` divergence
  (deferred since session 041; 4 attempts in 043).

## Bug 1: `&arr[N]` addend dropped on PPC ADDR32 relocations

### Reproduction (in-memory single-TU)

```c
int reltab[3] = { 100, 200, 300 };
int *rel1 = &reltab[1];
int *rel2 = &reltab[2];
int main(void) {
    printf("*rel1=%d (want 200)\n", *rel1);
    printf("*rel2=%d (want 300)\n", *rel2);
}
```

Pre-fix output: `*rel1=100`, `*rel2=100`. Both pointers resolve
to `&reltab` (offset 0) — addends 4 and 8 were lost.

Same pattern is also broken via `.o` roundtrip and across TUs
with extern (`int *rel1 = &extern_arr[1];`).

### Root cause

PPC's `relocate()` for `R_PPC_ADDR32` (in `tcc/ppc-link.c:229`)
overwrote the in-place data with the symbol's value:

```c
ppc_link_write32be(ptr, (uint32_t)val);
```

But TCC uses ELF Rel-format (no `r_addend` field) — the addend
is implicit in the in-place value (set by `init_putv` in
tccgen.c when the user writes `int *p = &arr[N]`). The correct
operation is **add**, not write. i386's `R_386_32` does this
correctly (`add32le(ptr, val)`).

The Mach-O EXE writer's `exe_resolve_section_relocs()` had the
same overwrite bug (`inst = target_addr`).

The .o → exe roundtrip path had a third twist: the .o loader
(`macho_translate_relocs`) extracts the addend from the in-place
value into a synthetic "anchor" symbol with
`st_value = sec_to_off + addend`. After this absorption the
in-place value is no longer the addend — it's `section_base +
addend`. If we naively switched to ADD semantics in the
relocator, we'd double-count the section base. So the fix also
zeros the in-place value at load time after the anchor absorbs
the addend.

### The fix (3 small changes)

1. `tcc/ppc-link.c::relocate()` for `R_PPC_ADDR32`: ADD, not
   write. (Single-TU exe and JIT paths.)
2. `tcc/ppc-macho.c::exe_resolve_section_relocs()` for
   `R_PPC_ADDR32`: `inst += target_addr` (was `inst =
   target_addr`).
3. `tcc/ppc-macho.c::macho_translate_relocs()` after creating
   the local-section anchor for VANILLA ADDR32: zero out the
   4 bytes of in-place value, so ADD semantics doesn't
   double-count `section_base + addend`.

The .o emit's pre-resolution path now writes Mach-O-conformant
VANILLA values (`section_base + addend` instead of just
`section_base`) as a side effect — which fixes the
.o-roundtrip path even before the loader adjustment lands,
because the loader's existing anchor logic starts producing
correct merged offsets again.

### Verification

```sh
# All three paths (in-memory, .o-roundtrip, cross-TU extern)
==single-TU==
*rel1=200 (want 200)
*rel2=300 (want 300)
==.o roundtrip==
*rel1=200 (want 200)
*rel2=300 (want 300)
==cross-TU extern==
*rel1=200 (want 200)
*rel2=300 (want 300)
```

* tests2: 111/111 (unchanged)
* Bootstrap fixpoint: still holds.
* tcctest.c diff vs gcc reference: **44 → 33 lines**
  (relocation_test divergence gone). Remaining 33 lines are all
  in the previously-classified "won't fix" buckets (Apple ABI
  `aligntest3/4`, gcc-4.0 `_Bool` size, undefined-behavior
  `promote char/short funcret`).

* abitest-tcc best-of-15: **19 / 30 passing** (up from
  HANDOFF's 14). Worst case is still 5 — the JIT-context
  Bus-error heisenbug is unchanged but the new ceiling is
  higher.

## Demo

[`demos/v0.2.22-relocation-addends.c`](../../../demos/v0.2.22-relocation-addends.c) — three
flavors of `&arr[N]` static init (single-TU, .o roundtrip,
cross-TU extern), all printing the expected values.

## Bug 2: extern function pointers in static init data → wild jump

After v0.2.22 shipped I retested the deferred sqlite issues from
session 041. Surprise: the CREATE TABLE → `SQLITE_CORRUPT` bug
fixed itself (the addend work covered it). But file-based open
still SEGV'd.

### Reproduction

```c
sqlite3 *db;
int rc = sqlite3_open("/tmp/sqfile.db", &db);  // SEGV
```

`:memory:`, NULL, and "" all worked. Only real filenames crashed.

### Triangulation

Auto-injected `write(2, "...", N)` markers throughout
`openDatabase` → `sqlite3BtreeOpen` → `sqlite3PagerOpen` (Python
script that walks each function and emits markers before each
`if` / `rc = ` / `sqlite3*` line). Drilled three levels deep until
the crash localized to `unixOpen → robust_open → osFcntl(...)`:

```c
osFcntl(fd, F_SETFD, osFcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
```

Where `osFcntl` is a macro:

```c
#define osFcntl ((int(*)(int,int,...))aSyscall[7].pCurrent)
```

`aSyscall[7].pCurrent` is initialized to `(sqlite3_syscall_ptr)fcntl`.
At runtime it should be `&fcntl` (the libSystem function).

### Root cause

Dumped `aSyscall[]` via the VFS's `xGetSystemCall`. Local funcs
(posixOpen, openDirectory, etc.) had real text addresses. But
extern libSystem funcs (close, fcntl, read, write, ...) all had
clustered values like `0x1d1014`, `0x1d1018`, `0x1d101c` — the
addresses of `__nl_symbol_ptr` SLOTS, not the dyld-resolved
function addresses.

Tracing through the EXE writer:

1. tcc emits `R_PPC_ADDR32` against `_fcntl` for the static init.
2. Resolver calls `exe_sym_addr(symidx)`. fcntl is `SHN_UNDEF`.
3. `stub_for_elfsym[fcntl_idx] == -1` (no stub allocated; the only
   trigger was `R_PPC_REL24` calls, and fcntl is only data-
   referenced).
4. Caller falls through to the `__nl_symbol_ptr` fallback:
   `target_addr = nlptrs[nl_for_elfsym[symidx]].slot_addr`.
5. The slot's *address* (not its value) gets stored in the data.

When sqlite calls `osFcntl(...)`, it loads the slot address as if
it were a function pointer and `bctrl`'s to it. That address is 4
bytes of data, not executable code → SEGV with PC = data address.

### The fix

In `tcc/ppc-macho.c::collect_extern_stubs()`, also trigger stub
allocation when an extern function (`STT_FUNC + SHN_UNDEF`) is
data-referenced via `R_PPC_ADDR32` — not just `R_PPC_REL24`. The
stub mechanism already exists; it's the standard 4-instruction
PPC PLT stub:

```
lis   r12, ha(slot)
lwz   r12, lo(slot)(r12)
mtctr r12
bctr
```

`exe_sym_addr` already prefers the stub address over the slot
fallback — so once the stub exists, the static init resolves
correctly. The data slot now holds a callable stub address;
calling through it jumps via `__nl_symbol_ptr` (filled by dyld
lazily on first call) to the real function.

### Verification

* `tests2`: still 111/111.
* Bootstrap fixpoint: still holds.
* All prior real-world demos (lua, bzip2, cJSON): still pass.
* `sqlite3_open("/tmp/some.db", ...)`: works.
* End-to-end: CREATE TABLE + INSERT + SELECT + ORDER BY against
  an on-disk sqlite database → correct rows.

### Caveats

Extern *data* references still have the same wrong-target problem
(`extern int errno; int *p = &errno;` would resolve `p` to the
slot address, not `&errno`). No real-world program in our test
matrix hits that, and the proper fix would need dyld bind-info
emission in our Mach-O exe writer — a significantly larger
change. Deferred.

## Demo (added in v0.2.23)

[`demos/v0.2.23-sqlite-file.sh`](../../../demos/v0.2.23-sqlite-file.sh) — full
sqlite3 round trip against `/tmp/v0223-sqlite-demo.db` with
DROP / CREATE / INSERT / SELECT / ORDER BY, prints rows ordered
by age, exits 0.

## Bug 3 / Improvement: tcc -ar Mach-O native (v0.2.24)

The build pipeline was using `XAR = $(AR)` (system `/usr/bin/ar`)
for PPC because the ELF-only `tcc_tool_ar()` couldn't read Mach-O
.o files. This was a structural item from session 028's roadmap
(#3 in the "structural items" table).

### Delegated to a Sonnet subagent

Spawned a `general-purpose` Sonnet subagent in the background
with a self-contained brief: scope (Mach-O code path inside
`tcc_tool_ar`), references (existing ELF code, the canonical
parser in `ppc-macho.c::macho_load_object_file`), and a TESTING
section template the agent should fill in (since it can't ssh to
the PPC machine).

The agent took ~10 minutes and returned with:

* `tcc/tcctools.c`: 120 lines added inside the per-object loop,
  gated on `defined(TCC_TARGET_MACHO)`. Handles fat (universal)
  binary slice selection, LC_SYMTAB walk, 12-byte nlist iteration.
  Selects defined-external symbols via the standard
  `(n_type & N_EXT) && (n_type & N_TYPE) == N_SECT` check.
  Symdef offsets written byte-by-byte (host-endian-independent
  big-endian).
* `tcc/lib/Makefile`: removed the `XAR = $(AR)` workaround.
* TESTING block listing exact rsync + build + smoke commands.

### Verification (mine)

* `build-tiger.sh`: builds successfully, log shows
  `../tcc -ar rcs ../libtcc1.a lib-ppc.o ...` (no `/usr/bin/ar`).
* `file libtcc1.a`: "current ar archive".
* `/usr/bin/ar t libtcc1.a`: lists all members correctly.
* `nm libtcc1.a`: dumps symbols cleanly.
* Bootstrap fixpoint: holds.
* tests2: still 111/111.
* All real-world demos (lua, bzip2, cJSON, sqlite): still pass.

### Subagent verdict

Worked on the first try with no iteration needed. The brief was
detailed (~80 lines) but that paid off: the agent didn't have to
re-research the existing code paths, found the right file
references, and produced a coherent diff. Total token cost was
minimal compared to me doing it.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|
| Bug 3 | Port `tcc -ar` symbol extraction from ELF to Mach-O | Sonnet (general-purpose) | First-try success. ~140 lines of C, well-scoped. Verified locally — bootstrap, tests2, all demos pass. |

## tcc -ar Mach-O port (Tier B #7)

### Problem

`tcc/tcctools.c::tcc_tool_ar()` parsed ELF `.o` headers to build
the `__.SYMDEF` symbol index. On Mach-O `.o` files it would
reject the input with "Unsupported Elf Class" because `0xfeedface`
is not a valid ELF class byte.

Work-around in `tcc/lib/Makefile`: `XAR = $(AR)` for ppc targets.

### Fix

Added a `#if defined(TCC_TARGET_MACHO)` code path in
`tcc_tool_ar()` that:

1. Handles fat (0xcafebabe) binaries: scans `fat_arch` entries
   for `CPU_TYPE_POWERPC` (18), adjusts `mb_off` to the PPC slice.
2. Checks for `0xfeedface` (PPC32 BE Mach-O magic). Hard-errors
   with a clear message on anything else.
3. Walks load commands looking for `LC_SYMTAB` (cmd == 0x2) to
   find `symoff / nsyms / stroff / strsize`.
4. Iterates nlist entries (12 bytes each). Selects entries where
   `(n_type & N_EXT) && (n_type & N_TYPE) == N_SECT` — i.e.
   defined, externally visible symbols. Skips stabs, undefined
   externs, and empty names.
5. Writes the `/.SYMDEF` index using byte-by-byte `ar_put_be32`
   so the big-endian integers are correct regardless of whether
   `tcc -ar` is running on a LE (cross-build on uranium) or BE
   (native Tiger PPC) host.

The ELF path is unchanged (`#else` branch). Local helper macros
`ar_get32be` and `ar_put_be32` are `#define`/`#undef`-ed within
their scope; they don't pollute the global namespace and don't
depend on the (static) `mach_get32` from ppc-macho.c.

Removed the `XAR = $(AR)` override from `tcc/lib/Makefile` so
`$(XTCC) -ar` is used for ppc-osx builds (native and cross).

### Files changed

- `tcc/tcctools.c` — Mach-O symbol extraction + BE symdef writing
- `tcc/lib/Makefile` — remove `XAR = $(AR)` workaround

### Judgment calls

- **Fat binary handling**: included defensively. `tcc -c` never
  emits fat binaries, but `crt1.o` from the SDK is fat. We select
  the PPC slice. If no PPC slice is found, `mb_off` stays 0 and
  the magic check fails with a clear error.
- **Cross-compilation endianness**: the symdef header integer writes
  are byte-by-byte (not `fwrite` of a host-endian int array) so
  `ppc-osx-tcc -ar` running on uranium (LE) also produces a
  correct BE symdef. The ELF path uses `le2belong` which is
  incorrect on a BE host, but the ELF path is only compiled when
  `TCC_TARGET_MACHO` is not defined.
- **GNU ar `/` format vs BSD `__.SYMDEF SORTED`**: the archive
  uses GNU ar format (first member named `/`), same as all other
  tcc -ar targets. Tiger PPC's `ld` accepts GNU ar format; this
  was not changed.
- **`n_strx` offset 0**: nlist strtab index 0 is valid (a real
  symbol could live at offset 0 in the strtab). We skip entries
  where the name string is empty (`!*sym_name`), not where
  `n_strx == 0`.

### Testing needed on ibookg37

See TESTING section below.
