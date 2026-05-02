# 027 — self-link works: tcc-self runs entirely on tcc-emitted machine code

## Result

✅ **`tcc-self` runs.** A `tcc-self` binary linked entirely by `tcc`
itself (no `gcc-4.0` involvement, just `tcc tcc.o libtcc1.a`)
launches, executes `_start`, runs `main`, and prints
`tcc version 0.9.28rc (PowerPC Darwin)`.

✅ **`tcc-self` compiles working executables.** Hello-world,
moderate-size programs that use `printf`/`malloc`/`getenv`,
long-long arithmetic — all compile via `tcc-self -o exe …` and the
resulting binary runs cleanly.

🟡 **`tcc-self` cannot recompile `tcc.c` itself.** A pre-existing
codegen bug (introduced somewhere in 023-025; documented in
[026/findings.md](../026-libgcc-helpers/findings.md)) means
`tcc-self -c tcc.c` faults with `EXC_BAD_ACCESS` inside `fflush`,
called from `_tcc_warning`. Diagnostics suggest it's a stack /
string corruption during warning emission for some particular C
construct in tcc.c. Self-host fixpoint test still fails. Tracking
as session 028.

## Arrival state

Coming out of [026](../026-libgcc-helpers/README.md):
- `libtcc1.a` had the libgcc helpers tcc.c needs.
- `bootstrap-tcc-self.sh` reverted to gcc-link because the
  tcc-link path produced a SIGBUS-on-launch tcc-self.
- The crash chain was diagnosed as `_libc_initializer →
  __NSGetEnviron → _malloc_initialize → null deref`, but the
  underlying cause hadn't been pinned down.

## What was actually wrong (four overlapping bugs)

This took a lot of bisecting. There were FOUR independent issues in
the EXE writer's symbol-table emission path, all converging on the
same surface symptom (`tcc-self` crashes in `_malloc_initialize`
before `main` runs). They had to be peeled off one at a time.

### Bug 1 — `__keymgr_dwarf2_register_sections` is load-bearing

`crt1.o`'s `_start` calls
`__keymgr_dwarf2_register_sections(eh_start, eh_end)` very early,
intended to register C++ exception unwind tables. The real
implementation (in `libSystem`) does two things:

1. Calls `_dyld_register_func_for_add_image(noop_handler)`.
2. Calls `_dyld_register_func_for_remove_image(noop_handler)`.

Step 1 is a *load-bearing* side effect: registering an add-image
callback causes dyld to synchronously dispatch the callback for
every already-loaded image, and that walks libSystem's
`__mod_init_func` section, kicking `_libc_initializer` (which sets
up the malloc subsystem). Without that side effect, libSystem's
init is deferred forever, and the very first `malloc()` faults
inside `_malloc_initialize`.

When tcc-link assembles a binary, `__keymgr_dwarf2_register_sections`
is left as an undefined extern resolved against libSystem. That
resolution succeeds — but for reasons we still don't fully
understand, calling libSystem's version directly from crt1's
`_start` (without gcc-4.0's libgcc.a "keymgr glue" objects in the
link) crashes inside the function.

**Fix:** `tccelf.c` auto-injects this stub into every PPC EXE
output:

```c
extern int _dyld_register_func_for_add_image(...);
extern int _dyld_register_func_for_remove_image(...);
extern void _dyld_lookup_and_bind(...);
static void __tcc_keymgr_noop(const void *mh, long s) { ... }
void __keymgr_dwarf2_register_sections(void *begin, void *end) {
    _dyld_register_func_for_add_image(__tcc_keymgr_noop);
    _dyld_register_func_for_remove_image(__tcc_keymgr_noop);
}
```

Same shape as the existing `__tcc_start_main` injection. The
function symbol shadows libSystem's via flat namespace, gets
called by crt1, runs the side effect, returns clean.

### Bug 2 — `__DATA,__dyld` was eight zeros

crt1's `__dyld_func_lookup` reads two function pointers from the
start of the executable's `__DATA,__dyld` section:

* offset 0: `__dyld_func_lookup` (in dyld, fixed at 0x8fe01000 on Tiger)
* offset 4: `dyld_stub_binding_helper` (in dyld, fixed at 0x8fe01008 on Tiger)

These bytes are pre-populated in `crt1.o`'s own `__DATA,__dyld`
section. gcc-4.0's ld copies them through unchanged. Our writer
was synthesizing a fresh 8-byte `__DATA,__dyld` section and zeroing
it — so crt1's `__dyld_func_lookup` would call dyld at address 0,
faulting before any other diagnostic could happen.

**Fix:** in `ppc-macho.c::macho_output_exe`, populate the synthesized
`__DATA,__dyld` with `0x8fe01000` and `0x8fe01008`. These are
constant on Tiger PPC (no ASLR, dyld always at 0x8fe00000). If
Tiger's dyld ever changes layout, we'd need to read the bytes out
of `crt1.o`'s `__DATA,__dyld` instead — but for now hardcoding
keeps the change small.

### Bug 3 — `_environ` symbols missed `REFERENCED_DYNAMICALLY`

`_libc_initializer` calls `__NSGetEnviron` to get the address of
the executable's `_environ` symbol, and immediately dereferences it
to set up libSystem's environment-array pointer. `__NSGetEnviron`
walks dyld's symbol-lookup machinery looking for a symbol named
`_environ` with the `REFERENCED_DYNAMICALLY` (0x10) bit set in
`n_desc`. Symbols without that bit are invisible to the lookup.

