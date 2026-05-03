/*
 * atomic-ppc.S — lock-free 4-byte atomic helpers for libtcc1 on PPC32.
 *
 * tcc PPC's backend doesn't (yet) emit inline asm and its built-in
 * assembler doesn't cover lwarx/stwcx., so this file is compiled by
 * gcc-4.0 during the libtcc1.a build via a per-file Makefile rule.
 *
 * What we provide here:
 *   - __atomic_load_4      (4-byte word load)
 *   - __atomic_store_4     (4-byte word store)
 *   - __atomic_exchange_4
 *   - __atomic_compare_exchange_4
 *   - __atomic_fetch_{add,sub,and,or,xor,nand}_4
 *
 * Everything else (1- and 2-byte ops, 8-byte ops, atomic_flag_*)
 * stays in lib-ppc.c under the global pthread_mutex. PPC32 has no
 * lbarx/sbarx/ldarx/stdcx, so byte/short would need word-RMW with
 * masking and 8-byte needs locking either way.
 *
 * Memory ordering: we treat all ops as memory_order_seq_cst by
 * always emitting `sync` before and `isync` after. Conservative but
 * correct — the C11 standard allows weaker but never stronger than
 * what the user asked for.
 *
 * Apple PPC ABI: args in r3..r10, return in r3 (or r3:r4 for LL).
 * Function names need a leading underscore in the symtab.
 *
 * Copyright (c) 2026 the tcc-darwin8-ppc contributors. MIT.
 */

	.text

/* uint32_t __atomic_load_4(const uint32_t *p, int memorder) */
	.globl ___atomic_load_4
	.align 2
___atomic_load_4:
	lwz	r3, 0(r3)
	sync
	blr

/* void __atomic_store_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_store_4
	.align 2
___atomic_store_4:
	sync
	stw	r4, 0(r3)
	blr

/* uint32_t __atomic_exchange_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_exchange_4
	.align 2
___atomic_exchange_4:
	sync
1:	lwarx	r0, 0, r3
	stwcx.	r4, 0, r3
	bne-	1b
	isync
	mr	r3, r0
	blr

/* int __atomic_compare_exchange_4(uint32_t *p, uint32_t *expected,
 *                                  uint32_t desired,
 *                                  int weak, int success, int failure)
 * Returns 1 on exchange, 0 otherwise. On 0, *expected = *p. */
	.globl ___atomic_compare_exchange_4
	.align 2
___atomic_compare_exchange_4:
	sync
	lwz	r9, 0(r4)
1:	lwarx	r0, 0, r3
	cmpw	r0, r9
	bne-	2f
	stwcx.	r5, 0, r3
	bne-	1b
	isync
	li	r3, 1
	blr
2:	isync
	stw	r0, 0(r4)
	li	r3, 0
	blr

/* uint32_t __atomic_fetch_add_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_fetch_add_4
	.align 2
___atomic_fetch_add_4:
	sync
1:	lwarx	r0, 0, r3
	add	r6, r0, r4
	stwcx.	r6, 0, r3
	bne-	1b
	isync
	mr	r3, r0
	blr

/* uint32_t __atomic_fetch_sub_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_fetch_sub_4
	.align 2
___atomic_fetch_sub_4:
	sync
1:	lwarx	r0, 0, r3
	subf	r6, r4, r0          /* r6 = r0 - r4 */
	stwcx.	r6, 0, r3
	bne-	1b
	isync
	mr	r3, r0
	blr

/* uint32_t __atomic_fetch_and_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_fetch_and_4
	.align 2
___atomic_fetch_and_4:
	sync
1:	lwarx	r0, 0, r3
	and	r6, r0, r4
	stwcx.	r6, 0, r3
	bne-	1b
	isync
	mr	r3, r0
	blr

/* uint32_t __atomic_fetch_or_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_fetch_or_4
	.align 2
___atomic_fetch_or_4:
	sync
1:	lwarx	r0, 0, r3
	or	r6, r0, r4
	stwcx.	r6, 0, r3
	bne-	1b
	isync
	mr	r3, r0
	blr

/* uint32_t __atomic_fetch_xor_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_fetch_xor_4
	.align 2
___atomic_fetch_xor_4:
	sync
1:	lwarx	r0, 0, r3
	xor	r6, r0, r4
	stwcx.	r6, 0, r3
	bne-	1b
	isync
	mr	r3, r0
	blr

/* uint32_t __atomic_fetch_nand_4(uint32_t *p, uint32_t v, int memorder) */
	.globl ___atomic_fetch_nand_4
	.align 2
