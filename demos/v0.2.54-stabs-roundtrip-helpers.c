/* v0.2.54-g3 — second compilation unit for the stabs round-trip demo.
 *
 * The point of splitting this off is to exercise the multi-.o stab
 * chain merge: each `.o` contributes its own N_SO + per-function
 * entries; the linker's `macho_load_object_file` must adjust each
 * entry's `n_strx` so it indexes into the *merged* .stabstr, not the
 * .o's local .stabstr. If the fix-up is wrong, gdb's `list` /
 * `info function helper_*` produce garbage names or misalign source
 * lines across the second .o.
 */

int g_seed = 5;

int helper_add(int x, int y)
{
    int sum;
    sum = x + y;
    return sum;
}

int helper_mul(int x, int y)
{
    int product;
    product = x * y;
    return product;
}
