/* Does Tiger's dyld treat VANILLA external relocs as ADD or
 * OVERWRITE? Test with a nonzero in-place addend. */
#include <unistd.h>
#include <stdio.h>

extern int optind;
int *p_plus3 = &optind + 3;  /* should be &optind + 3*sizeof(int) = &optind + 12 */

int main(void) {
    unsigned long want = (unsigned long)&optind + 3 * sizeof(int);
    unsigned long got  = (unsigned long)p_plus3;
    printf("&optind     = 0x%lx\n", (unsigned long)&optind);
    printf("&optind + 3 = 0x%lx (expected)\n", want);
    printf("p_plus3     = 0x%lx (got)\n", got);
    if (got == want) { puts("PASS (dyld ADDs)"); return 0; }
    if (got == (unsigned long)&optind) { puts("FAIL: dyld OVERWROTE (lost addend)"); return 1; }
    puts("FAIL: completely off");
    return 2;
}
