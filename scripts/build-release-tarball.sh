#!/bin/sh
# build-release-tarball.sh — produce a /opt-installable tarball for
# tcc-darwin8-ppc on Tiger PowerPC. Default version v0.2.0-g3.
#
# Layout (tarball expands into /opt/tcc-darwin8-ppc-<version>/):
#   bin/tcc                  — the self-hosted PPC binary
#   lib/tcc/libtcc1.a        — runtime helpers
#   lib/tcc/include/         — tcc's own headers
#   share/doc/tcc-darwin8-ppc/README
#   share/doc/tcc-darwin8-ppc/RELEASE_NOTES
#   share/doc/tcc-darwin8-ppc/SELF_HOST_NOTES   (020 writeup)
#   share/doc/tcc-darwin8-ppc/SELF_LINK_NOTES   (027 writeup)
#
# Run on the G3 (or wherever a working ./tcc exists). Tarball lands
# in <repo>/artifacts/ (gitignored — canonical copy is the GitHub
# release asset).

set -e

cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUTDIR="$ROOT/artifacts"
mkdir -p "$OUTDIR"

VERSION="${VERSION:-v0.2.36-g3}"
PKGNAME=tcc-darwin8-ppc-$VERSION
TARNAME=$PKGNAME.tar.gz
PREFIX=/opt/$PKGNAME

# Step 1: build the host tcc fresh.
echo "==> step 1/4: build host tcc with gcc-4.0"
( cd tcc && [ -f config.mak ] || ./configure --config-semlock=no --prefix=$PREFIX )
( cd tcc && /opt/make-4.3/bin/make >/dev/null )

# Step 2: bootstrap tcc-self with the full self-host fixpoint.
# bootstrap-tcc-self.sh produces a tcc-self compiled AND linked by
# tcc itself (no gcc-4.0). FIXPOINT=1 then verifies tcc-self2.o ==
# tcc-self3.o (canonical self-host fixpoint).
echo "==> step 2/4: bootstrap tcc-self + fixpoint test"
WORK=/tmp/$PKGNAME-build/self1
OUT=/tmp/$PKGNAME-build/tcc-self
mkdir -p "$WORK"
FIXPOINT=1 TCC=./tcc/tcc OUT="$OUT" WORK="$WORK" \
    ./scripts/bootstrap-tcc-self.sh

