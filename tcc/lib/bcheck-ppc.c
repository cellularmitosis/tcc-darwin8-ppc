/*
 * bcheck stub for 32-bit PowerPC (Apple Mach-O), installed as bcheck.o.
 *
 * tcc -b -run looks up bcheck.o BY NAME via tcc_add_support() in
 * tccelf.c::tcc_add_runtime() (called from tccrun.c). The file must
 * exist on disk -- if it doesn't, tcc errors out with
 * "file 'bcheck.o' not found" before any compilation begins.
 *
 * The actual __bound_* symbol stubs already live in lib-ppc.c
 * (and therefore libtcc1.a). All we need here is a valid object
 * file. A single static placeholder satisfies that without
 * shadowing or conflicting with any libtcc1.a symbol.
 *
 * When we eventually port the real bcheck.c, this file goes away
 * and we bring lib-ppc.c's __bound_* stubs along with it (they'd
 * become the real impls). Until then, this is the minimum that
 * unblocks `tcc -b -run` on PPC.
 */
static const char bcheck_ppc_stub_marker[] = "tcc-ppc bcheck stub v1";
