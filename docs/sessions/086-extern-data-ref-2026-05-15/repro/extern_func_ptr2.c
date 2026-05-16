/* What if the extern function has been declared *without* a prototype?
 * E.g. legacy code uses K&R-style declarations, or `#undef isalpha`
 * removed Apple's prototype version. Then tcc might emit STT_NOTYPE,
 * which my fix would misclassify as data and emit an extrel for. */
#include <stdio.h>
extern void *isalpha;        /* declared as data, not function */
extern int isupper();        /* K&R declaration, no proto */

typedef int (*ctype_fn)(int);
static ctype_fn t1 = (ctype_fn)isalpha;
static ctype_fn t2 = isupper;

int main(void) {
    printf("t1(a)=%d t2(a)=%d\n", t1('a'), t2('a'));
    return 0;
}
