# Session 036 — post-travel resumption, 2026-05-02

Picking up from session 035's [HANDOFF-2.md](../035-unsupervised-2026-05-02/HANDOFF-2.md).
The previous session got partway through landing constructor /
destructor support, committed `d82b12a` ("ppc-macho: emit
__mod_init_func / __mod_term_func in EXE output"), then unplugged
the laptop for travel before re-running the full test suite. The
handoff said:

> Bootstrap fixpoint last verified at HEAD `11c9cfc` (v0.2.5
> release) — should still hold at d82b12a but **NOT YET RE-RUN**.
> 108_constructor verified locally as passing (saw
> "constructor\nmain\ndestructor\n") but **full tests2 NOT YET
> RE-RUN** at HEAD d82b12a.

## Headline result

| | start | end |
|---|---|---|
| HEAD | `d82b12a` (post-travel arrival) | `5fcec8c` (v0.2.6-g3 release) |
| Releases | 6 (v0.2.5-g3 latest) | **7** (v0.2.6-g3 cut this session) |
| tests2 | 101 / 122 (82.8%) failing 21 | **105 / 118 (89.0%)** failing 13 |
| Self-host fixpoint | holds | holds |

Three substantive fixes landed plus a release.

## Verification at HEAD `d82b12a`

First step from the handoff: run fixpoint + tests. Both expected to
pass, with `108_constructor` listed as "verified passing".

- `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` — holds. ✓
- `./scripts/run-tests2.sh` — 101 / 122 = 82.8%. **108_constructor
  failed.** Contradicts the handoff's "manually verified passing".

The integrated test compile of `108_constructor.c` hung tcc forever
(~12 minutes wall, growing to ~500 MB of RAM, no output). When run
as `tcc -c 108_constructor.c -o /tmp/c108.o`, it completed in
0.3s. So the bug was specifically in EXE output (the new code path
introduced by `d82b12a`).

## Bug 1: `sects[8]` overflow in macho_output_exe (`7151bf1`)

Bisected the hang to a stack-array overflow:

```c
struct exe_sect sects[8];   /* generous; we never use more than 4 */
```

That comment was correct when written — pre-`d82b12a` the maximum
was `text + rodata + stub + data + nlptr + dyld + bss = 7`. The
constructor commit added `__mod_init_func` and `__mod_term_func`
to the layout. With both halves present (as `108_constructor.c`
exercises), we overflow:

```
text(0) + rodata(1) + stub(2) + data(3) + init_array(4)
       + fini_array(5) + nlptr(6) + dyld(7) + bss(8) = 9 sections
```

`sects[8]` is OOB. The write clobbered the next-adjacent stack
locals (`sect_idx_init_array`, `sect_idx_nlptr`, `sect_idx_dyld`).
The corrupted indices then read garbage `file_off` values
(273492, 547853, ...) which the section write-out loop's
`while (out.len < target) put8(&out, 0)` "padded" to with hundreds
of MB of zero bytes — runaway memory + no progress.

Single-constructor and single-destructor cases didn't overflow (8
or fewer sections), so the bug only fired when both halves were
present — exactly `108_constructor.c`. The previous session's
"manually verified passing" appears to have been a measurement
error (different invocation that produced fewer than 8 sections).

**Fix**: bump `sects[]` from 8 to 16. Headroom for future additions.

**Result**: 102 / 122 = 83.6%, fixpoint holds, 108 passes.

## Bug 2: VLA × callee param-spill collision (`8d72774`)

The biggest lift this session. Apple PPC ABI requires every callee
to unconditionally spill its incoming r3..r10 to caller_SP+24..+52
— the parameter save area:

```
Prologue (every PPC function):
  mfspr r0, lr
  stw   r0, 8(r1)       ; save LR in caller's linkage area
  stw   r3, 24(r1)      ;\
  stw   r4, 28(r1)      ; \  spill r3..r10 to caller's
  ...                    ;  ) parameter save area, ALWAYS,
  stw  r10, 52(r1)      ; /  even for void/no-arg functions.
  stwu  r1, -frame_size(r1)
  ...
```

For a function with a live VLA at the bottom of the frame, the
VLA's first 28 bytes were in caller_SP+24..+52 from the callee's
view — and got clobbered on every function call.

`79_vla_continue`'s test2-test4 exercise this:

```c
void test2(void) {
    int count = 10;
    void *addr[count];   /* outer VLA */
    for (;count--;) {
        int a[f()];      /* inner VLA */
        addr[count] = a; /* called between two function calls */
        continue;
    }
    /* expect: every iteration's `a` at the same SP — addr[0..9]
       all equal */
}
```

