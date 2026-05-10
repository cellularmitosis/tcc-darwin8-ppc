# seed-1536 SEGV — root cause analysis

## TL;DR

Tcc's codegen for `func_40(((*l_45) = l_44), (*p_24), l_46[1])` in
the seed-1536 reduced repro emits a wrong instruction sequence: it
loads the 8-byte union arg3 (l_46[1]) into r5+r6, then issues
`mr r3, r5` immediately before `bl <func_40>`, overwriting r3 (which
should hold arg1 = pointer) with r5 (high half of arg3).

The high half of `union U2 l_46[1] = {-1L}` on BE PPC with MSB-first
bitfield packing (per commit fc241f5) is `0xF0000000` — the leading
4-bit signed bitfield `f0:4 = -1` lands in the top nibble of the
container. So r3 receives `0xF0000000` and func_40 dereferences a
wild pointer.

## Evidence

1. Crash address: `0x9618` in the reduced reproducer (test.c at 191
   lines, partly creduce-reduced from seed-1536.c).
2. Faulting instruction: `lwz r4, 0(r4)` after `lwz r4, 24(r31)`,
   where `r31+24` is the saved-r3 (first param) slot of the
   crashing function's frame.
3. Crashing function: `func_40` (entry at `0x92e8`, identified by
   binary order — last function before `_main` at `0xa428`, with
   first param a pointer per source).
4. Caller is `func_22` (entry `0x8750`), with `bl 0x92e8` at
   `0x8a04`.
5. Disassembly of arg setup at the call site:
   ```
   0x000089dc: lwz   r5,-16(r31)      ; r5 = l_44 (= &g_27)
   0x000089e0: stw   r5,0(r4)          ; (*l_45) = l_44 — side effect of arg1
   0x000089e4: lwz   r4,28(r31)        ; r4 = p_24 (frame param)
   0x000089e8: stw   r3,-148(r31)      ; spill some r3 to a temp
   0x000089ec: addi  r3,r31,-44        ; r3 = &l_46[1]
   0x000089f0: mr    r12,r3
   0x000089f4: lwz   r5,0(r12)         ; r5 = high half of l_46[1] = 0xF0000000
   0x000089f8: lwz   r6,4(r12)         ; r6 = low  half of l_46[1] = 0
   0x000089fc: lwz   r4,0(r4)          ; r4 = *p_24 = arg2
   0x00008a00: mr    r3,r5             ; *** BUG: r3 := high half of union ***
   0x00008a04: bl    0x92e8            ; call func_40
   ```

   The `mr r3, r5` is wrong. After the union is loaded into r5+r6 it
   should stay there (Darwin PPC32 ABI: arg3 is at GPR-pair r5+r6,
   not r3+r4). r3 should have been loaded with `l_44 = &g_27` for
   arg1; instead it gets `0xF0000000`.

## Why 0xF0000000

`union U2 l_46[1] = {-1L}` initializes only the first member,
`signed f0 : 4`, to -1 (= `0xF` in 4 bits). All other bits of the
union are zero (auto-storage with explicit init).

Big-endian PPC with MSB-first bitfield packing puts `f0` in the top
4 bits of its container. The union has an `int64_t f1`, so the
container is at least 8 bytes. The two halves split as:

- bytes 0..3 (high word, BE) = `0xF0000000`  ← top 4 bits set, rest zero
- bytes 4..7 (low  word, BE) = `0x00000000`

So the high-half load `lwz r5, 0(r12)` returns `0xF0000000`, exactly
matching the wild pointer seen at the crash.

## Likely codegen culprit

The bug is in `gfunc_call` (or its arg-shuffle helper) in
`tcc/ppc-gen.c`. Specifically, the path that handles passing a
struct/union as the LAST argument when earlier args occupy the
first GPRs.

Hypothesis A: tcc evaluates args in some order, places the union
in r5+r6 correctly, but then a final shuffle step incorrectly
copies r5 to r3 — perhaps confusing "first GPR of the struct" with
"first GPR of the call".

Hypothesis B: tcc treats the struct return / struct passing wrong
when the called function returns int16_t (func_40 returns
int16_t — a sub-word return). On Darwin PPC, int16_t return goes
in r3 normally. tcc may be confusing the small-return case with
the hidden-arg-pointer case used for large struct returns.

Next investigation step: read `gfunc_call` in `tcc/ppc-gen.c` and
look for the code path that emits the post-load-arg `mr` shuffle.

## Same bug shape probably explains seed-8271 and seed-8389

Both are SEGVs that surfaced from the `--builtins` campaign.
Source-shape signature:

- seed-8271 has multi-byte unions/structs passed by value.
- seed-8389 ditto.

If all three SEGVs share the "struct/union arg passing clobbers
earlier GPR" pattern, one fix in `gfunc_call` closes all three.

After landing a fix:
1. Rebuild libtcc1.a, fixpoint check, tests2, abitest.
2. Recompile seed-1536, seed-8271, seed-8389; verify all three
   no longer SEGV.
3. Re-run the relevant FAIL slice of csmith batches to confirm.

## Reduced reproducer

`docs/sessions/062-bswap-shim-builtins-2026-05-10/seed-1536-repro/test.c`
(currently 191 lines, in-flight creduce on uranium ssh imacg3 will
shrink it further). Bug reproduces today against tcc HEAD.
