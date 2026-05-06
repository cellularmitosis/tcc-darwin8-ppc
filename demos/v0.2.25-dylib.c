/* v0.2.25-dylib.c — first dylib end-to-end on Tiger PPC.
 *
 * Two pieces in one file (split out at runtime by the runner):
 *
 *   - libgreet.dylib, exporting:
 *       int greet(const char *who);
 *       int get_count(void);
 *   - main, which dlopens libgreet.dylib and calls both.
 *
 * Demonstrates: tcc -shared produces a Mach-O dylib that dyld
 * loads via dlopen, dlsym finds exported functions, and PIC stubs
 * route libSystem calls (printf, strlen) correctly.
 *
 * See demos/v0.2.25-dylib.sh for the actual build commands.
 */

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

int main(int argc, char **argv) {
    void *h = dlopen("./libgreet.dylib", RTLD_NOW);
    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    int (*greet)(const char *) = (int (*)(const char *))dlsym(h, "greet");
    int (*get_count)(void)     = (int (*)(void))dlsym(h, "get_count");
    if (!greet || !get_count) {
        fprintf(stderr, "dlsym: %s\n", dlerror());
        return 1;
    }
    int total = greet("World");
    total += greet("Tiger");
    total += greet("PowerPC");
    printf("Total chars: %d, total calls: %d\n", total, get_count());
    dlclose(h);
    return 0;
}
