# Findings — session 041

## PPC32 shift-≥32 hypothesis: ruled out

The handoff from session 040 flagged a possible PPC32 shift bug
(parallel golang/PPC backend hit `uint64(1) << 64` returning 1
instead of 0). Tested on `tcc -run`-built binaries on ibookg37
(`/tmp/shift_test.c`):

* `1ULL << 31..63` correct.
* `1ULL << 64` (UB) returns 0 — better than gcc which can return 1
  at -O0.
* Runtime-shift-count variant: same.
* Signed shifts: `(-1LL) >> 33` correct (sign-extends).

PPC's `slw`/`srw` instructions zero the result when the shift
count is ≥ 32 (per the ISA). Our backend doesn't suffer from this
class of bug. Hypothesis closed.

## 96_nodata_wanted bitfield bug: root cause + fix

### Root cause

tcc numbers bitfield positions LSB-first. Two code paths write/
read bitfields:

1. **Static initializer** (`tccgen.c::init_putv`, line 7945+):
   writes bitfields byte-by-byte starting at
   `ptr + (bit_pos >> 3)`. LSB-first byte interpretation.
2. **Runtime load/store** (when `auxtype != VT_STRUCT`, set by the
   FIX pass at line 4476+): loads the field's container (2/4/8
   bytes) as one int and extracts via `SHL ((bits - bit_pos -
   bit_size); SAR ((bits - bit_size))`. LSB-first bit numbering
   within the loaded int.

On **little-endian**, both interpretations agree because the byte
order matches the bit numbering. On **big-endian**, they disagree:

* Static init for `unsigned x : 12` initialized to `0x333` writes
  `byte 0 = 0x33; byte 1 low nibble = 0x3`.
* Runtime load reads `byte 0 << 24 | byte 1 << 16 | byte 2 << 8 |
  byte 3` = some `0x33XX_XXXX`. `bit_pos=0, bit_size=12` extracts
  the LOW 12 bits of that integer — bytes 2 and 3 — which is
  unrelated memory.

### Concrete example (struct A: `unsigned x : 12; unsigned y : 20`)

Initialized `{ 0x333, 0x55555 }`:

| Path | Bytes | Reads back x | Reads back y |
|---|---|---|---|
| gcc-4.0 | `33 35 55 55` | 0x333 ✓ | 0x55555 ✓ |
| tcc (broken) | `33 53 55 55` | 0x555 ✗ | 0x33535 ✗ |

The byte difference (`0x53` vs `0x35` at byte 1) shows tcc and
gcc choose different memory layouts on BE — tcc is LSB-first, gcc
is the standard BE bitfield convention (first declared field at
the MSB end of the container). But that's a layout convention
issue; the read-back inconsistency is the bug.

### Fix

For the failing test 96, what matters is that read-back is
consistent with what was written. The runtime store path also
goes through `store_packed_bf` (byte-wise, LSB-first) when
`auxtype == VT_STRUCT`, which is consistent with the static
initializer. So the fix is: on 32-bit BE PowerPC, force every
bitfield to use the byte-wise load/store paths.

Patch: at the top of the per-field loop in `struct_layout`'s FIX
pass (`tccgen.c` line 4476), set `f->auxtype = VT_STRUCT` and
`continue`. This is gated on `defined(TCC_TARGET_PPC) &&
!defined(TCC_TARGET_PPC64)`.

Cost: ~2-4 byte loads + shifts per access vs a single int load + 2
shifts. Acceptable for a tiny-codegen project with no inliner.

Memory layout still differs from gcc on BE — that's a deeper
project (struct_layout would need MSB-first bit numbering on BE)
and isn't required by any test we ship.

### Result

* Test 96 passes.
* tests2: **111 / 111 (100.0%)** — first time at 100%.
* Bootstrap fixpoint holds.

## Next: sqlite3_open crash

Carrying forward from 040. Hypothesis (PPC32 shift bug) is now
ruled out as the cause — it's not a tcc backend bug. So the crash
is something else: likely a different codegen edge case the
`sqlite3_open` path exercises.
