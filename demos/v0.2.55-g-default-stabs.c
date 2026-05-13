/* v0.2.55-g3 — `-g` defaults to stabs on PPC Darwin
 * (roadmap #7.5 Phase 2, item 1d).
 *
 * Pre-v0.2.55, plain `tcc -g foo.c -o foo` on PPC Darwin emitted
 * DWARF-2 — historically reasonable (Tiger's `dwarfdump` reads
 * DWARF-2 cleanly) but Tiger's bundled `gdb 6.3` only partially
 * understands embedded DWARF in Mach-O. To get a gdb-debuggable
 * binary the user had to type `-gstabs` explicitly.
 *
 * v0.2.55 flips the Darwin default: bare `-g` now sets
 * `s->dwarf = 0` (= stabs), so `tcc -g hello.c -o hello && gdb
 * hello` Just Works on Tiger out of the box. DWARF is still
 * available via the explicit `-gdwarf-2` / `-gdwarf-4` flags.
 *
 * This demo:
 *   1. Builds with bare `-g` (no `-gstabs`!) and asserts the
 *      resulting exe carries the full stabs chain (FUN/SLINE/
 *      BNSYM/ENSYM/SO entries) in `LC_SYMTAB`, NOT a __DWARF
 *      segment.
 *   2. Builds the same source with explicit `-gdwarf-2` and
 *      asserts the resulting exe carries a __DWARF segment with
 *      __debug_info, NOT stabs entries — confirms DWARF is still
 *      reachable when asked for explicitly.
 *   3. Runs the bare-`-g` exe under gdb: break in `compute`,
 *      print params + locals + global, walk the backtrace,
 *      assert file:line is shown per frame.
 *
 * Program does a tiny mix: a global, a function with params and
 * locals, a printf — same shape as v0.2.52 but without the
 * cross-TU framing of v0.2.54. The point is solely the default
 * flip.
 */

#include <stdio.h>

int g_seed = 7;

int compute(int x, int y)
{
    int sum  = x + y;
    int prod = x * y;
    return sum + prod + g_seed;
}

int main(int argc, char **argv)
{
    int r = compute(3, 4);
    printf("result=%d\n", r);
    (void)argv; (void)argc;
    return 0;
}
