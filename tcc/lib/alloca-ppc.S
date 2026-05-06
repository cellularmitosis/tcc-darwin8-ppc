/*
 *  alloca for 32-bit PowerPC (Apple Mach-O).
 *
 *  Standard "growing the caller's frame" trick: alloca runs as a
 *  leaf function (no prolog/epilog of its own), drops the stack
 *  pointer, replicates the back chain, and returns a pointer just
 *  above the new linkage area so future callees can write their
 *  own param-save area below it without clobbering the alloca'd
 *  region.
 *
 *  Apple PPC ABI minimum frame: 24-byte linkage area + 8-byte
 *  parameter save area for the first two GPR params = 32 bytes
 *  reserved at the bottom of every frame for callees. We bump
 *  the return pointer past 32 bytes so calls made *after* alloca
 *  still have room to spill r3/r4 etc. without trampling the
 *  alloca'd memory.
 *
 *  Quad-aligned (16-byte) result, matching Apple's malloc.
 *
 *  This file is offered as-is, without any warranty.
 */

    .text
    .align 2
    .globl _alloca

_alloca:
    /* r3 = requested size. Round up to 16 bytes. */
    addi    r3, r3, 15
    rlwinm  r3, r3, 0, 0, 27        /* clear low 4 bits */

    /* Reserve a generous safety zone below the user's alloca'd region.
       The caller (the function that invoked alloca) keeps issuing
       calls AFTER alloca returns. Every such call has tcc's gfunc_call
       spilling outgoing GPR args to caller_r1 + 24 + slot*4 in caller's
       OUTGOING PARAM AREA. Since alloca dropped caller's r1, the
       outgoing param area is now at NEW_SP + 24..NEW_SP + 24 + 8*4 = 56
       bytes. If the user's alloca region overlaps with that range, the
       next outgoing call will clobber it.

       Reserve 256 bytes -- comfortably more than any plausible outgoing
       param area for tcc-generated code (24-byte linkage + up to 8 GPR
       arg slots = 56 bytes; even doubled for multi-word args, well
       under 256). The user's region sits ABOVE this safety zone, so
       subsequent spills land in the zone, not in the alloca'd memory.

       Cost: 256 bytes wasted per alloca. Acceptable until we expand
       alloca inline (gen_vla_alloc-style) and can size the safety
       zone to the actual ppc_param_area. */
    addi    r3, r3, 256

    /* Snapshot the EXISTING back chain (pointer to alloca's caller's
       caller's frame) and copy it through to the new frame. Standard
       PPC alloca pattern: alloca's allocation grows alloca_func's
       frame "in place" rather than introducing an intermediate frame.
       So the back chain at new_sp must point to wherever the existing
       back chain at old_sp pointed -- typically alloca_func's caller.
       Caller's epilog will then do a single `lwz r1, 0(r1)` to skip
       past both the alloca'd region AND alloca_func's frame in one
       hop, landing at the right caller_sp. */
    lwz     r0, 0(r1)

    /* Drop sp by r3 bytes, write back chain at new sp. */
    subf    r1, r3, r1
    stw     r0, 0(r1)

    /* Return pointer = new sp + 256 (above the reserved safety zone). */
    addi    r3, r1, 256

    blr
