/* v0.2.61-g3 milestone demo source.
 *
 * Exercises the reader-side `-vv` diagnostic added to ppc-macho.c
 * (the symmetric counterpart to the writer-side v0.2.50/v0.2.59/v0.2.60
 * pre-write + post-write checks).
 *
 * The program itself is uninteresting — it just calls a helper from
 * a separate .o so the link exercises multi-input loading. The
 * driver shell script verifies that:
 *
 *   - default verbosity produces zero `tcc: reader: ...` lines
 *   - `-v` (level 1) produces the existing writer summary but still
 *     zero reader lines
 *   - `-vv` (level 2) produces > 0 reader lines across `section-skip`
 *     and `reloc-drop` categories
 *
 * The reader lines surface decisions the loader makes silently in
 * older releases (Apple-internal stub sections in crt1.o, debug
 * sections, etc.). They were what we'd have needed to triage the
 * v0.2.40 sed bug and the v0.2.44 SECTDIFF shadow-section bug. */

#include <stdio.h>

int helper(int x);

int main(void)
{
    int v = helper(7);
    printf("v=%d\n", v);
    return v == 14 ? 0 : 1;
}
