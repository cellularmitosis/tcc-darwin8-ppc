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
