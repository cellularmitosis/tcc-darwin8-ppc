# Session 041 — pickup 2026-05-03 (unsupervised)

## Arrival state

Picked up from session 040 (HANDOFF.md). HEAD `7f4ab95`, latest
release v0.2.13-g3, tests2 110/111. Two main targets:
1. 96_nodata_wanted BE bitfield bug (only failing test).
2. sqlite3_open crash.

## What landed

### 1. PPC32 shift-≥32 hypothesis ruled out

Quick experiment (`/tmp/shift_test.c`): `1ULL << 31..63` correct,
shifts ≥ 64 return 0 (PPC's slw/srw zero the result; better than
gcc which can return 1 at -O0). Closed.

### 2. Bitfield read-back fix → tests2 100% [v0.2.14-g3]

tcc's static initializer writes bitfields byte-by-byte (LSB-first)
but the runtime load goes through the wide-container path (LSB-
first within a BE-loaded int). Same bit numbering, different
memory regions on BE. Fix forces every bitfield to use the byte-
wise load_packed_bf / store_packed_bf paths on PPC32. Tests2
climbed to 111 / 111. See `findings.md` for the full trace.

Commits: `e929b86`, `66874ff` (release).

### 3. Apple PPC ABI long-long alignment + LL field-load clobber [v0.2.15-g3]

Two distinct bugs surfaced together inside `sqlite3PcacheRefCount`,
the first NON-trivial code path tcc reaches inside `sqlite3_open`.

**Bug A**: tcc was using 8-byte alignment for `double` / `long
long` inside structs; Apple PPC32 ABI uses 4-byte (matches m68k
Mac, gcc-4.0 on Tiger, etc.). Fix in `tccgen.c::type_size`.

**Bug B**: PPC32 BE LL split-load `incr_offset(+4); load LOW;
incr_offset(-4); load HIGH` is broken for VT_LLOCAL lvalues —
the LOW load() clobbers the address register, so the -4 step
operates on the loaded value rather than the address. Fix:
snapshot vtop's r/c.i/r2 before the LOW load and re-anchor to the
original VT_LLOCAL state for the HIGH load.

After both fixes, `sqlite3_open(":memory:", ...)` completes (was
crashing inside open).

Commits: `bc1ff4d`, `560ae06` (release).

### 4. sqlite3_prepare_v2 — crash, partial diagnosis

After v0.2.15, `select 1+1` still crashes — but now inside
`sqlite3RunParser` (after parsing 4 tokens) rather than during
open. Crash assembly:
```
li r7, 0
mr r5, r7        ; r5 = 0
lwz r4, 0(r5)    ; load from absolute address 0 — SEGV
```

This is a deliberate `*(NULL)` deref by tcc-emitted code. Could be
either:
- A struct field at the wrong offset (post-fix layout mismatch).
- A codegen bug emitting `*0` instead of `&something`.

**Heisenbug**: adding `write(2, marker, n)` calls inside
sqlite3RunParser to triangulate makes the crash disappear. Adding
markers anywhere in the parse path perturbs codegen enough to dodge
the issue. Suggests register pressure / allocation sensitivity.

Triangulation chain (tcc binary, fallback symbol attribution):
```
crash function <- inner static helper
  <- sqlite3_vtab_distinct+offset (probably sqlite3Parser)
  <- sqlite3_prepare16_v3+offset
  <- sqlite3_reset_auto_extension+offset
  <- sqlite3_prepare_v2 (real)
  <- main
```

Deferred for next session — see `sqlite-debug.md`.

## Performance benchmark (unchanged from v0.2.13)

`./scripts/bench.sh` on iBook G3 / 900 MHz:
- tcc:         **2 s** (33 lua 5.4.7 .c files)
- gcc-4.0 -O0: 17 s
- gcc-4.0 -Os: 40 s

## Verified at HEAD

- Bootstrap fixpoint holds.
- tests2: 111 / 111 (100.0%).
- lua 5.4.7 demo (`demos/v0.2.12-lua.sh`): works.
- sqlite3 amalgamation builds; `./sqlite3 -version` works;
  `./sqlite3 :memory: "select 1+1"` crashes inside RunParser.

## Releases shipped

- [v0.2.14-g3](https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.14-g3) — tests2 100% (BE bitfield fix).
- [v0.2.15-g3](https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.15-g3) — sqlite3_open works (Apple ABI LL alignment + LL field-load clobber fixes).

## Open work for next session

1. **sqlite3_prepare_v2 crash** — primary target. The Heisenbug
   nature suggests trying `volatile` annotations or other source-
   level perturbations to localize the buggy expression.
2. **Mach-O `tcc -ar` driver** (roadmap #3).
3. **Mach-O archive alacarte loader** (roadmap #4).
4. **bcheck.c port** — would un-skip 6 tests, multi-session lift.
