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

    /* Reserve another 32 bytes below for any future callees'
       linkage + min param-save area. */
    addi    r3, r3, 32

    /* Snapshot back chain (caller's saved-sp link). */
    lwz     r0, 0(r1)

    /* Drop sp by r3 bytes, write back chain at new sp. */
    subf    r1, r3, r1
    stw     r0, 0(r1)

    /* Return pointer = new sp + 32 (above the reserved linkage). */
    addi    r3, r1, 32

    blr
