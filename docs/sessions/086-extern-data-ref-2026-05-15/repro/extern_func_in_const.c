/* Mirror sed's pattern: const struct { name, fn } table with extern
 * functions. The funcptrs land in __TEXT,__const (read-only). If my
 * change emits external relocs for those, dyld can't write into the
 * read-only segment → SIGBUS. */
#include <stdio.h>
#include <ctype.h>

typedef struct { const char *name; int (*fn)(int); } entry;
static const entry table[] = {
    { "alpha", isalpha },
    { "upper", isupper },
    { "lower", islower },
    { "digit", isdigit },
};

int main(void) {
    int i;
    for (i = 0; i < 4; i++) {
        printf("%s('a'+i) -> %d\n", table[i].name, table[i].fn('a' + i));
    }
    return 0;
}
