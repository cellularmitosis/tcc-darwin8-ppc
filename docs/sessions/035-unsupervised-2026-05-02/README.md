# Session 035 — unsupervised continuation, 2026-05-02

Picking up from session 034's HANDOFF.md. The user invoked
unsupervised mode and asked me to continue working through the
v0.2.x roadmap, ship releases as appropriate, and document
everything.

## Headline result

| | start | end |
|---|---|---|
| HEAD | `9a5dc3f` (post-v0.2.2) | `ad144c0` (post-v0.2.3) |
| Releases on GitHub | 4 | **5** (v0.2.3-g3 cut mid-session) |
| tests2 baseline | 77 / 122 (63.1%) | **89 / 122 (73.0%)** |
| Self-host fixpoint | holds | holds |

Net **+12** tests passing, plus a release published with notes.

## What landed

In rough order:

| Commit | Why | tests2 effect |
|---|---|---|
| `902aea7` | EXE PIC reloc fallback: `break` → `continue` (regression from `9a5dc3f` was silently dropping every reloc after the first forward-defined symbol fixup, including printf calls in main) | +2 (17_enum, 120_alias unmasked) |
| `1479224` | gitignore release tarballs | — |
| `ef6fa2b` | tests2 Makefile: wrap `T1` in `( ... )` so $(FILTER)'s `2>&1` captures compile-step stderr too | +2 (33_ternary_op, 102_alignas) |
| `15deb71` | libtcc1 PPC: add `__eprintf` for `assert()` | +1 (33_ternary_op compile) |
| `0501a9f` | ppc-gen: VT_BOOL load/store, ggoto (computed goto), `*pv` of `void *` | +1 (88_codeopt) |
| `c36279f` | **Struct-by-value parameters** (≤ 32 bytes) | +3 (109, 135, 137) |
| `af615c2` | Release script bumped to v0.2.3-g3, notes refreshed | — |
| `82a583b` | README status to v0.2.3-g3 | — |
| `ad144c0` | Struct returns (hidden pointer ABI), dynamic param-area sizing, struct load via sym | +2 (130_large_argument, 131_return_struct_in_reg) |

Net effect: 77 → 89 / 122 (+12).

## What I tried but backed out

### char/short parameter byte-position fix

PPC is big-endian, so a `char` arg passed in r3 (zero/sign-extended
to 32 bits) ends up at the *end* of the 4-byte slot when the prolog
spills it: `stw r3, 24(r1)` writes the high bytes of r3 to offsets
24..26 (= 0 for an extended char) and the actual char to offset 27.

Setting `param_offset = 24 + 3` for VT_BYTE/VT_BOOL params (and `+2`
for VT_SHORT) fixes 23_type_coercion (which currently outputs
`char:  ` instead of `char: a`) and probably the visible failure in
129_scopes line 59 — but it **breaks the bootstrap fixpoint**:
`tcc-self2` SIGBUSes when invoked.

