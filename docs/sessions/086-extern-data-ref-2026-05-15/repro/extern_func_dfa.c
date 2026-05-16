/* Mirror sed's exact dfa.c pattern: typedef function (not function
 * pointer) type, then pointer-to-typedef in a struct, then static
 * init with function names. */
#include <stdio.h>
#include <ctype.h>

typedef int predicate (int);
typedef int bool;
struct dfa_ctype {
    const char *name;
    predicate *func;
    bool single_byte_only;
};

static const struct dfa_ctype prednames[] = {
    {"alpha", isalpha, 0},
    {"upper", isupper, 0},
    {"lower", islower, 0},
    {"digit", isdigit, 1},
    {NULL, NULL, 0},
};

int main(void) {
    int i;
    for (i = 0; prednames[i].name; i++) {
        printf("%s('a'+i) -> %d (single=%d)\n",
               prednames[i].name,
               prednames[i].func('a' + i),
               prednames[i].single_byte_only);
    }
    return 0;
}
