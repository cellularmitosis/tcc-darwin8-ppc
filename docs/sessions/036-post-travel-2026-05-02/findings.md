# Session 036 findings — durable lessons

Two PPC-ABI gotchas worth remembering.

## VLAs collide with callee param-spill on Apple PPC

**Rule**: every PPC callee unconditionally spills its incoming
r3..r10 to caller_SP+24..+52. So caller_SP+0..52 (linkage area +
parameter save area) is owned by the callee, not the caller.

**Implication**: if your VLA lives at caller_SP+0..size, every
function call will silently clobber the VLA's first 28 bytes (r3
saved at +24, r10 saved at +52). The bug is invisible until you
read the VLA after a call.

**Pattern** (in our backend, see `ppc-gen.c::gen_vla_alloc`):
reserve a fixed buffer below the VLA's data so callees spill into
the buffer instead of the VLA. We use `PPC_VLA_BUFFER = 128` (24
linkage + 96 param area + 8 alignment pad).

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
`SP`. `gen_vla_sp_save` and `_restore` apply the offset
symmetrically so a single saved slot works for both
"user-pointer-to-VLA" (load + use) and "SP-restore-target"
(used on continue / scope exit).

**Caveat**: PPC_VLA_BUFFER is a compile-time constant. A function
with both VLAs and outgoing arg lists exceeding 96 bytes of params
(24 GPR slots' worth) would still corrupt the VLA. No test in
tests2 hits this currently. If future tests do, the right fix is
probably to bake the per-function `ppc_param_area` value into a
dedicated frame-local slot at function entry and read it from
there instead of using a constant.

## Stack arrays in macho_output_exe size with section-list growth

`ppc-macho.c::macho_output_exe` lays out Mach-O sections into a
fixed-size `struct exe_sect sects[N]` array, indexed by section
order. We hit a `sects[8]` overflow in this session when adding
`__mod_init_func` and `__mod_term_func` brought the maximum to 9.

**Rule when adding a new Mach-O section type**: count the worst
case (text + rodata + stub + data + init_array + fini_array +
nlptr + dyld + bss + ... whatever you're adding) and bump the
array if needed. The overflow corrupts adjacent stack locals
(`sect_idx_*` integers that share a stack frame with `sects[]`),
so the symptom is wildly wrong file_off values, NOT a clean
abort. Don't trust silent success without re-running the suite.

Currently `sects[16]` — should be enough headroom for several
more section additions before another bump is needed.
