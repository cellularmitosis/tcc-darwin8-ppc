/* v0.2.59-g3 — dylib self-link diagnostics (roadmap #7, dylib half).
 *
 * Companion source for v0.2.59-dylib-self-link-diagnostics.sh:
 * a dlopen driver that loads libdiag.dylib (built by the script
 * from an inline source) and calls its exported entrypoints.
 *
 * The interesting work is in the dylib itself; this driver exists
 * only so we can prove the dylib actually loads and runs after
 * the four pre-write sanity checks have approved its layout.
 *
 * Expected output (single line):
 *     ro=ok | data=42 | bss=11 22 33 | got=ok | call_count=2
 */

#include <stdio.h>
#include <dlfcn.h>

int main(int argc, char **argv)
{
    void *h;
    int (*greet)(const char *);
    int (*get_count)(void);
    const char *(*get_ro)(void);
    int *(*get_data_ptr)(void);
    int *(*get_bss_ptr)(void);
    void (*(*get_init_fn_ptr)(void))(int *);
    int *p;
    void (*fn)(int *);
    int slot;

    (void)argc; (void)argv;

    h = dlopen("./libdiag.dylib", RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

    greet           = (int (*)(const char *))         dlsym(h, "diag_greet");
    get_count       = (int (*)(void))                 dlsym(h, "diag_get_count");
    get_ro          = (const char *(*)(void))         dlsym(h, "diag_get_ro");
    get_data_ptr    = (int *(*)(void))                dlsym(h, "diag_get_data_ptr");
    get_bss_ptr     = (int *(*)(void))                dlsym(h, "diag_get_bss_ptr");
    get_init_fn_ptr = (void (*(*)(void))(int *))      dlsym(h, "diag_get_init_fn_ptr");

    if (!greet || !get_count || !get_ro || !get_data_ptr
     || !get_bss_ptr || !get_init_fn_ptr) {
        fprintf(stderr, "dlsym: missing one or more symbols\n");
        return 1;
    }

    /* Touch every section class via the dylib's exports. */
    greet("first");
    greet("second");

    p = get_bss_ptr();
    p[0] = 11; p[1] = 22; p[2] = 33;

    /* Function-pointer-in-data exercises the locrel slide-time
     * fixup path, which is one of the things (a) layout checks
     * must keep clean. */
    fn = get_init_fn_ptr();
    slot = 0;
    fn(&slot);

    printf("ro=%s | data=%d | bss=%d %d %d | got=%s | call_count=%d\n",
           get_ro()[0] ? "ok" : "empty",
           *get_data_ptr(),
           p[0], p[1], p[2],
           slot == 99 ? "ok" : "miss",
           get_count());

    dlclose(h);
    return 0;
}
