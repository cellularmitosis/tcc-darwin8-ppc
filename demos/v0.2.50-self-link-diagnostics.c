/* v0.2.50-g3 — self-link diagnostics (roadmap #7).
 *
 * Happy-path program: nothing here exercises the new diagnostics
 * directly. The diagnostics fire when tcc's Mach-O EXE writer would
 * otherwise produce a syntactically valid but semantically broken
 * executable — those bugs surfaced historically as cryptic dyld
 * errors (Cannot allocate memory, Symbol not found:
 * __mh_execute_header) or SIGBUS during crt1 startup.
 *
 * The session-068 README documents the four invariants now checked
 * before the EXE writer emits a single byte:
 *   (a) VM layout: segment alignment, vmsize >= filesize, no
 *       overlap, __LINKEDIT placed after __TEXT+__DATA in both VA
 *       and file space.
 *   (b) Required symbols: `__mh_execute_header` is registered as
 *       N_ABS at __TEXT base; entry_addr falls inside __text.
 *   (c) Stub/slot wiring: every stub VA lies in __symbol_stub1;
 *       every __nl_symbol_ptr slot lies in __nl_symbol_ptr; every
 *       ST_PPC_NEEDS_STUB symbol got a stub allocated.
 *   (d) Section-presence: every defined symbol's section is in the
 *       EXE writer's emitted section list.
 *
 * Each invariant was verified by hand-injecting a break right
 * before the checks, rebuilding, and observing the resulting
 * tcc-level error message — see docs/sessions/068.
 *
 * This program exercises a mix of features that all four checks
 * touch under the hood (text, rodata, data, bss, libSystem-imported
 * extern functions, a static initializer with a function pointer)
 * so the EXE writer's happy path runs every check on every emitted
 * section. If any check fires in the future, this build will fail
 * with a useful error instead of producing a binary that crashes.
 */

int   printf(const char *, ...);
void *malloc(unsigned long);
void  free(void *);

static const char  ro_msg[] = "rodata works";
static int         bss_arr[8];
static int         data_val = 42;
static void      (*ctor_ptr)(void *) = free;

int main(int argc, char **argv)
{
    char *p = malloc(64);
    int i;
    (void)argc; (void)argv; (void)ctor_ptr;
    for (i = 0; i < 8; i++) bss_arr[i] = i * 11;
    printf("%s | data=%d | bss=%d %d %d | p=%s\n",
           ro_msg, data_val,
           bss_arr[0], bss_arr[3], bss_arr[7],
           p ? "ok" : "null");
    free(p);
    return 0;
}
