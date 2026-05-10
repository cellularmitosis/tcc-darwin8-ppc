/* Minimal repro of csmith seed-2187 (campaign B-complex) divergence.
 *
 * Boils down to: gcc-4.0's constant folder for
 *
 *     (LHS_with_side_effects, unsigned_long_const) >= signed_char_var
 *
 * picks SIGNED comparison instead of the C-required UNSIGNED comparison
 * (after promoting the signed char to int and then to unsigned long).
 *
 * | Compiler                | Output            |
 * |-------------------------|-------------------|
 * | tcc (this tree)         | j=0 k=0 l=0 m=0   | (correct)
 * | Apple clang 17 (uranium)| j=0 k=0 l=0 m=0   | (correct)
 * | gcc-4.0 -O0..-O3 (Tiger)| j=1 k=1 l=1 m=1   | (WRONG)
 *
 * Run on imacg3:
 *   /usr/bin/gcc-4.0 -O0 -w cmp.c -o cmp.gcc && ./cmp.gcc
 *   $TCC -B$TCC_TREE/tcc -I$TCC_TREE/tcc/include -w cmp.c -o cmp.tcc && ./cmp.tcc
 */
#include <stdio.h>
int main(int argc, char **argv) {
    signed char p_11 = -5;
    int side = argc;
    int j = ((side++, 65528UL) >= p_11);
    int k = ((side, 65528UL) >= p_11);
    int l = ((side = 1, 65528UL) >= p_11);
    int m = ((argc, 65528UL) >= p_11);
    printf("j=%d k=%d l=%d m=%d side=%d\n", j, k, l, m, side);
    return 0;
}
