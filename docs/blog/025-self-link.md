# Eight bus errors and a one-line fix

*Teaching a tiny C compiler to link its own programs, on a 22-year-old
PowerPC Mac.*

---

## The setup

We've been porting [TCC](https://repo.or.cz/tinycc.git), the Tiny C
Compiler, to a 2003-vintage PowerPC G3 running Mac OS X 10.4 Tiger.
TCC has had backends for x86, ARM, and a few others, but never for
PowerPC. The whole project is a slow-motion bring-up: each session
adds one more feature — first integer arithmetic, then function calls,
then floating point, then structs, eventually self-hosting (TCC
compiling its own source code into a working compiler).

By the time this session started, we had a real win. TCC could read
C code and emit PowerPC machine code into a real Mach-O object file
— the macOS equivalent of an ELF `.o` on Linux. We could even compile
TCC *itself* with TCC, producing an output file byte-identical to the
one its predecessor made. That's the canonical "self-host fixpoint":
proof that the compiler is internally consistent.

But there was a stubborn loose end. To turn that `.o` file into an
actual runnable executable — a thing you can type at the shell and
have it print "hello, world" — we still needed `gcc-4.0` to do the
*link* step. TCC could produce parts; gluing them into a final
program was Apple's compiler's job. That feels wrong for a
self-contained toolchain. The whole point is to stop needing the
other compiler.

This session was about closing that gap.

## What's actually missing