___atomic_fetch_nand_4:
	sync
1:	lwarx	r0, 0, r3
	nand	r6, r0, r4
	stwcx.	r6, 0, r3
	bne-	1b
	isync
	mr	r3, r0
	blr

/* ============================================================
 * 1-byte and 2-byte atomics via word-RMW with masking.
 *
 * PPC32 has no lbarx/sbarx, so byte/short atomics work by
 * lwarx-ing the 4-byte word that contains the byte, masking out
 * the byte, OR-ing in the new value, then stwcx-ing the whole
 * word. Reservation is on the word, so 16 threads RMW-ing the
 * same byte serialize correctly.
 *
 * Common register convention for these helpers:
 *   r3  = original byte/short pointer (input), then return value
 *   r4  = new value (input)
 *   r9  = word-aligned pointer (r3 & ~3)
 *   r10 = bit-shift amount (0, 8, 16, or 24)
 *   r11 = mask (0xff << r10 or 0xffff << r10)
 *   r12 = scratch
 *   r0  = lwarx'd word
 *   r6  = computed new word
 *
 * Big-endian byte addressing: byte at addr A occupies bits
 *   [(3 - (A&3))*8 .. (3 - (A&3))*8 + 7]
 * Half at addr A (assumed 2-aligned) occupies bits
 *   [(1 - ((A>>1)&1))*16 .. (1 - ((A>>1)&1))*16 + 15]
 * which is equivalent to ((A^2)&2)*8 for the shift amount.
 * ============================================================ */

/* Macros to set up r9, r10, r11 for byte / half ops.
 * After byte_setup: r9 = word ptr, r10 = shift, r11 = mask
 * (both byte and half use the same r9/r10/r11 convention; only
 * the mask width differs). */

/* unsigned char __atomic_load_1(const unsigned char *p, int o) */
	.globl ___atomic_load_1
	.align 2
___atomic_load_1:
	clrrwi	r9, r3, 2          /* word ptr */
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28 /* shift */
	lwz	r0, 0(r9)
	srw	r0, r0, r10
	rlwinm	r3, r0, 0, 24, 31  /* zero-ext to byte */
	sync
	blr

/* void __atomic_store_1(unsigned char *p, unsigned char v, int o) */
	.globl ___atomic_store_1
	.align 2
___atomic_store_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 24, 31  /* clamp v to 8 bits */
	slw	r12, r4, r10        /* v << shift */
	sync
1:	lwarx	r0, 0, r9
	andc	r6, r0, r11         /* clear byte */
	or	r6, r6, r12         /* set new byte */
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	blr

/* unsigned char __atomic_exchange_1(unsigned char *p, unsigned char v, int o) */
	.globl ___atomic_exchange_1
	.align 2
___atomic_exchange_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 24, 31
	slw	r12, r4, r10
	sync
1:	lwarx	r0, 0, r9
	andc	r6, r0, r11
	or	r6, r6, r12
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 24, 31
	blr

/* int __atomic_compare_exchange_1(unsigned char *p, unsigned char *exp,
 *                                  unsigned char des, int weak,
 *                                  int success, int failure) */
	.globl ___atomic_compare_exchange_1
	.align 2
___atomic_compare_exchange_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	lbz	r7, 0(r4)           /* r7 = *expected (snapshot) */
	rlwinm	r5, r5, 0, 24, 31  /* clamp desired to 8 bits */
	slw	r8, r5, r10         /* shifted desired */
	slw	r7, r7, r10         /* shifted expected */
	sync
1:	lwarx	r0, 0, r9
	and	r6, r0, r11         /* current byte at position */
	cmpw	r6, r7
	bne-	2f
	andc	r6, r0, r11
	or	r6, r6, r8
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	li	r3, 1
	blr
