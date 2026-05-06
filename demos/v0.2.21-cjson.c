/* v0.2.21-g3 milestone — cJSON 1.7.18 builds + parses end-to-end
 * with tcc-darwin8-ppc.
 *
 * cJSON is a small (single-file) JSON parser used by lots of C
 * code. Exercises a different mix of paths than lua/zlib/bzip2:
 * heavy linked-list traversal, strdup/free patterns, recursive
 * descent parsing, a malloc/realloc-heavy state machine.
 *
 * Build (after tar xzf cJSON-1.7.18.tar.gz in /tmp):
 *
 *     tcc -B<tccdir> -I /tmp/cJSON-1.7.18 -o /tmp/jdemo \
 *         /tmp/cJSON-1.7.18/cJSON.c demos/v0.2.21-cjson.c -lm
 *
 * Expected output:
 *     name=alice age=30
 *     sum=15
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

int main(void) {
    const char *json =
        "{\"name\":\"alice\",\"age\":30,\"items\":[1,2,3,4,5]}";
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        printf("parse fail: %s\n", cJSON_GetErrorPtr());
        return 1;
    }

    cJSON *name  = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *age   = cJSON_GetObjectItemCaseSensitive(root, "age");
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    printf("name=%s age=%d\n", name->valuestring, age->valueint);

    int sum = 0;
    cJSON *e;
    cJSON_ArrayForEach(e, items) sum += e->valueint;
    printf("sum=%d\n", sum);

    cJSON_Delete(root);
    return 0;
}
