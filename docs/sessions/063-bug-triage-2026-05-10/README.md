# Session 063 — bug triage continuation (2026-05-10)

Continuation of session 062. Picked up the 5 in-flight bugs and the
stale creduces. **Net result: a single 4-line fix in
`tcc/ppc-gen.c` closes 4 of the 5 bugs** — including all three
SEGVs.

## TL;DR

| Bug | Symptom | Status |
|---|---|---|
| seed-1536 | tcc SIGSEGV (default-opts) | ✅ FIXED |
| seed-1705 | output-diff (default-opts) | ✅ FIXED |
| seed-8271 | tcc SIGSEGV (builtins+bitfields) | ✅ FIXED |
| seed-8389 | tcc SIGSEGV (builtins+bitfields) | ✅ FIXED |
| seed-8255 | output-diff (builtins+bitfields) | ❌ residual; separate bug class |

Regression suite all clean: fixpoint holds, tests2 111/111, abitest
every test passes (including all `*struct*` and `*union*` cases that
exercise the affected codepath).

## The bug

`tcc/ppc-gen.c:gfunc_call` had a missing `save_reg()` call in the
struct-by-value arg path. When a call's last argument is a struct
or union and earlier args occupy ABI GPR slots that fall under the
struct's word range, the struct-word loads (`lwz r{slot+3}, ...`)
clobbered prior args still living in those registers.

`gfunc_call`'s `VT_LLONG` and `VT_FLOAT` paths already had this
spill before writing target GPRs; the `VT_STRUCT` path didn't.

### Concrete instance

`seed-1536` reduced to:

```c
func_40(((*l_45) = l_44), (*p_24), l_46[1])  /* 3 args: ptr, int, union8 */
```

with `union U2 l_46[1] = {-1L}` initializing only `signed f0:4`. On
BE PPC with MSB-first bitfield packing (commit fc241f5), `f0 = -1`
lands in the top 4 bits of the 8-byte union container, so the high
word reads back as `0xF0000000`.

Pre-fix codegen for the call:

```
lwz   r5, l_44_addr  ; r5 := arg1 value (= &g_27)
stw   r5, 0(r4)       ; *l_45 = l_44 (side effect)
addi  r3, r31, -40    ; r3 := &l_46[1]
mr    r12, r3
lwz   r5, 0(r12)      ; r5 := union high (0xF0000000)  ← clobbers arg1
lwz   r6, 4(r12)      ; r6 := union low  (0)
lwz   r4, 0(r4)       ; r4 := *p_24 = 42 (arg2)
mr    r3, r5          ; r3 := r5 = 0xF0000000          ← BUG (was meant to be arg1)
bl    func_40
```

Inside `func_40` the first parameter `int *p_41` arrives as
`0xF0000000`; dereferencing it segfaults.

### The fix

Spill any vstack entries living in the ABI target slots before
loading struct words, so a prior-arg vstack entry gets moved to a
local memory slot instead of being silently overwritten in-register.

```diff
+ for (w = 0; w < swords; w++) {
+     int slot = gslot + w;
+     if (slot < 8) save_reg(slot);
+ }
  gv(RC_INT);
  addr_gpr = TREG_TO_GPR(vtop->r & VT_VALMASK);
```

After the fix, when pass 2 later processes arg1, `gv()` reloads
the spilled value from the local into the right ABI register. The
final arg-shuffle reads from the correct location.

## Why one fix closes four bugs

The bug surfaces whenever the codegen for a struct/union arg
overlaps an ABI GPR holding a still-live earlier arg's value. Both
SEGVs (1536/8271/8389) and output-diffs (1705) are different
manifestations of the SAME underlying clobber:

- If the clobbered register held a pointer that later gets
  dereferenced → SEGV (1536/8271/8389).
- If the clobbered register held data that later flows into a
  computation → wrong final value, output-diff (1705).

`seed-1705`'s first divergent global (`g_142`) lives in `func_22`'s
body, where `func_40` is called inside an outer assignment
expression — the same call shape as the SEGVs.

## Investigation flow

1. `gdb` on imacg3-built `seed-1536.tcc`: SEGV at `lwz r4, 0(r4)`,
   `r4 = 0xF0000000`. Wild pointer, not a stack-overflow / bad
   frame. Backtrace points at the deepest user function.
2. Identified function bounds via prologue patterns
   (`mflr r0; stw r0, 8(r1); stw r3, 24(r1)`) and `nm` for `_main`.
   Crashing function = `func_40` (last user-fn before `_main`).
3. Located caller's `bl 0x92e8` inside `func_22` body. Disassembly
   showed `mr r3, r5` immediately before the call.
4. Traced r5's history: loaded with arg1's pointer, then clobbered
   by struct-word load.
5. Hypothesized the bug; built a 25-line minimal repro
   (`test_arg_clobber.c` in this dir) that reproduces deterministically.
6. Found the missing `save_reg()` in `gfunc_call`'s struct case by
   comparison with the LL and FLOAT cases that already had it.
7. One-line fix; rebuilt; re-ran all 5 reproducers.

## Stale creduces

The session-062 in-flight reductions on uranium were chasing
"any-signal" predicates. Once my fix landed, the originals stopped
crashing — but the partially-reduced testcases still did, because
creduce had drifted into different bug shapes during reduction
(seed-1536 reduced→147 lines now SIGBUSes at `r3 = 17` instead of
`r3 = 0xF0000000`; that's a separate residual bug, possibly tcc,
possibly creduce-introduced UB). Killed all three creduces — the
originals are fixed and the reductions are no longer pointing at
the same bug.

## Regression suite (post-fix)

| Check | Host | Result |
|---|---|---|
| fixpoint (tcc-self2.o == tcc-self3.o) | imacg3 | ✅ HOLDS |
| tests2 | ibookg38 | ✅ 111/111 |
| abitest (incl. struct/union by-value tests) | ibookg38 | ✅ all PASS |
| demos/v0.2.47-fp-arg-shadow.sh | imacg3 | ✅ All PASS |

The default-opts campaign on imacg3 reached 783/1500 with the SAME
2 FAILs (seed-1536, seed-1705) — both now fixed. No new bugs
surfaced during the session.

## Residual: seed-8255

First divergence at `g_181[i]`:

```
gcc: ...checksum after hashing g_181[i] : 7DF4B4D6
tcc: ...checksum after hashing g_181[i] : 111DC160
```

Different bug class — not closed by the gfunc_call struct fix.
Worth pursuing in a future session, but the bug-hunt arc has
landed its biggest payoff for now.

## Files

- `seed-1536-analysis.md` — original detailed analysis (kept for
  the disassembly walkthrough).
- `test_arg_clobber.c` — 25-line minimal repro of the bug.
- `seed-1705-repro/` — output-diff predicate + creduce setup
  (creduce killed; predicate kept for reference).

## Next session

`HANDOFF.md` in this dir.