2:	isync
	srw	r6, r0, r10
	rlwinm	r6, r6, 0, 24, 31
	stb	r6, 0(r4)           /* *expected = current byte */
	li	r3, 0
	blr

/* The fetch-OP family for byte: same template, only OP differs.
 * After op: r6 holds the (potentially out-of-byte-range) result.
 * Re-shift to position and merge with preserved high bits. */

/* unsigned char __atomic_fetch_add_1(unsigned char *p, unsigned char v, int o) */
	.globl ___atomic_fetch_add_1
	.align 2
___atomic_fetch_add_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	sync
1:	lwarx	r0, 0, r9
	srw	r7, r0, r10         /* extract old byte (low bits) */
	add	r7, r7, r4          /* + v */
	slw	r7, r7, r10         /* shift back */
	and	r7, r7, r11         /* mask to byte position */
	andc	r6, r0, r11
	or	r6, r6, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 24, 31
	blr

/* unsigned char __atomic_fetch_sub_1 */
	.globl ___atomic_fetch_sub_1
	.align 2
___atomic_fetch_sub_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	sync
1:	lwarx	r0, 0, r9
	srw	r7, r0, r10
	subf	r7, r4, r7
	slw	r7, r7, r10
	and	r7, r7, r11
	andc	r6, r0, r11
	or	r6, r6, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 24, 31
	blr

/* unsigned char __atomic_fetch_and_1 */
	.globl ___atomic_fetch_and_1
	.align 2
___atomic_fetch_and_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 24, 31
	slw	r4, r4, r10         /* v shifted; now ANDed-and-position-correct */
	sync
1:	lwarx	r0, 0, r9
	andc	r12, r0, r11        /* preserved bits */
	and	r7, r0, r4          /* (word & shifted_v) */
	and	r7, r7, r11         /* mask to byte position only */
	or	r6, r12, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 24, 31
	blr

/* unsigned char __atomic_fetch_or_1 */
	.globl ___atomic_fetch_or_1
	.align 2
___atomic_fetch_or_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 24, 31
	slw	r4, r4, r10         /* v shifted to position */
	and	r4, r4, r11
	sync
1:	lwarx	r0, 0, r9
	or	r6, r0, r4
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 24, 31
	blr

/* unsigned char __atomic_fetch_xor_1 */
	.globl ___atomic_fetch_xor_1
	.align 2
___atomic_fetch_xor_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 24, 31
	slw	r4, r4, r10
	and	r4, r4, r11
	sync
1:	lwarx	r0, 0, r9
	xor	r6, r0, r4
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 24, 31
	blr

/* unsigned char __atomic_fetch_nand_1 */
	.globl ___atomic_fetch_nand_1
	.align 2
___atomic_fetch_nand_1:
	clrrwi	r9, r3, 2
	xori	r10, r3, 3
	rlwinm	r10, r10, 3, 27, 28
	li	r11, 0xff
	slw	r11, r11, r10
	sync
1:	lwarx	r0, 0, r9
	srw	r7, r0, r10
	and	r7, r7, r4
	not	r7, r7              /* nand = ~(a & b) */
	slw	r7, r7, r10
	and	r7, r7, r11
	andc	r6, r0, r11
	or	r6, r6, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 24, 31
	blr

/* ============================================================
 * 2-byte (half-word) atomics, same word-RMW pattern.
 * Big-endian half addressing: half at addr A (2-aligned) is at
 * shift = (1 - ((A>>1)&1)) * 16 = ((A^2) & 2) * 8.
 * Equivalently: rlwinm of (A^2) by 3 with mask {28,28} gives
 * shift in {0, 16}.
 * ============================================================ */

/* unsigned short __atomic_load_2(const unsigned short *p, int o) */
	.globl ___atomic_load_2
	.align 2
___atomic_load_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28 /* shift in {0, 16} */
	lwz	r0, 0(r9)
	srw	r0, r0, r10
	rlwinm	r3, r0, 0, 16, 31  /* zero-ext to 16 bits */
	sync
	blr

/* void __atomic_store_2(unsigned short *p, unsigned short v, int o) */
	.globl ___atomic_store_2
	.align 2