# Step 3: stage the install tree.
echo "==> step 3/4: stage install tree under /tmp/$PKGNAME"
STAGE=/tmp/$PKGNAME
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib/tcc/include" "$STAGE/share/doc/tcc-darwin8-ppc"
install -m 755 "$OUT" "$STAGE/bin/tcc"
install -m 644 tcc/libtcc1.a "$STAGE/lib/tcc/libtcc1.a"
install -m 644 tcc/include/*.h "$STAGE/lib/tcc/include/"
install -m 644 tcc/tcclib.h "$STAGE/lib/tcc/include/"
install -m 644 README.md "$STAGE/share/doc/tcc-darwin8-ppc/README"
install -m 644 docs/sessions/020-self-host/README.md "$STAGE/share/doc/tcc-darwin8-ppc/SELF_HOST_NOTES"
install -m 644 docs/sessions/027-self-link/README.md "$STAGE/share/doc/tcc-darwin8-ppc/SELF_LINK_NOTES"
cat > "$STAGE/share/doc/tcc-darwin8-ppc/RELEASE_NOTES" <<EOF
tcc-darwin8-ppc $VERSION
========================

Fully self-hosted release: tcc compiles AND links itself with no
gcc-4.0 involvement. Targets 32-bit PowerPC on Mac OS X 10.4
Tiger (Mach-O). Built and bootstrapped on a real iBook G3/G4.

What's new (cumulative since v0.1.0-g3):
  * Self-link: bootstrap-tcc-self.sh no longer needs gcc-4.0; tcc
    links its own tcc-self via auto-loaded /usr/lib/crt1.o.
  * Bundled libgcc helpers in libtcc1.a (long-long arithmetic,
    IEEE 754 conversions, __eprintf for assert, no-op bound-check
    stubs, single-threaded atomic stubs).
  * Self-host fixpoint: tcc -> tcc-self -> tcc-self2 -> tcc-self3,
    with tcc-self2.o == tcc-self3.o byte-identical.
  * Struct-by-value parameters (≤ 32 bytes) AND struct returns via
    hidden pointer per the Apple PPC ABI.
  * EXE PIC reloc fallback handles forward-defined and aliased
    symbols (a regression's break-out-of-loop bug is also fixed).
  * Big-endian sub-word param offset; FP-to-LL libgcc helper
    return-swap; absolute-address load/store; computed goto;
    void* deref; VT_BOOL handling.
  * Tiger realpath workaround in normalized_PATHCMP.
  * Long-frame prolog/epilog (>32KB stack frames work).
  * Long-offset local load/store/address-of.
  * VLAs (variable-length arrays) supported via gen_vla_alloc.
  * Constructor / destructor support: __attribute__((constructor))
    and __attribute__((destructor)) emit __mod_init_func and
    __mod_term_func sections that dyld walks at startup / exit.
  * VLA + function-call interaction is now safe: each VLA reserves
    a 128-byte buffer below its data so callee param-spills don't
    corrupt the array.
  * tcc -run mode works for the first time on PPC:
    create_plt_entry + relocate_plt now generate proper 4-instruction
    branch islands (lis/ori/mtctr/bctr) that JIT-resolve every libc
    call through dlsym. Simple programs (printf hi-world, struct
    tests, etc.) now run end-to-end via "tcc -run".
  * Lock-free atomics for 1-, 2-, and 4-byte widths via lwarx/stwcx
    (tcc/lib/atomic-ppc.S, compiled by gcc-4.0 via a per-file
    Makefile rule). Byte and short use word-RMW with masking
    since PPC32 has no lbarx/sbarx. 8-byte stays under
    pthread_mutex (no ldarx/stdcx on PPC32). Real-world impact:
    124_atomic_counter (16 threads x 65535 ops x 4 widths)
    drops from ~6m23s to 2.4s -- 137x speedup.
  * Variadic FP arg shadow spill: a long-standing codegen bug
    where printf calls with FP args whose GPR shadow slots ran
    past r10 (gslot >= 8) didn't actually write the value to the
    outgoing parameter stack. printf read garbage from there.
    The fix flipped two tests (73_arm64, 70_floating_point_literals)
    that had been suspected of unfixable platform-mismatch /
    upstream-parser causes.
  * Honest test classification on PPC: bcheck-asserting tests
    (112, 113, 115, 116, 117, 126) are now skipped on PPC because
    we have no-op bound-check stubs but no real bcheck.c port
    yet. Tests that just need linking (121, 122, 132) still pass.
    128_run_atexit skipped because it uses BSD on_exit not in
    Tiger libSystem. 125_atomic_misc pinned to -run via per-test
    T1 override (the test gates main behind #if defined test_*,
    which only fires under -dt -run).
  * Atomic OP_fetch family + memory fences + __atomic_is_lock_free
    added so 125_atomic_misc can actually run.
  * tests2 baseline at this release: 109 / 111 (98.2%) under the
    default -o exe path. Total count drops from 118 to 111 because
    of the bcheck/backtrace/on_exit skips listed above. Two real
    failures remain (60_errors_and_warnings test_scope_1 + a
    96_nodata_wanted bitfield case) -- both look like specific
    codegen bugs in narrow paths.

  * v0.2.12: two backend bugs surfaced trying to compile sqlite3
    amalgamation (3.46.1, 257K lines). Both now fixed:
      - struct-deref-by-value: passing *p (struct via pointer
        deref) by value now compiles. Previously errored
        "deref of basic type 0x7 not yet supported".
      - cross-TU PIC reloc translation: the Mach-O reader was
        unconditionally skipping all scattered SECTDIFF relocs,
        so when linking multiple tcc-built .o files, the second
        TU's references to extern data symbols (__sF, etc.) read
        from random addresses. Fix translates them to
        R_PPC_HA16_PIC / LO16_PIC like our own codegen, with
        per-reloc anchor recorded for the linker.
    Real-world impact: lua 5.4.7 now builds and runs
    end-to-end (hello-world, fib, math.lua, the full standard
    library). sqlite3 amalgamation builds cleanly and \`./sqlite3
    -version\` / \`.help\` work; \`select 1+1\` still hits a
    separate codegen bug under investigation.

  * v0.2.13: BE enum-constant value clobber. tcc's sym_link wrote
    local_scope into s->sym_scope on every identifier link, but
    sym_scope shares storage with the low half of enum_val in the
    Sym union. On big-endian PPC, that erased the visible value:
    every parameter-scope enum constant came back as 1. Fix is a
    one-line guard in sym_link to skip the write for IS_ENUM_VAL
    syms (canonical scope already lives on the enum tag, per
    sym_scope_ex's existing read-side handling).
    tests2 now 110/111 (99.1%); 60_errors_and_warnings passes
    with this fix + a per-test pin to -run on PPC (mirrors 125,
    needed because -dt only expands per-test_X discrimination in
    -run / preprocess output_types).
    Also adds scripts/bench.sh — a compile-time benchmark that
    builds lua 5.4.7 (33 .c files) with tcc / gcc -O0 / gcc -Os,
    discards the first run, reports steady-state seconds. Initial
    numbers on a 900 MHz iBook G3 (PowerPC 750):
    tcc 2s, gcc-O0 17s, gcc-Os 41s.

  * v0.2.14: BE bitfield read-back bug. The static initializer
    writes bitfields byte-by-byte (LSB-first) but the runtime
    load goes through the wide-container path (LSB-first within a
    BE-loaded int) — same bit numbering, different memory bits.
    On LE these agree because byte order matches bit numbering;
    on BE they don't. Fix forces every bitfield to use the byte-
    wise load_packed_bf / store_packed_bf paths on PPC32. Cost:
    ~2-4 byte loads per access vs 1 int load + shifts. tests2
    climbs to **111/111 (100.0%)** — first time at full pass.

  * v0.2.15: two PPC long-long bugs surfaced trying to compile
    sqlite3_open. Both now fixed:
      - Apple PPC ABI long-long alignment: tcc was using 8-byte
        alignment for double / long long inside structs; the Apple
        PPC32 ABI (and gcc-4.0 on Tiger) uses 4-byte. Fix: gate
        \`*a = 4\` in tccgen.c::type_size on PPC.
      - LL field-load address-clobber: the PPC32 BE LL split-load
        \`incr_offset(+4); load LOW; incr_offset(-4); load HIGH\`
        clobbers the address register on the LOW load, so the -4
        step operates on the loaded value rather than the address.
        For VT_LLOCAL lvalues, snapshot vtop's r/c.i/r2 before
        the LOW load and re-anchor for the HIGH load (which then
        re-derives the pointer fresh from the saved local).
    Real-world impact: \`sqlite3_open(":memory:", ...)\` now
    completes (was crashing inside sqlite3PcacheRefCount).

  * v0.2.16: LL-arg shuffle in gfunc_call clobbered another arg's
    address register. Surfaced by \`sqlite3 :memory: "select 1+1"\`
    (SEGV in sqlite3RunParser) and by zlib \`./example\` (SEGV
    in gz_open's LSEEK call). The second pass of gfunc_call
    processes args from vtop downward; arg2 (a long long) gets
    materialized into temp regs then \`mr\`'d into target_hi /
    target_lo (PPC ABI slots). Those mr's clobber the registers,
    one of which held arg1's lvalue address. Fix: save_reg on
    target_hi and target_lo before materializing the LL — moves
    any vstack entry using those regs to a stack local first.

    Real-world impact: \`sqlite3 :memory: "select 1+1"\` returns
    2 (was SEGV). zlib's full test suite passes. File-based
    sqlite (\`/tmp/test.db\`) still crashes inside sqlite3_open_v2
    — distinct bug in the file-open path; deferred.

  * v0.2.17: alloca() works correctly. Previously, programs that
    called alloca and then made any further function call (e.g.
    \`p = alloca(N); strcpy(p, "x"); printf("%s", p);\`) would
    silently corrupt the alloca'd memory (subsequent printf's
    outgoing param area landed inside the alloca'd region) and
    eventually segfault on epilog (epilog used \`addi r1, r1,
    frame_size\` which doesn't account for alloca having moved
    sp). Fix: epilog restores SP via the back chain (\`lwz r1,
    0(r1)\`) and saved regs via FP (r31) instead of SP. New
    \`lib/alloca-ppc.S\` reserves a 256-byte safety zone below
    the user's region so subsequent param spills land in the
    zone, not in alloca'd memory.

    Also: full \`tcctest.c\` (the upstream 4500-line tcc stress
    test) now runs to completion (was SEGV at line 770/4500 in
    \`stdarg_test\`). New \`__bound_*\` library stubs unblock
    \`-b\` compilation. Mach-O \`bcheck.o\` / \`bt-log.o\` shipped
    so \`tcc -b -run\` finds them.

  * v0.2.18: Two more independent codegen fixes:
    - Variadic FP arg past 8 FPRs was wrong: an off-by-one on
      TREG_TO_FPR (which already returns 1-indexed) made us
      stfd a different FPR than gv() loaded into. Surfaced by
      printf with 8 ints + 10 doubles (manyarg_test) producing
      huge garbage for the 9th and 10th doubles.
    - Float negation (\`-x\` for double/float) used the generic
      LE-only sign-flip (XOR 0x80 with byte at offset size-1).
      On big-endian PPC the sign bit is at offset 0. Routed
      TOK_NEG through gen_opf for PPC, which emits a real
      \`fneg\` instruction.

    Real-world impact: bzip2 1.0.8 now builds + round-trips
    end-to-end with tcc-darwin8-ppc (third real-world program
    after lua and zlib). tcctest's diff vs reference shrinks
    from 255 to 122 lines (the remaining diffs are all known
    long-double-128 / _Bool-size / static-init-addend issues).

  * v0.2.19: Three more codegen fixes:
    - FP comparison >= and <= now correctly return false for NaN
      operands (per IEEE 754). Old code tested "branch when LT
      bit clear", which fires for NaN since NaN sets only the
      FU/SO bit. Fixed via cror to merge FU into LT/GT before bc.
    - Pointer-to-wider-int cast now follows the destination's
      signedness (matches gcc on Apple PPC). Previously always
      sign-extended; (unsigned long long)(char*)0xFFFFFFBE came
      out as 0xFFFFFFFFFFFFFFBE instead of the expected
      0x00000000FFFFFFBE.
    - Long long arg straddling the r10/stack boundary (gslot=7,
      HIGH in r10 LOW on stack) now writes BOTH halves correctly.
      Old code's gv(RC_R(gslot)) loaded only one half into r10.

    Plus tcctest.c's LONG_DOUBLE = double on Apple PPC (we use
    8-byte double for long double; matching gcc's reference
    output requires both compilers do the same). tcctest's diff
    vs reference shrinks from 122 to 44 lines. Remaining diffs
    are known: aligntest3/4 alignof=4 vs gcc's 8 (Apple PPC ABI
    convention from session 041); _Bool size 1 vs gcc-4.0 quirk
    of 4; relocation_test &arr[N] addend (deferred); promote
    char/short funcret undefined-behavior corner.

  * v0.2.20: Apple PPC32 ABI 13-FPR support (NB_REGS 18 → 23,
    f1..f13 instead of f1..f8). Old code capped FP arg passing
    at 8 FPRs, the SysV/Linux PPC convention. Apple PPC actually
    uses f1..f13 (the AIX/PowerOpen variant). Functions with
    more than 8 FP args used to fail with "ppc-gen: parameters
    exceed 8 FPR slots". After this, abitest::ret_8plus2double
    passes (had been failing since v0.2.0).

    Also new __gcc_qadd / qsub / qmul / qdiv / __cmpdi2 /
    __ucmpdi2 stubs in lib-ppc.c so binaries that link
    gcc-4.0-built objects (e.g. libtcc.a built during the
    build-tiger bootstrap step) can find these libgcc helpers
    that Tiger libSystem doesn't ship. Falls back to plain
    double arithmetic for the IBM double-double helpers.

  * v0.2.21: __builtin_fabs / __builtin_fabsf / __builtin_inf /
    __builtin_inff stubs. Tiger's <math.h> for PPC declares
    fabs / inf etc. via macros that expand to these gcc-style
    builtins; gcc inlines them but tcc emits regular function
    calls. Without stubs, programs that include <math.h> and
    use fabs/inf hit "Symbol not found" at startup.

    Real-world impact: cJSON 1.7.18 builds + parses end-to-end
    with tcc-darwin8-ppc (fourth real-world program after lua,
    zlib, bzip2). cJSON exercises a different mix of paths than
    the others: heavy linked-list traversal, strdup/free
    patterns, recursive descent parsing.

  * v0.2.22: \`int *p = &arr[N]\` (N != 0) static initializers
    now actually point at \`&arr[N]\`. Pre-v0.2.22 they collapsed
    to \`&arr[0]\` because \`R_PPC_ADDR32\` overwrote the in-place
    value (which is the implicit ELF Rel addend) instead of
    adding to it. Three fixes: ppc-link.c::relocate() switches to
    add (matches i386's R_386_32); the Mach-O exe writer's resolver
    mirrors that; the Mach-O .o loader zeros the in-place after the
    addend is absorbed into a synthetic anchor sym. Drops
    tcctest.c diff vs gcc reference 44 -> 33 lines.

  * v0.2.23: full sqlite3 with on-disk database works end-to-end
    -- fifth real-world program (after lua, zlib, bzip2, cJSON).
    Static-init data slots like \`(sqlite3_syscall_ptr)fcntl\` used
    to resolve to the \`__nl_symbol_ptr\` slot ADDRESS (4 bytes of
    data, not callable code), so calling them faulted. Fix: in
    \`collect_extern_stubs\`, also allocate a stub when an extern
    STT_FUNC is data-referenced via \`R_PPC_ADDR32\` (was: only
    \`R_PPC_REL24\` calls). The slot now holds a stub address;
    calling the stub jumps via \`__nl_symbol_ptr\` (filled by dyld
    lazily) to the real function.

  * v0.2.24: \`tcc -ar\` is now native Mach-O on PPC (was: ELF
    parser only, fell back to system /usr/bin/ar). New code path
    inside \`tcc_tool_ar()\` handles fat (universal) binaries by
    locating the PPC32 slice, parses the Mach-O header + LC_SYMTAB,
    walks 12-byte nlist entries, and selects defined-external
    symbols. Removes the \`XAR=\$(AR)\` workaround in tcc/lib/
    Makefile so libtcc1.a builds via the in-tree tcc. tcc is now
    fully self-sufficient on Tiger PPC -- no system ar needed.

  * v0.2.25: \`tcc -shared\` produces a real Mach-O dylib on Tiger
    PPC. dyld can load it via dlopen, dlsym finds exported
    functions, function-call stubs survive dyld's slide. New
    \`macho_output_dylib()\` in ppc-macho.c (~600 lines, mirrors
    \`macho_output_exe\`): MH_DYLIB filetype; no __PAGEZERO /
    LC_UNIXTHREAD / crt-shim; LC_ID_DYLIB for self-name; default
    __TEXT vmaddr 0x40000000. Function stubs are PIC -- 8
    instructions / 32 bytes per stub, with a (slot - .L1) SECTDIFF
    anchor invariant under whole-image slide.

    Multi-dylib link-time support (loading non-libSystem dylibs
    via \`tcc -lfoo\` to resolve link-time references) is deferred
    to a follow-up release. Programs that only use libSystem
    symbols + dlopen Just Work today.

  * v0.2.26: link-time dylib support. \`tcc -lz hello.c\` now
    actually works at runtime. \`macho_load_dll()\` (was a no-op
    stub) parses the dylib's Mach-O header, walks LC_SYMTAB to
    register each defined-external symbol as UNDEF in our own
    symtab, captures the install name from LC_ID_DYLIB, and adds
    a DLLReference. The exe/dylib writer emits one LC_LOAD_DYLIB
    per loaded dll (libSystem first, extras after). With extra
    dylibs loaded we switch to flat namespace (clear MH_TWOLEVEL)
    so dyld searches all loaded dylibs at runtime -- avoids
    per-symbol two-level ordinal tracking.

    Closes upstream \`dlltest\` (multi-session deferred): tcc
    compiles libtcc.c into libtcc2.dylib, links a tcc2 exe
    against that dylib, and tcc2 -run prints "Hello World"
    through the round trip. Verified with Tiger's bundled
    /usr/lib/libz.1.dylib (zlibVersion, adler32 work at runtime).

  * v0.2.27: long-deferred JIT heisenbug fixed (5+ sessions).
    When tcc compiles itself, the inline-asm \`__clear_cache\`
    in ppc-macho.c falls through to a no-op stub (we have no
    PPC inline-asm parser). \`tccrun.c::protect_pages\` calls
    \`__clear_cache\` after JIT codegen, but the stub did
    nothing -- so JIT page reuse across iterations (same
    address each time) saw stale icache. Failure modes:
    SIGILL / SIGBUS / SIGSEGV / silent-wrong-result, all
    layout-sensitive. The comment in the no-op stub had
    actually acknowledged this and was the trail to the fix.

    Fix: delegate to libSystem's \`sys_icache_invalidate\`,
    which performs dcbst/sync/icbi/sync/isync internally.
    One-line change.

    Test impact: tests2 still 111/111 and bootstrap fixpoint
    still holds. test3 now runs to completion (was SEGV at
    line ~770/4500 of tcctest); abitest-tcc passes 20 of
    ~24 sub-tests deterministically (was variable 5--19);
    dlltest is now stable across the full upstream
    \`make -k test\` run.

  * v0.2.28: abitest-tcc 24/24 + Tiger PPC backtrace support.

    (a) gfunc_call's save_regs(nb_args+1) skipped the func-ptr
    slot. For indirect calls where the func pointer is an
    LVAL with its address in a volatile GPR (e.g.
    \`(*(s2->f2 = &f) + 0)(v,v,v,v,v,v,1.0)\` from upstream
    many_struct_test_3), the arg-pass-2 code overwrote that
    register with v.a before the call site could gv() it.
    Switched to save_regs(nb_args), matching x86_64-gen.c.
    many_struct_test_3 + stdarg_test + stdarg_many_test +
    stdarg_struct_test + arg_align_test all flip from
    crash/fail to pass; abitest-tcc is now 24/24.

    (b) Implemented rt_get_caller_pc + rt_getcontext for
    Apple PPC (Tiger 10.4 mcontext->ss.{srr0,r1}) in
    tccrun.c. Backtraces from JIT-caught crashes now print
    real addresses (was \`??\`). libtest_mt's "producing
    some exceptions" stage passes -- the signal-handler
    longjmp path needed working stack walk. test1b now
    reports real "RUNTIME ERROR: invalid memory access"
    with backtrace.

  * v0.2.29: dylib slide support. Pre-v0.2.29, tcc-built
    dylibs only worked when dyld loaded them at the preferred
    vmaddr (0x40000000). If dyld had to slide them (e.g.
    because that range was occupied), absolute references
    inside the dylib pointed at the unslid VAs and either
    crashed or returned garbage.

    Two changes:
    (a) ppc_need_pic_for_sym() now returns 1 for locally-
    defined symbols in DLL mode too. This routes code refs
    to local data through the PIC sectdiff (sym - anchor)
    pattern that's invariant under whole-image slide. The
    PIC base setup (mflr/bcl/mflr/mtlr giving r30 as anchor)
    is now also pinned to function prolog for DLL mode --
    lazy emission was unsafe when the first PIC use was
    inside a conditional branch.

    (b) macho_output_dylib() now walks the relocation tables
    of __data, __mod_init_func, __mod_term_func looking for
    R_PPC_ADDR32 entries against locally-defined symbols.
    For each, it emits a Mach-O VANILLA local relocation_info
    entry into LC_DYSYMTAB.locrel. dyld walks this table at
    load time and adds the slide amount to each location --
    fixing static-initializer pointers like
    \`int *p = &arr[3];\` and \`(void(*)())fns[] = {f1,f2}\`.

    Limitation: \`static const struct {const char *name;...}
    tbl[]\` placed in __const can't be slid (read-only at
    runtime; dyld can't write). Such tables continue to
    work when no slide happens.

  * v0.2.30: Mach-O archive alacarte loader. Pre-v0.2.30, tcc
    force-loaded every member of every \`.a\` archive
    (whole-archive). For an archive with a hundred members of
    which only three are referenced, the resulting binary
    pulled in 97 redundant \`.o\` files, and any one of those
    unrelated members crashing during load could fail an
    otherwise-fine link.

    v0.2.30 parses the archive's BSD \`__.SYMDEF SORTED\` (or
    \`__.SYMDEF\`) symbol table, mapping each defined symbol to
    its member's file offset, and pulls in only the members
    needed to resolve currently-undefined symbols. Iterates
    until no new members get added (so transitive dependencies
    work).

    Also extends the existing SysV-style \`/\` symdef path to
    sniff each loaded member's magic and route Mach-O \`.o\`
    members through macho_load_object_file — this lets
    tcc -ar-built archives (libtcc1.a) keep loading via the
    SysV path while gaining alacarte semantics.

  * v0.2.31: const-data-with-pointers slide fix. Pre-v0.2.31,
    \`static const struct {const char *name; ...} tbl[]\`
    landed in __TEXT,__const (read-only at runtime); dyld
    couldn't write the slide fixups to the embedded \`name\`
    pointers, so under slide they stayed unslid and any
    dereference crashed. v0.2.29's locrel emission only
    covered __data / __mod_init_func / __mod_term_func; the
    pointers in __const went unslid.

    Fix: when output_type is TCC_OUTPUT_DLL, tccgen routes
    initialized const data to data_section instead of
    rodata_section. The whole table now lives in __DATA,__data
    which IS writable and IS slid by dyld; the existing locrel
    walker picks up the embedded pointers. Trade-off: lose the
    RO protection for const data in dylibs, but ensure
    correctness under slide.

    Practical impact: libtcc.c compiled as -shared produces a
    dylib whose \`tcc_options[]\`-style tables now slide
    correctly. Verified by dlopening libtcc2.dylib with a
    MAP_FIXED blocker over 0x40000000 and exercising
    \`tcc_compile_string\` + \`tcc_relocate\` + \`tcc_get_symbol\`
    end-to-end — pre-v0.2.31 SIGBUS'd in tcc_compile_string;
    post-v0.2.31 the JIT'd function runs and returns 42.

Install:
  sudo mkdir -p $PREFIX
  sudo tar -C /opt -xzf $TARNAME
  Then add $PREFIX/bin to PATH or invoke as $PREFIX/bin/tcc.

Try it:
  $PREFIX/bin/tcc -B$PREFIX/lib/tcc -run hello.c
  $PREFIX/bin/tcc -B$PREFIX/lib/tcc -o hello hello.c \\
      $PREFIX/lib/tcc/libtcc1.a

See $PREFIX/share/doc/tcc-darwin8-ppc/SELF_LINK_NOTES for the
v0.2.0 milestone writeup (the four overlapping bugs that had to
be peeled off before self-link worked).

Project: https://github.com/cellularmitosis/tcc-darwin8-ppc
EOF

# Step 4: tar it up.
echo "==> step 4/4: package artifacts/$TARNAME"
( cd /tmp && tar czf "$OUTDIR/$TARNAME" "$PKGNAME" )
ls -la "$OUTDIR/$TARNAME"

echo
echo "Done. Install with:"
echo "  sudo tar -C /opt -xzf artifacts/$TARNAME"