Our writer was emitting `_environ`, `_NXArgc`, `_NXArgv`, and
`__mh_execute_header` with `n_desc=0`. `__NSGetEnviron` returned
NULL. `_libc_initializer` dereferenced NULL. SIGBUS.

**Fix:** in `ppc-macho.c`, set `n_desc = REFERENCED_DYNAMICALLY` for
`_environ`, `_NXArgc`, `_NXArgv`, `___progname`, and
`__mh_execute_header` when emitting the nlist.

### Bug 4 — externally-defined symbols must be SORTED

This was the killer, and the diagnosis took the longest. Even with
all three fixes above, `tcc-self` *still* crashed. After reducing
to a minimal repro (varying the user-function count of a tiny
C program through tcc-link), we found:

```
N user functions  -> nextdefsym  -> exit code
       18                39          0   (works)
       20                41        138   (crashes)
       22                43        138   (crashes)
```

So crossing some specific external-defined-symbol count broke it.
But the threshold *moved* depending on what other symbols were
present.

The ah-ha moment came from comparing `nm -p` output (which preserves
symtab order) between the working gcc-linked binary and our
tcc-linked one:

```
gcc-link binary:
  _NXArgc      _NXArgv      ___ashldi3      ___clear_cache  ...     # alphabetical!
tcc-link binary:
  __tcc_error  _tcc_malloc  _tcc_free       _tcc_realloc    ...     # insertion order
```

**dyld uses BINARY SEARCH on the externally-defined symbol table.**
It assumes the table is sorted alphabetically by name. If unsorted,
lookups for `_environ` (or anything else) past a few entries silently
return NULL, and your binary faults in the `__NSGetEnviron` call
inside `_libc_initializer`.

**Fix:** `ppc-macho.c` collects all external defined symbols into
parallel arrays, sorts them alphabetically by name (insertion sort —
n is ~80 max, so the O(n²) doesn't matter), then emits.

Bonus: this rewrite also dropped the spurious extra `_start` we
were emitting alongside crt1's `start` (causing duplicate
definitions), and skipped some crt1 internals (`__start`,
`__dyld_func_lookup`, `dyld_stub_binding_helper`,
`___keymgr_dwarf2_register_sections`) that gcc-4.0's ld
silently demotes to local — keeping our external table tighter and
thus speeding dyld's binary search.

### Bonus tcc PPC backend bug discovered along the way

`put_nlist` was originally:

```c
static void put_nlist(obuf *nlist, uint32_t strx,
                      uint8_t n_type, uint8_t n_sect,
                      uint16_t n_desc, uint32_t n_value)
{
    put32be(nlist, strx);
    put8(nlist, n_type);   ...
}
```

When tcc-built tcc compiled this function, the four middle bytes
(`n_type`, `n_sect`, two halves of `n_desc`) all came out as zero
in the produced binary's nlist entries. Smells like a tcc PPC
backend issue with passing/storing narrow integer args across a
function-call boundary at certain stack-frame configurations.
Workaround: assemble the 12-byte nlist entry into a `unsigned char
buf[12]` with explicit byte-shift stores, then call `obuf_put` once.
No narrow-int call args. Documented at the function definition.

## What landed

| File | Change |
|---|---|
| `tcc/tccelf.c` | Auto-inject `__keymgr_dwarf2_register_sections` stub for PPC EXE output. |
| `tcc/ppc-macho.c` | Populate `__DATA,__dyld`; set `REFERENCED_DYNAMICALLY` on dyld-discovered symbols; sort ext defs alphabetically; skip crt1 internals; skip duplicate `_start`; bypass tcc backend bug in `put_nlist`. |
| `scripts/bootstrap-tcc-self.sh` | Drop `gcc-4.0` link, use `tcc` itself for the self-link step. |

## What works

* `tcc-self -v` prints the version.
* `tcc-self -B./tcc -I./tcc -o exe foo.c libtcc1.a` for hello-world,
  malloc/printf/getenv programs, programs needing libtcc1.a's
  long-long helpers (link-wise, see caveat below).
* The full bootstrap chain: gcc-4.0 builds `tcc`, `tcc` compiles +
  links `tcc-self`, `tcc-self` runs.

## What's still broken

1. **Self-host fixpoint** still doesn't hold. `tcc-self -c tcc.c`
   crashes in `fflush`/`_tcc_warning` partway through compilation.
   This is independent of 027's work — likely the same regression
   noted in 026's findings (introduced somewhere in 023-025), now
   surfaced more clearly because we're actually running tcc-self.
   Tracked as session 028.

2. **Long-long shifts compiled by tcc-self** produce wrong results
   in some cases (test program `0x100000000LL << 4` returns 0
   instead of `0x1000000000`). The same source compiled by `tcc`
   (gcc-built) produces the right result. Looks like another
   tcc-PPC-backend codegen bug similar to the `put_nlist` one above.
   Tracked separately.

3. **Common symbols from crt1.o** end up in `.bss` but their type
   is wrong in tcc-self output (lowercase `c` instead of uppercase
   `D`/`T` in nm). This is the same `put_nlist` bug as #2 — the
   workaround above fixes the *defined extern* path but the
   common-symbol allocation path likely shares the same vulnerable
   pattern.

## Files touched

| | |
|---|---|
| `tcc/ppc-macho.c` | +231 / -52 lines |
| `tcc/tccelf.c` | +58 lines |
| `scripts/bootstrap-tcc-self.sh` | switch from gcc-link to tcc-link |
| `docs/sessions/027-self-link/README.md` | (this file) |