When you write `int main() { printf("hi\n"); }` and compile it, the
compiler doesn't produce the whole executable directly. It produces
your `main` function plus a bunch of references to things it doesn't
own: `printf`, the C runtime startup, the place where `argv` gets
unpacked from the kernel. Those references get filled in by the
linker, which pulls together your code, the C library, and a small
piece of glue called the *startup file*. On macOS this glue is
`/usr/lib/crt1.o`. It's the very first code that runs when you launch
a program, and it's what makes `printf` work — it sets up the global
state that `libSystem` (Apple's C library) expects.

If TCC could read `/usr/lib/crt1.o` and stitch its contents into our
output, we could link executables ourselves. TCC has a perfectly good
ELF reader for this kind of thing. It just doesn't have a Mach-O
reader. We had to write one.

That's what session 025 was. It's also why this writeup exists:
between "let's read this file format" and "the executable runs", I
hit a wall eight times in a row, each one teaching a small thing
about why old-Mac executables work the way they do. Most of those
fixes were small. The last one was *one character*.

## The wall, eight times

**1. The file isn't what you think it is.** First attempt: feed
`crt1.o` to TCC, watch it explode. The error was "unrecognized file
type". I'd carefully written the magic-number check for `0xfeedface`
(the Mach-O sentinel) and... it wasn't matching. Five minutes of
disbelief later: `crt1.o` doesn't *start* with `0xfeedface`. It
starts with `0xcafebabe`, the marker for a "fat binary" — Apple's
container format that bundles four versions of the file (PowerPC,
PowerPC-64, Intel-32, Intel-64) into one. We had to detect the fat
wrapper, scan its directory, and fish out just the PowerPC slice.

**2. Defining the same thing twice.** With the file readable, the
linker complained that `_NXArgc` was defined twice. Of course it was:
crt1 defines it (it's where `argc` ends up), and last session we
wrote a tiny shim that defined it ourselves because we *didn't* have
crt1. With crt1 in the picture the shim was now a duplicate. Easy
fix: skip injecting our shim when crt1's signature symbol `start` is
present.

**3. A name in the wrong section.** Now an undefined-symbol error on
something called `_L.0`. It took a while to figure out what `_L.0`
even was — it's TCC's auto-generated label for an anonymous string
literal (the `"hi\n"` in your code). The string was correctly
*emitted*, but our executable writer was looking for it in a section
called `.rodata`, while TCC actually puts string literals in a
section called `.data.ro`. After loading crt1 (which contributed a
`.rodata` of its own, from its `__cstring` section), we had two
parallel sections and our writer picked the wrong one. Fix: map
crt1's section to TCC's existing name so they merged.

**4. No room at the inn.** Next gripe: an undefined symbol with the
charming name `___darwin_gcc3_preregister_frame_info`. This is a
"common" symbol — declared but not defined, the linker is supposed
to allocate space for it in the `.bss` section. Our writer didn't
know about `.bss`. Adding it was straightforward, but the kind of
straightforward that involves three coordinated edits across two
hundred lines: layout the section in memory, emit the right
zero-fill flags, adjust the segment's virtual size.

**5. Address collision.** Compiles cleanly, runs, and immediately
dies with `dyld: Cannot allocate memory`. That's not actually about
memory; it's the dynamic linker rejecting a structurally invalid
binary. The bug: I'd added the new `.bss` section to the data
segment's *virtual* size but used the *file* size when computing
where `__LINKEDIT` (the metadata segment) should live. So
`__LINKEDIT` and `.bss` claimed the same address range. The dynamic
linker noticed and bailed. One-line fix.

**6. Looking in the wrong place.** Now `dyld` complains that
`__mh_execute_header` isn't in libSystem. Of course it isn't —
that's a magic symbol that points at the executable's own Mach-O
header. The trick is that crt1 references it as if it were external,
expecting *us* to define it. We were defining it inside the writer
but never registering it in TCC's symbol table, so when our
allocator scanned for unresolved externals it saw crt1's reference
and dutifully sent dyld looking for it in libSystem. Pre-define it
in the symbol table; problem solves itself.

**7. Branching into data.** Compiles, links, *almost* runs. Bus error
during crt1's startup. Disassembling the binary and tracing through
crt1 by hand revealed that some of its function calls — sequences
like `lis r12, ha(addr); ori r12, lo(addr); mtctr; bctr`, which on
PowerPC means "load this address into r12 and branch to it" — were
loading the address of a non-lazy pointer slot (i.e. *data*) and
trying to execute it. The original crt1 had wanted those addresses
to point at *stubs* (small bridge routines that lazy-load the real
function from libSystem). My reader had collapsed both cases into
one and lost that distinction. Fix: tag the symbol with a custom
"this needs a stub" bit when we recognize the original was a stub
reference.

**8. The one-character fix.** With all of the above in place, simple
programs worked. `printf("hi\n")` ran, exited zero, the works.
Triumph. So I tried linking TCC itself. New error: `___ashldi3`
(GCC's runtime helper for shifting 64-bit integers) was undefined,
even though I was pulling in `libgcc.a` which definitely contains it.
Tracing showed that the file `_ashldi3.o` *was* being loaded — but
`___ashldi3` wasn't ending up in our symbol table. Several hours of
disbelief later, the bug:

```c
for (i = 1; i < nsyms; i++) {  /* should be i = 0 */
```

ELF, the executable format on Linux that TCC originally targets,
reserves symbol index 0 as a sentinel "null symbol". Every loop that
walks an ELF symbol table starts at 1, because index 0 is meaningless.
Mach-O has no such convention — symbol 0 is a real symbol like any
other. I'd copied the ELF idiom without thinking. For most files,
where there are dozens of symbols, this skipped one that didn't
matter. But `_ashldi3.o` is a tiny file: it defines exactly one
symbol, at index 0, and that symbol is `___ashldi3`. My loop ran zero
times. Change `i = 1` to `i = 0` and everything downstream just
worked.

## Stepping back

What does it feel like, working at this layer? Mostly it feels like
detective work in a museum. PowerPC was last shipped in a Mac in
2006. Apple's documentation for the Mach-O format is patchy in
several places and contradictory in a few. The dynamic linker (`dyld`)
gives almost no diagnostics when something is structurally wrong: you
get "cannot allocate memory" or a bus error and have to disassemble
your own binary, hex-dump it, and reason backwards. Stack traces are
luxuries from another era. There's no debugger that understands TCC's
output, because TCC doesn't yet emit debug info for PowerPC.

The compensating reward is that everything is *small*. Mach-O file
headers are 28 bytes. Each load command is between 16 and 200 bytes
of dense, fully-specified structure. The PowerPC instruction set is
a clean 32-bit fixed-width design — `lis r12, 0x1234` is always four
bytes and you can read the immediate field straight off the page.
When you fix something, you can immediately see it in the binary.
There's no opaque toolchain layer between you and the result.

The one-character bug at the end is the kind of thing that's
embarrassing in the moment and instructive in retrospect. ELF and
Mach-O look superficially similar — both are "binary file formats
for object code", both have section tables and symbol tables and
relocation entries — but they're products of different design
cultures, separated by nearly two decades. Mach-O came out of
NeXTSTEP in the late 1980s; ELF arrived with System V Release 4 in
1988 and got refined through the 90s. The little decisions diverge
constantly, and copying conventions from one to the other works
right up until it doesn't.

The fix landed. `tcc -o hello hello.c` now produces a working
executable on a 22-year-old G3, with no help from Apple's compiler.
Next session: get the same thing working for TCC compiling itself.
