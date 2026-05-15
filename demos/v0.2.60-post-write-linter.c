/* v0.2.60-g3 milestone — post-write linter demo (EXE path).
 *
 * Touches text + rodata + data + bss + a libSystem stub (printf) so
 * the EXE writer emits a representative load-command set: __PAGEZERO,
 * __TEXT, __DATA, __LINKEDIT, LC_LOAD_DYLINKER, LC_LOAD_DYLIB
 * libSystem, LC_SYMTAB, LC_DYSYMTAB, LC_UUID, LC_UNIXTHREAD. The
 * post-write linter (new this milestone) re-reads the just-emitted
 * file and verifies every shape invariant. The linter is silent on
 * the happy path; this demo just confirms the build still succeeds
 * and the program still runs.
 */
#include <stdio.h>

static const char *g_ro = "rodata-string";
int  g_data = 42;
int  g_bss[3];

int main(void)
{
    g_bss[0] = 11;
    g_bss[1] = 22;
    g_bss[2] = 33;
    printf("v0.2.60 ok: ro=%s | data=%d | bss=%d %d %d\n",
           g_ro, g_data, g_bss[0], g_bss[1], g_bss[2]);
    return 0;
}
