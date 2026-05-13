/*
 * bswap_compat.c — shim providing builtins gcc-4.0 (and tcc) lack,
 * for use in csmith differential-testing campaigns on Tiger PPC.
 *
 * csmith --builtins emits calls to:
 *   __builtin_bswap32        (added in gcc 4.3)
 *   __builtin_bswap64        (added in gcc 4.3)
 *   __builtin_ia32_crc32qi   (x86-only SSE4.2 intrinsic)
 *
 * gcc-4.0 (and our tcc) treat these as ordinary extern function calls
 * — neither inlines them. Linking this .c file alongside the csmith
 * program resolves the symbols. Both compilers execute the same C
 * implementation, so any output divergence is a bug in the compiler's
 * handling of surrounding code, not the shim.
 */

unsigned int __builtin_bswap32(unsigned int x) {
    return ((x & 0xFFu) << 24)
         | ((x & 0xFF00u) << 8)
         | ((x & 0xFF0000u) >> 8)
         | ((x & 0xFF000000u) >> 24);
}

unsigned long long __builtin_bswap64(unsigned long long x) {
    return ((x & 0xFFULL) << 56)
         | ((x & 0xFF00ULL) << 40)
         | ((x & 0xFF0000ULL) << 24)
         | ((x & 0xFF000000ULL) << 8)
         | ((x & 0xFF00000000ULL) >> 8)
         | ((x & 0xFF0000000000ULL) >> 24)
         | ((x & 0xFF000000000000ULL) >> 40)
         | ((x & 0xFF00000000000000ULL) >> 56);
}

/*
 * Reflected Castagnoli CRC32 (poly 0x82F63B78 = bit_reverse(0x1EDC6F41)),
 * one byte at a time. Matches the x86 SSE4.2 CRC32 instruction's value
 * for single-byte inputs, but for our purposes any deterministic
 * function of (crc, data) works — what matters is that gcc and tcc
 * both call into this same implementation.
 */
unsigned int __builtin_ia32_crc32qi(unsigned int crc, unsigned char data) {
    int i;
    crc ^= data;
    for (i = 0; i < 8; i++) {
        unsigned int mask = -(crc & 1u);
        crc = (crc >> 1) ^ (0x82F63B78u & mask);
    }
    return crc;
}
