/*
 * Session 008 milestone — pointer dereference, store-via-pointer,
 * arrays-as-pointers, and the modulo operator.
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC):
 *
 *     ./tcc -B. -run demos/s008-array-sum.c
 *     echo $?      # → 55  (= 1+2+3+4+5+6+7+8+9+10)
 *
 * Why this matters: every prior demo dealt only with scalar locals.
 * This one passes a pointer between functions, indexes through an
 * array via pointer arithmetic, and uses modulo to keep the result
 * in the byte-sized exit-code range. It exercises the full
 * (reg | VT_LVAL) addressing convention — values whose effective
 * address is "the register r, with offset 0 from it" — which had a
 * subtle bug in the first cut (we were adding the residual sv->c.i
 * from the pre-gv local offset).
 *
 * Codegen surface added in 008:
 *   - load(): pointer-deref case for VT_LOCAL → register lvalue,
 *     with offset zero (matching i386/arm convention).
 *   - store(): same, store-via-register.
 *   - gen_opi(): real implementation of `%` and TOK_UMOD using
 *     `divw[u]` + `mullw` + `subf` over a fresh temp register.
 */

int sum_array(int *a, int n)
{
    int s = 0;
    int i = 0;
    while (i < n) {
        s = s + a[i];
        i = i + 1;
    }
    return s;
}

int main(void)
{
    int a[10];
    int i = 0;
    while (i < 10) { a[i] = i + 1; i = i + 1; }
    /* sum 1..10 = 55, fits in a byte exit code. */
    return sum_array(a, 10) % 256;
}