Each call to `f()` clobbered `addr[8..9]` (and adjacent slots) via
its prolog spill, so by the time the comparison `addr[9] == addr[0]`
ran, both indices were corrupted by f()'s prolog from different
iterations.

**Fix**: reserve a 128-byte buffer (24 linkage + 96 param area + 8
align pad) BELOW the VLA's data. Layout becomes:

```
high addr
  +-------------+
  | (frame)     |
  +-------------+ pre-VLA SP
  | VLA data    | size_aligned bytes
  +-------------+ VLA pointer = post-VLA SP + PPC_VLA_BUFFER
  | param area  | <- callees write here on r3..r10 spill
  | linkage     |
  +-------------+ post-VLA SP (r1)
low addr
```

The user-visible VLA pointer is `SP + PPC_VLA_BUFFER`, NOT plain
`SP`. `gen_vla_alloc`, `gen_vla_sp_save`, and `gen_vla_sp_restore`
apply the offset symmetrically so the same saved slot serves both
as the user pointer and as the SP-restore target on scope exit /
continue.

A pitfall while implementing: `addi r1, r0, -128` doesn't subtract
from r0 — PPC treats `addi rD, r0, imm` as `rD = imm` (the rA=0
encoding special case). Switched the restore path to use r12 as
the scratch source register.

**Caveat (left for follow-up)**: PPC_VLA_BUFFER is fixed at compile
time at 96 bytes of param area (24 GPR slots). A function with both
VLAs and outgoing arg lists exceeding 96 bytes of params would
still corrupt the VLA. No test in tests2 hits this.

**Result**: tests2 102 → **105 / 118 = 89.0%**. 79_vla_continue
passes all 5 sub-tests. Bootstrap fixpoint still holds (the offset
is identical on every code path so fixpoint emission is
deterministic).

## Cleanup: BE-aware test skips (`9c20d7b`)

Four tests' `.expect` files bake little-endian byte order:

* `90_struct-init` — prints struct bytes via a loop; output reflects
  storage byte order.
* `91_ptr_longlong_arith32` — `*(unsigned*)(t+4) += a-b` mutates a
  string by overlaying an integer; resulting characters depend on
  which byte ends up at which offset.
* `95_bitfields`, `95_bitfields_ms` — bit-field layout within their
  storage unit follows the platform's bit-numbering convention. PPC
  packs from the MSB; x86 from the LSB.

None are tcc bugs. They're testing platform-conditional behavior
with x86-specific expected output. Skipped on `CONFIG_BIGENDIAN=yes`
targets via `tcc/tests/tests2/Makefile`.

Net effect on the report: 0 actual tests moved, but the
denominator went from 122 to 118 (4 reclassified from "fail" to
"skip"). 102 / 118 = 86.4%, then jumping to 105 / 118 = 89.0% with
the VLA fix.

## v0.2.6-g3 release (`5fcec8c`)

Cut [v0.2.6-g3](https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.6-g3)
bundling the three fixes plus housekeeping. Tarball is 153 KB.
Release notes call out:

* Constructor / destructor support (the originally-intended v0.2.6
  feature, now actually working end-to-end).
* VLA × function-call safety (the corollary fix that fell out of
  the verification work).
* BE-aware test classification.

## Trajectory updated

77 → 79 → 83 → 84 → 87 → 89 → 91 → 92 → 93 → 95 → 96 → 98 →
101 → **102** (sects fix) → **105** (VLA fix, then BE-skip
denominator change) at v0.2.6-g3.

## Commits this session

```
5fcec8c release: bump default to v0.2.6-g3 with new feature notes
595340a README: status to v0.2.6-g3 (105 / 118 = 89.0%)
9c20d7b tests2: skip 4 LE-specific tests on big-endian targets
8d72774 ppc-gen: VLA-vs-callee-spill safety buffer
7151bf1 ppc-macho: fix sects[8] overflow when init_array+fini_array present
```

Plus the v0.2.6-g3 tag and GitHub release.

## Files touched

- `tcc/ppc-macho.c` — `sects[]` 8 → 16
- `tcc/ppc-gen.c` — VLA buffer in gen_vla_alloc/sp_save/sp_restore
- `tcc/tests/tests2/Makefile` — BE-aware skips
- `scripts/build-release-tarball.sh` — version bump + release notes
- `README.md` — status table

## Exit state

HEAD `5fcec8c` ([v0.2.6-g3](https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.6-g3)).
Bootstrap fixpoint holds. 105 / 118 tests passing. 13 remaining
failures all in three buckets (`-run` / `-dt`, `-bcheck`, two
platform-mismatched outliers); see [session 037](../037-tcc-run-on-ppc-2026-05-02/README.md)
for the next push.
