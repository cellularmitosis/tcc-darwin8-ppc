/*
 * Session 006 milestone — locals, arithmetic, and control flow.
 *
 * Build & run on imacg3 (Tiger 10.4.11 PowerPC):
 *
 *     ./tcc -B. -run demos/s006-factorial.c
 *     echo $?      # → 120  (= 5!)
 *
 * Why this matters: this is the first program tcc-darwin8-ppc compiles
 * and runs that contains *real computation* — multiple local variables
 * stored in a stack frame, a multiplicative accumulator, a counted
 * loop with a comparison and conditional branch.
 *
 * Codegen surface exercised:
 *   - locals at negative offsets from r31 (FP register)
 *   - `lwz` / `stw` for int load/store
 *   - `mullw` for integer multiply
 *   - `subf` for integer subtract
 *   - `cmpw` + `bc` for the loop condition
 *   - `b` for the loop back-edge
 *   - the icache-flush sequence (dcbst/sync/icbi/sync/isync) so the
 *     freshly-emitted body actually executes the right bytes
 *
 * 5! = 120, which fits in a Unix exit code (< 256). For larger n the
 * exit code shows the low byte: 6! = 720 → exit 208; 10! = 3628800 →
 * exit 0.
 */

int main(void)
{
    int n = 5;
    int f = 1;
    while (n > 0) {
        f = f * n;
        n = n - 1;
    }
    return f;
}
