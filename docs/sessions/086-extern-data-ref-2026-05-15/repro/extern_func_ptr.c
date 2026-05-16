/* Test: static-init function pointer to extern. This is the
 * v0.2.23 case (R_PPC_ADDR32 + STT_FUNC). My change should NOT
 * have altered it — it should still use the stub path, not extrel. */
#include <stdio.h>
#include <ctype.h>

/* gnulib idiom: function-pointer table to ctype functions. */
typedef int (*ctype_fn)(int);
static ctype_fn table[] = { isalpha, isupper, islower, isdigit };

int main(void) {
    int i;
    for (i = 0; i < 4; i++) {
        printf("table[%d](%c) -> %d\n", i, 'a' + i, table[i]('a' + i));
    }
    return 0;
}
