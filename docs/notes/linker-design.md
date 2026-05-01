# tcc as its own linker — design context

## tcc's overall design

tcc was designed by Bellard from the start as a self-contained
toolchain: compiler + linker + assembler in one tiny binary. "The
whole toolchain in one tiny binary" is a core part of its appeal.
On every platform tcc supports, it does its own linking:

| Platform | Linker code | Notes |
|---|---|---|
| Linux ELF (i386, x86_64, ARM, ARM64, RISC-V) | `tccelf.c` | Reads `.o` and `.a`, resolves symbols, emits ELF executables and shared libs that interface directly with `ld-linux.so`. |
| Windows PE | `tccpe.c` | Same idea for PE/COFF. |
| Modern macOS (10.6+, x86_64 / arm64) | `tccmacho.c` | Mach-O executables with `LC_DYLD_CHAINED_FIXUPS`. No external linker needed. |
| **Tiger PPC (this fork)** | `ppc-macho.c` | Partial — see status below. |

So our reliance on `gcc-4.0`'s `ld` for the link step on PPC isn't
intentional design — it's a temporary scaffold while the writer
was incomplete. Tiger's classic Mach-O format (no chained fixups)
is structurally different from x86_64's modern format, so
`tccmacho.c` doesn't directly apply, and we're building the
equivalent from scratch in `ppc-macho.c`.

## Why it matters (disadvantages of leaning on gcc-4.0)

1. **Dependency.** A "tcc tarball" that requires Xcode/gcc-4.0 to
   produce binaries isn't self-contained. Defeats half the appeal
   of a 500 KB compiler.
2. **Speed.** tcc's whole pitch is "compile + link in milliseconds".
   `ld` from gcc-4.0 adds 100–500 ms per link. On a G3 the perceived
   hit is real.
3. **Self-host purity.** What we currently call "self-host" (session
   [020](../sessions/020-self-host/README.md)) only validates codegen
   — `tcc-self2` compiles `tcc.c` to a byte-identical `.o` as
   `tcc-self`, but the *link* step still uses gcc-4.0. A true
   self-host would have tcc-self link tcc-self2 as well. Without it,
   if gcc-4.0 disappeared from the system our toolchain stops being
   able to produce executables.
4. **Bootstrap on a minimal Tiger.** Somebody installing fresh Tiger
   without Xcode can't use tcc to produce executables yet.

## Why we still lean on it (for now)

1. gcc-4.0's `ld` is battle-tested for every Mach-O corner case
   (crt1, libgcc EH unwind, two-level binding hints, prebinding,
   weak refs, etc.) — handling all of that is a lot of code.
2. Going through `gcc-4.0 -arch ppc foo.o -o foo` was the fastest
   path to working binaries, so we could focus on codegen first.

## Current status (sessions 023+)

[023](../sessions/023-macho-executable/README.md) lands Phase A and
Phase B of a native Mach-O exe writer:

* **Phase A** — self-contained executables (no libSystem). Works for
  programs like `int main(){return N;}`, recursion, function calls.
* **Phase B** — libSystem stubs via dyld. Works for syscalls (write,
  read, getpid) and stateless libSystem fns (strlen, strchr).
  `tcc -o exe foo.c` produces a runnable Mach-O directly, no
  gcc-link.

What still requires gcc-link:

* Programs that touch libSystem-internal state (printf, puts, malloc,
  getenv, fopen). Apple's `crt1.o` does substantial setup that we
  don't yet replicate: sets `_NXArgc` / `_NXArgv` / `_environ`
  libSystem globals, calls `__dyld_func_lookup` to bootstrap dyld
  helpers, registers libgcc EH tables, runs library initializers,
  installs an `atexit` chain.

## Path to closing the gap

The remaining piece — pulling in `/usr/lib/crt1.o` automatically when
producing an executable — is structurally a Mach-O `.o` *reader*.
tcc already has analogues for this:

* `tccelf.c::tcc_load_object_file` reads ELF `.o` files.
* `tccmacho.c` reads modern Mach-O `.dylib` files (sym tables only).

We'd need a classic-Mach-O `.o` reader that can:

1. Parse the Mach header + LC_SEGMENTs.
2. Extract `__text`, `__cstring`, `__data`, `__dyld` sections.
3. Parse the symbol table (nlist) and string table.
4. Parse relocation entries (PPC_RELOC_*).
5. Parse the indirect symbol table.
6. Hand all of the above to our existing layout/output code so
   `crt1.o` gets merged into our executable like another input.

That's its own focused project (probably one or two more
substantive sessions). It's the piece that closes the gap between
"self-hosting compile" and "fully self-contained toolchain".