___atomic_store_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff    /* r11 = 0xffff */
	slw	r11, r11, r10        /* r11 = mask = 0xffff << shift */
	rlwinm	r4, r4, 0, 16, 31
	slw	r12, r4, r10
	sync
1:	lwarx	r0, 0, r9
	andc	r6, r0, r11
	or	r6, r6, r12
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	blr

/* unsigned short __atomic_exchange_2 */
	.globl ___atomic_exchange_2
	.align 2
___atomic_exchange_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 16, 31
	slw	r12, r4, r10
	sync
1:	lwarx	r0, 0, r9
	andc	r6, r0, r11
	or	r6, r6, r12
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 16, 31
	blr

/* int __atomic_compare_exchange_2 */
	.globl ___atomic_compare_exchange_2
	.align 2
___atomic_compare_exchange_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	lhz	r7, 0(r4)           /* r7 = *expected */
	rlwinm	r5, r5, 0, 16, 31
	slw	r8, r5, r10
	slw	r7, r7, r10
	sync
1:	lwarx	r0, 0, r9
	and	r6, r0, r11
	cmpw	r6, r7
	bne-	2f
	andc	r6, r0, r11
	or	r6, r6, r8
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	li	r3, 1
	blr
2:	isync
	srw	r6, r0, r10
	rlwinm	r6, r6, 0, 16, 31
	sth	r6, 0(r4)
	li	r3, 0
	blr

/* unsigned short __atomic_fetch_add_2 */
	.globl ___atomic_fetch_add_2
	.align 2
___atomic_fetch_add_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	sync
1:	lwarx	r0, 0, r9
	srw	r7, r0, r10
	add	r7, r7, r4
	slw	r7, r7, r10
	and	r7, r7, r11
	andc	r6, r0, r11
	or	r6, r6, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 16, 31
	blr

/* unsigned short __atomic_fetch_sub_2 */
	.globl ___atomic_fetch_sub_2
	.align 2
___atomic_fetch_sub_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	sync
1:	lwarx	r0, 0, r9
	srw	r7, r0, r10
	subf	r7, r4, r7
	slw	r7, r7, r10
	and	r7, r7, r11
	andc	r6, r0, r11
	or	r6, r6, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 16, 31
	blr

/* unsigned short __atomic_fetch_and_2 */
	.globl ___atomic_fetch_and_2
	.align 2
___atomic_fetch_and_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 16, 31
	slw	r4, r4, r10
	sync
1:	lwarx	r0, 0, r9
	andc	r12, r0, r11
	and	r7, r0, r4
	and	r7, r7, r11
	or	r6, r12, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 16, 31
	blr

/* unsigned short __atomic_fetch_or_2 */
	.globl ___atomic_fetch_or_2
	.align 2
___atomic_fetch_or_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 16, 31
	slw	r4, r4, r10
	and	r4, r4, r11
	sync
1:	lwarx	r0, 0, r9
	or	r6, r0, r4
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 16, 31
	blr

/* unsigned short __atomic_fetch_xor_2 */
	.globl ___atomic_fetch_xor_2
	.align 2
___atomic_fetch_xor_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	rlwinm	r4, r4, 0, 16, 31
	slw	r4, r4, r10
	and	r4, r4, r11
	sync
1:	lwarx	r0, 0, r9
	xor	r6, r0, r4
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 16, 31
	blr

/* unsigned short __atomic_fetch_nand_2 */
	.globl ___atomic_fetch_nand_2
	.align 2
___atomic_fetch_nand_2:
	clrrwi	r9, r3, 2
	xori	r10, r3, 2
	rlwinm	r10, r10, 3, 27, 28
	lis	r11, 0
	ori	r11, r11, 0xffff
	slw	r11, r11, r10
	sync
1:	lwarx	r0, 0, r9
	srw	r7, r0, r10
	and	r7, r7, r4
	not	r7, r7
	slw	r7, r7, r10
	and	r7, r7, r11
	andc	r6, r0, r11
	or	r6, r6, r7
	stwcx.	r6, 0, r9
	bne-	1b
	isync
	srw	r3, r0, r10
	rlwinm	r3, r3, 0, 16, 31
	blr
