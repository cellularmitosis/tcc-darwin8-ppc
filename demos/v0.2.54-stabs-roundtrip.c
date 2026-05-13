/* v0.2.54-g3 — stabs round-trip through `tcc -c` -> link
 * (roadmap #7.5 Phase 2, item 1b prerequisite).
 *
 * Pre-v0.2.54, `tcc -gstabs -c foo.c -o foo.o` silently dropped the
 * `.stab` / `.stabstr` ELF sections when writing the Mach-O `.o` —
 * `classify_section` had no mapping for them, so they fell out of
 * the section table entirely. The two-step gdb-on-Tiger flow that
 * v0.2.51 unlocked only worked in single-step mode: `tcc -gstabs
 * foo.c -o foo` was debuggable, `tcc -gstabs -c foo.c -o foo.o
 * && tcc -gstabs foo.o -o foo` was not.
 *
 * v0.2.54 maps `.stab` -> `__DWARF,__stab` and `.stabstr` ->
 * `__DWARF,__stabstr` (mirroring the existing `.debug_*` ->
 * `__DWARF,__debug_*` convention), teaches `macho_load_object_file`
 * to read them back into the merged `stab_section`, and threads the
 * STT_SECTION-targeted relocs through `emit_section_relocs`. The
 * resulting linked exe's `LC_SYMTAB` is byte-equivalent to the
 * single-step build.
 *
 * The companion .sh exercises the full two-step flow with two
 * source files compiled separately, then linked together:
 *   1. Compile each .c -> .o with `-gstabs -c`.
 *   2. Confirm each .o contains `__DWARF,__stab` + `__DWARF,__stabstr`.
 *   3. Confirm each .o's __stab has non-zero relocs (against the
 *      .o's __text section_sym anchor).
 *   4. Link the .o's together with `-gstabs`.
 *   5. Confirm the linked exe has the full stab chain in
 *      `LC_SYMTAB` (FUN/SLINE/BNSYM/ENSYM/PSYM/LSYM all present).
 *   6. Script `gdb` to break by file:line in both compilation
 *      units, print params/locals/globals, walk the backtrace
 *      across .o boundaries.
 *
 * The point of having two source files is to verify the stab chain
 * round-trips correctly when MULTIPLE .o's contribute stab data —
 * the loader has to fix up each .o's n_strx offsets so they index
 * into the merged .stabstr, not the .o's own .stabstr.
 */

#include <stdio.h>

extern int helper_add(int x, int y);
extern int helper_mul(int x, int y);
extern int g_seed;

int main(int argc, char **argv)
{
    int a = helper_add(g_seed, 10);
    int b = helper_mul(a, 2);
    printf("a=%d b=%d\n", a, b);
    (void)argv; (void)argc;
    return 0;
}