I confirmed by binary-search: reverting only the byte-offset change
restores fixpoint. The mechanism isn't fully understood — best
guess is that something in tcc-self assigns to a char parameter by
emitting `stb` at the unfixed offset somewhere, breaking
read/write symmetry on some path. Worth a deeper investigation in
a follow-up session; the change is too dangerous to ship without
understanding what it actually breaks. Diff is in the git log
under
[the 035 progress notes](#235-charshort-param-investigation-detail)
below.

### LL helper return swap by symbol token

`(unsigned long long)d` for double `d` calls `__fixunsdfdi`, which
returns r3=high, r4=low (Apple ABI). gfunc_call does an r3<->r4
swap on calls whose return type it knows is LL — but
`vpush_helper_func` pushes with `func_old_type` (returns int), so
the swap doesn't fire. The result: 111_conversion outputs
`5302428712241725440` (= `0x499602D200000000`, the value with the
two halves transposed) instead of `1234567890`.

Adding a token-list check in gfunc_call (recognize
`TOK___fixunsdfdi`, `TOK___udivdi3`, etc., and force the swap)
fixed the standalone test but **broke fixpoint**. Reason: tcc's
own body code uses these helpers and depends on the existing
"helpers leave the LL halves transposed in vtop, but we never
reorder them so it's internally consistent" behavior. Adding the
swap turns the inconsistency into a correctness bug at the
internal use sites.

A correct fix likely needs to either:
- patch `vpush_helper_func` to use a real LL-returning function
  type for the LL helpers (so PUT_R_RET/ret_is_ll detects them
  uniformly), and audit all the helper use sites in tccgen.c, OR
- handle the convention mismatch at the boundary between helper
  return and the next gfunc_call that consumes the LL value
  (i.e., another swap).

Neither is a 5-minute change, and getting it wrong breaks
self-host. Backed out.

## Codegen quirks burned in (current list)

These remain workarounds rather than backend bug fixes. Updated
from session 034's list:

1. `put_nlist` narrow-int args — bypass via inlined byte writes in
   `tcc/ppc-macho.c::put_nlist`.
2. Warning emission via `fprintf+fflush` — bypass via direct
   `write(2)` syscall in `tcc/libtcc.c::_tcc_error_noabort`.
3. 64-bit const-fold sign-ext — partial workaround in
   `tcc/tccgen.c::gv` (`vpushi → vpush64`); the deeper fix is the
   open question carried over from session 031.
4. **NEW:** char/short param big-endian offset (the +3/+2 fix
   above is correct but breaks fixpoint; not yet diagnosed).
5. **NEW:** LL-helper-return r3<->r4 swap (libgcc helpers leave
   the halves transposed; tcc body code consumes them in that
   transposed form consistently; fixing the helper convention
   breaks fixpoint).

## Findings & gotchas

### `( T1 )` shell precedence in tests2 Makefile

The line `T3 = $(FILTER) >$*.output 2>&1` worked unambiguously
with `T2 = -run NAME.c` (one process, one stderr stream). With
`T2 = NAME.c -o NAME.exe && ./NAME.exe`, shell precedence binds
the `>$*.output 2>&1` only to the right side of `&&`, so compile
warnings/errors went to terminal but never to `.output`. Brace
groups (`{ ...; }`) failed because Make's expansion put them in
the middle of the command (`tcc { src.c -o exe; }` instead of
the surrounding `{ tcc src.c -o exe; }`). Subshell parens (`( ... )`)
worked. This pattern only matters on PPC (where -run isn't yet
fully wired); arm64 etc. don't hit it.

### "Loading" a struct value really means producing its address

This is a tcc convention: VT_STRUCT rvalues flow through the
backend as pointers. The frontend hands the backend a VT_LVAL
SValue with type VT_STRUCT, and `gv()` is supposed to "produce
the value" — which for a struct means "compute &struct" into a
GPR. Without this, nothing involving struct args worked.

Three load-paths needed it: VT_LOCAL, via-symbol, and (probably)
via-PIC-symbol though I haven't tested the last one. Three were
enough to unblock tests 109 / 130 / 137.

### Struct-return ABI is callee-driven, not caller-driven

The frontend (`tccgen.c`) decides struct-return convention by
calling `gfunc_sret`. We return 0 (memory return). The frontend
then handles the caller side: allocate temp, push `&temp` as
implicit first arg, call `gfunc_call`, and the result lives in
`*temp`. The backend (`gfunc_prolog` / `gfunc_epilog`) only has
to handle the callee side — receive r3, store somewhere known
(`func_vc`), reload r3 from `func_vc(r31)` before `blr`.

The prolog's existing unconditional spill of r3..r10 to
`r1+24..r1+52` made this nearly free: setting `func_vc = 24` and
incrementing `gpr_index` by 1 was sufficient on the prolog side.
Six instructions in the epilog (the `lwz r3, func_vc(r31)`) and
that's the whole change.

### Apple ABI `___bound_*` symbols

Several remaining test failures (121_struct_return,
122_vla_reuse, etc.) link against `___bound_ptr_add` or
`___bound_local_new` because their `Makefile` line says
`FLAGS += -b`. Tiger's libSystem doesn't expose these — they're
in libgcc/libtcc1.a normally. Implementing `-b` for PPC would
need both the libtcc1.a helpers (mostly platform-independent) and
PPC-aware codegen for the bounds-check instrumentation. Listed
as a v0.3 item in the roadmap.

### Big-endian-specific test expectations

Several tests have `.expect` files that bake in
little-endian-specific output:
- `90_struct-init`: `gssu1: 5 0 0 0 3 0 0 0` (LE) vs our
  `0 0 0 5 0 0 0 3` (BE).
- `91_ptr_longlong_arith32`: `data = "0123-5678"` after pointer
  arithmetic that depends on the LE byte order; our BE output
  `data = "012345608"` is correctly computed for our endianness.
- `95_bitfields` / `95_bitfields_ms`: bitfield byte order differs
  by endianness.

These aren't tcc bugs; they're upstream-test assumptions. We
could ship per-platform `.expect` files, but it would be a lot of
churn.

## tests2 status snapshot at end of session

89 / 122 pass (73.0%). Breakdown of the 33 remaining fails:

- **Bound checking (`-b`) needed (8 tests):** 112_backtrace,
  113_btdll, 114_bound_signal, 115_bound_setjmp, 116_bound_setjmp2,
  121_struct_return, 122_vla_reuse, 126_bound_global, 132_bound_test
- **VLAs (4 tests):** 78_vla_label, 79_vla_continue, 122_vla_reuse,
  123_vla_bug
- **`-run` / -dt support (4 tests):** 60_errors_and_warnings,
  96_nodata_wanted, 104_inline, 117_builtins, 128_run_atexit
- **Atomic helpers (3 tests):** 124_atomic_counter, 125_atomic_misc,
  136_atomic_gcc_style
- **Constructor / destructor (1 test):** 108_constructor —
  needs `__mod_init_func` section emission
- **Endianness-specific .expect (4 tests):** 23_type_coercion (also
  char-param offset), 90_struct-init, 91_ptr_longlong_arith32,
  95_bitfields, 95_bitfields_ms
- **Float-literal parsing precision (1 test):** 70_floating_point_literals
- **HFA-on-PPC (1 test):** 73_arm64 (hits "argument list
  exceeds 8 FPR slots" with HFA aggregates of doubles)
- **Other / TBD (5 tests):** 18_include (SIGBUS), 101_cleanup,
  111_conversion (LL helper return swap), 119_random_stuff
  (load from absolute address), 129_scopes (likely byte-param
  offset)

## Important state for the next session

### Unchanged from 034 handoff

- Primary host: **`ibookg37`** (PowerBook G4 600MHz, Tiger
  10.4.11). `git` not in default `$PATH`; rsync is the
  established way to ship code.
- After every rsync, force a clean configure:
  `ssh ibookg37 'cd ~/tcc-darwin8-ppc/tcc && rm -f config.mak && cd .. && ./scripts/build-tiger.sh configure'`
- `./scripts/run-tests2.sh` and
  `FIXPOINT=1 ./scripts/bootstrap-tcc-self.sh` are the canonical
  verifications.
- "Unsupervised mode" rules in CLAUDE.md.

### New release infra

`scripts/build-release-tarball.sh` now defaults to v0.2.3-g3.
The release notes inside the tarball are kept terse; the longer
narrative goes into the GitHub Release body (see `/tmp/release-
notes-v0.2.3.md` in this session for the v0.2.3 example —
delete after release ships).

### v0.2.3-g3 published

URL: <https://github.com/cellularmitosis/tcc-darwin8-ppc/releases/tag/v0.2.3-g3>
Tarball: 143 KB.

## Recommended next steps

1. **Diagnose the char/short param fixpoint break.** This is
   the cheapest route to ~3 more tests (23_type_coercion,
   129_scopes, possibly 130). Plausible approach: bisect by
   adding the +3/+2 fix one bt at a time (just VT_BYTE first,
   not VT_SHORT/VT_BOOL), and run fixpoint after each.
2. **Diagnose the LL-helper return-swap fixpoint break.** Either
   patch `vpush_helper_func` or audit all helper use sites.
   Unblocks 111_conversion and probably exposes a dozen smaller
   bugs in long-long math under the hood.
3. **Bound checking** for PPC. ~7 tests, ~200-500 lines split
   between `lib/bcheck.c` integration and codegen
   instrumentation in `ppc-gen.c`. Substantial.
4. **`-run` mode for PPC.** Needed for several tests and would
   make the test runner simpler. Requires implementing
   `create_plt_entry` for the JIT path. Substantial.
5. **Constructor/destructor support.** One test (108) but the
   feature matters for real code. Needs `__mod_init_func`
   section emission in `ppc-macho.c`.

### 23.5 char/short param investigation detail

The diff that breaks fixpoint:
```diff
         } else {
             int slots = (bt == VT_LLONG) ? 2 : 1;
             param_offset = 24 + gpr_index * 4;
+            if (bt == VT_BYTE || bt == VT_BOOL) param_offset += 3;
+            else if (bt == VT_SHORT) param_offset += 2;
             gfunc_set_param(sym, param_offset, 0);
             gpr_index += slots;
         }
```

Symptom: `tcc-self2 -c tcc.c` SIGBUSes (Bus error). `tcc-self`
itself runs fine. So tcc-self builds tcc.c → tcc-self2.o (and
links → tcc-self2). When tcc-self2 tries to compile tcc.c, it
crashes.

The compiler crash means tcc-self2 has bad code. Since tcc-self
generates tcc-self2 by compiling tcc.c, the offset-fix broke
tcc-self's codegen for some specific construct in tcc.c.

Next-time investigation:
- `tcc-self2 -bench -c tcc.c -o /tmp/x.o` to see how far it
  gets before the bus error.
- gdb on tcc-self2 might show the exact instruction.
- Search tcc.c for `void f(char ...)` declarations to find
  candidate trigger sites.
- Possibly: it's not char/short PARAMS but rather char/short
  return values that are now misaligned.

## Files touched

- `tcc/ppc-gen.c` — struct args, struct returns, dynamic
  param area, VT_BOOL, ggoto, void* deref, VT_STRUCT loads
- `tcc/ppc-macho.c` — break/continue fix in PIC reloc fallback
- `tcc/lib/lib-ppc.c` — `__eprintf`
- `tcc/tests/tests2/Makefile` — `( T1 )` capture fix,
  MAP_ANONYMOUS alias for 119_random_stuff
- `scripts/build-release-tarball.sh` — v0.2.3-g3 default
- `README.md` — status to v0.2.3-g3 + new rows
- `.gitignore` — release tarballs

## Commit list (this session)

```
ad144c0 ppc-gen: struct returns + dynamic param-area + struct load via sym
82a583b README: status to v0.2.3-g3, add v0.2.2 + v0.2.3 rows
af615c2 v0.2.3-g3 release: struct-by-value args + EXE PIC fix + small wins
c36279f ppc-gen: struct-by-value parameters (≤ 32 bytes)
0501a9f ppc-gen: handle VT_BOOL load/store, computed goto, void* deref
15deb71 libtcc1 PPC: add __eprintf for assert()
ef6fa2b tests2 Makefile: capture compile-step stderr too
1479224 gitignore: release tarballs
902aea7 EXE PIC reloc fallback: fix break-out-of-loop bug
```

Plus the v0.2.3-g3 tag and GitHub release.
