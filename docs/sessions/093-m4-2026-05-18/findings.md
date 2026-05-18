# Findings — session 093 (GNU m4 with tcc, no source change)

## 1. tcc resolves `-I` paths strictly left-to-right; there is no `-isystem`-style demotion

This was the headline blocker. gcc and clang split include search
into "user" (high priority) and "system" (low priority) lanes —
even if you put `-I /usr/local/include` BEFORE `-I .`, the latter
wins for user code. tcc has only one lane: a flat list searched in
command-line order.

The demos to date all set
```sh
CC="$TCC -B$TCCROOT -L$TCCROOT -I$TCCROOT/include"
```
The `-I$TCCROOT/include` is redundant — `-B` already brings in
tcc's sysinclude as a fallback. It worked fine for non-gnulib
projects (bc, expat, sed, gzip, ...) because they have no override
headers — there's nothing in their own source tree named
`stddef.h`, so `<stddef.h>` resolves to tcc's regardless of
order.

m4 is the first demo project that ships **wrapper headers under
its own `lib/`** (a generated `lib/stddef.h` that uses
`#include_next <stddef.h>` to chain to the real one after
inserting its own substitutes — `rpl_max_align_t` etc.). The
project's Makefile adds `-I.` (`lib/`) to compile lines.

With `-I$TCCROOT/include -I.` (as the demos' CC pattern produces),
tcc's stddef.h wins, gnulib's wrapper is never seen, and
`max_align_t` ends up undeclared because tcc only declares it
under `__STDC_VERSION__ >= 201112L` (which is not the default).

Fix: drop `-I$TCCROOT/include` from CC. `-B$TCCROOT` alone covers
the sysinclude path as a low-priority fallback. Verified that
non-gnulib projects (bc) still build correctly without it.

**How to apply:** for any future demo of a gnulib-using project,
use `CC="$TCC -B$TCCROOT -L$TCCROOT"` (no `-I`). For pure-C
projects without wrapper headers, either form works.

**Possible future tcc improvement (not done this session):**
teach tcc to support `-isystem` so demos could pass
`-isystem $TCCROOT/include` and get proper low-priority semantics.
This would make the existing demos' CC pattern continue to work
unchanged even on gnulib-using projects. Estimated scope: an
afternoon — `-isystem` is a single new argv flag that appends to a
separate sysinclude list, the same one `-B` already populates.

## 2. Modern gnulib's `lib/sigsegv.c` has a Tiger-PPC ucontext bug

m4-1.4.19 (2021) bundles a 2021-vintage gnulib whose `lib/sigsegv.c`
expects the modern macOS ucontext field naming:

```c
#elif defined __powerpc__
#  define SIGSEGV_FAULT_STACKPOINTER  ((ucontext_t *) ucp)->uc_mcontext->__ss.__r1
```

Tiger's `<mach/ppc/_types.h>` defines `ppc_thread_state` /
`__darwin_ppc_thread_state` with fields named `srr0`, `srr1`,
`r0`...`r31` — **no leading underscores**. The `__ss`/`__r1`
naming is a Leopard+ convention. gcc-4.0 on Tiger fails on this
same file for the same reason.

This is the first time we've seen a real-world program tripped by
gnulib's modernity, not by tcc. The session pivoted to m4-1.4.18
(2016), which uses an older gnulib snapshot that doesn't bundle
sigsegv.c at all (m4 1.4.18 implements stack overflow recovery via
the c-stack module alone, no libsigsegv).

**How to apply:** when surveying a gnulib-using project for a
Tiger demo, grep `lib/sigsegv.c` and `lib/`-wide for
`uc_mcontext->__ss`. If present, expect a port effort beyond the
demo's scope. Older releases of the same project are often a
practical answer because their gnulib snapshot predates the
problem. (See also: modern gnulib's `<sys/random.h>`, `<spawn.h>`
substitutes — likely similar minefield.)

## 3. The autoconf macro processor passes its own self-test under tcc

m4 1.4.18 ships a `checks/` subdirectory containing **236 macro
expansion tests transcribed verbatim from the GNU m4 manual**.
Every documented macro/builtin has at least one example there —
`define`, `ifdef`, `pushdef`, `popdef`, `dnl`, `len`, `substr`,
`translit`, `index`, `regexp`, `patsubst`, `format`, `divert`,
`undivert`, `divnum`, `eval`, `incr`, `decr`, `ifelse`, `len`,
`include`, `sinclude`, `syscmd`, `esyscmd`, `sysval`, `mkstemp`,
`maketemp`, `errprint`, `m4exit`, `m4wrap`, `traceon`, `traceoff`,
`debugmode`, `debugfile`, `dumpdef`, `m4_arg`, `__file__`,
`__line__`, `__program__`. Plus a stack-overflow recovery test.

Pass count: 236/236 (with 6 changeword tests skipped because
GNU m4 1.4.18 disabled the changeword feature by default — those
skips happen on any build, tcc or gcc).

This is a substantial **self-validating** test suite — markedly
stronger than bc's `Test/` (which is timing inputs). And it's
*the* program autoconf runs every probe through, so a tcc bug in
m4 would have rippled through every autoconf-based demo. It
didn't.

**How to apply:** m4-1.4.18's `make -C checks check` is now in
our toolbelt as a real correctness signal for tcc — every time we
rebuild tcc and want to assert "no regression in autoconf-driven
programs," running this end-to-end is a deeper test than any
synthetic suite we maintain in-tree.

## 4. Cross-validation against Tiger's /usr/bin/m4 (1.4.2) works the
same way bc cross-validation does

Tiger ships `/usr/bin/m4` = `/usr/bin/gm4` = GNU m4 1.4.2 (2007,
compiled by Apple gcc-3.3 / gcc-4.0). The codebase is the same as
1.4.18 for the bulk of macro-expansion semantics; the 1.4.3–1.4.18
delta is mostly bug fixes and a couple of new builtins
(`mkstemp`, `pushdef` semantic tweaks) — but every test in our
cross-validation corpus uses macros that existed in 1.4.2.

Output is bit-identical across 26 macro-expansion lines covering
define / eval / ifelse / len / substr / translit / index /
recursion (factorial up to 12, Fibonacci to fib(15)) / divert /
undivert with output reordering.

Same pattern as session 092's bc validation: a tcc codegen bug
in string handling, branching, recursion, or token streaming
would show up as a diff somewhere in those 26 lines. None did.

**How to apply:** Tiger ships its own `/usr/bin/<thing>` for
quite a few classic tools — bc, m4, awk, sed, gzip, tar — all
buildable from upstream sources. Continue leaning on this for
demos: the "compile newer upstream with tcc, diff vs. shipped
binary" mode is the strongest correctness check we have.

## 5. Don't accept `make: 'X' is up to date` as evidence the link worked

When investigating an intermittent "macho: symtab/strtab out of
bounds" error during the m4 test-suite build, the rebuilt
`test-xalloc-die` binary existed AND make reported success on the
next invocation — yet the prior run had failed with `Error 1`.

The clue: a make rule can emit a non-zero exit from one of its
sub-commands and still leave an apparently-valid output file in
place, especially when the failure was on a build step that's
later in the recipe than the one that produces the named target.
When that happens, the next `make` sees the file's mtime and says
"up to date" — which is technically true, even though the prior
invocation failed.

In this session it turned out to be intermittent (couldn't
reproduce on a fresh rebuild) and not a real tcc bug — likely a
spurious failure on the very slow G3 under load. But the lesson is
real:

**How to apply:** when investigating an unexpected error during
a make-driven build, don't trust a follow-up `make` reporting
"up to date" as evidence the original error was a transient. Run
`make clean && make` and reproduce from scratch before deciding
it's an intermittent.

## 6. m4-1.4.19 is reachable with a Tiger-PPC ucontext patch — but skipping it for now is the right call

A real Tiger-PPC port of gnulib's sigsegv.c needs:
- Replace `uc_mcontext->__ss.__r1` with `uc_mcontext->ss.r1` on
  Tiger PPC, and the same de-prefixing for any other arch fields.
- Either gate the patch on a config-time check ("does the system
  use the modern field convention?") or hardcode it on
  `__APPLE__ && __MACH__ && __ppc__ && < 10.5`.

This is a portability fix to gnulib, not a tcc fix. Submitting it
upstream is the right thing eventually, but it's not on our
critical path. m4-1.4.18 builds cleanly and demonstrates
everything we wanted to demonstrate.

**How to apply:** if a future contributor wants the
"m4-1.4.19 with tcc" milestone, the gnulib patch is the only blocker
once the include-order fix from finding #1 is applied.
