# Session 066 — seed-732 r12-clobber fix (2026-05-11)

Picked up the lone open bug from session 065's validation campaign:
**seed-732** (csmith default-opts), output-diff at the first read of
`g_359.f0`. Session 065's HANDOFF flagged it as the headline next-
session task, with a suspect-shape of "union U0 passed by value as
a middle argument".

That hypothesis turned out to be wrong (the call-setup for the union
arg is fine). The actual bug is a **register clobber in tcc's PPC
codegen for global symbol loads**: `r12` is in tcc's int-register
allocator AND is used as a hardcoded scratch by every `lis r12,
ha(sym); lwz/lbz rD, lo(sym)(r12)` two-instruction symbol load
sequence. Under high register pressure, tcc allocates `r12` to hold
a live vstack value, then the next symbol-load `lis r12, ha`
silently destroys that value.

In seed-732 this manifested as `(*l_505) |= g_26.f0` — the load of
the lhs `*l_505` (an int8_t from `&g_359.f0`) landed in `r12`; the
load of the rhs `g_26.f0` (another int8_t global) clobbered `r12`
with the high half of `g_26`'s address; the resulting OR was
`0x20000 | 7 = 0x20007`, stored as a byte = `0x07`. So `g_359.f0`
ended up `0x07` instead of `0xA7` (= `0xA4 | 7`).

## TL;DR

| Step | Result |
|---|---|
| Confirmed seed-732 reproduces on imacg3 + post-063 tcc | gcc=`A7`, tcc=`07` at g_359.f0 |
| Hand-instrumented seed-732 to pinpoint the failing site | line 467: `(*l_505) |= g_26.f0` inside func_8 |
| Added sentinel `volatile int __sentinel = 0x7320/0x7321` to bracket the statement and disassembled the resulting tcc-produced .tcc with `otool -tV` | r12-clobber visible in 9 instructions |
| Fix: removed r12 from `reg_classes[]` (slot 9 → `0` instead of `RC_INT \| RC_R(9)`) | r12 is now reserved as pure scratch |
| Regression suite | fixpoint HOLDS, tests2 111/111, abitest 24/24, demos/v0.2.47 All PASS |
| seed-732 closes | gcc/tcc match `76F5DB56` byte-identical |
| 1000-seed default-opts re-sweep (ibookg37) | **873 PASS / 0 FAIL / 127 SKIP** (1 better than session 065) |
| v0.2.48-g3 tag | created locally on commit `1f32055`; push pending user sign-off |

## Investigation flow

### 1. Reproduced bug on imacg3

```
$ /usr/bin/gcc-4.0 -O0 -w -I/Users/macuser/tmp/csmith-runtime \
    seed-732.c -o seed-732.gcc
$ tcc -B... -I/Users/macuser/tcc-darwin8-ppc/tcc/include \
    -I/Users/macuser/tmp/csmith-runtime seed-732.c -o seed-732.tcc
$ ./seed-732.gcc 1 > g.out
$ ./seed-732.tcc 1 > t.out
$ diff g.out t.out | head
77,82c77,82
< ...checksum after hashing g_359.f0 : 3B71BCAC
...
> ...checksum after hashing g_359.f0 : F3115DB3
```

(Same divergence point as session 065's HANDOFF.)

### 2. Narrowed bug to one statement via instrumented printfs

Added `fprintf(stderr, ...)` traces at `func_1`/`func_8`
entry+exit, plus around the `(*l_528) = (!(safe_div_...))`
statement at line 467. Output:

```
FUNC1 ENTER         g_359.f0=-92
FUNC8 ENTER         g_359.f0=-92  p_11.f0=-26    (correct: l_53 was {0xE6})
FUNC8 BEFORE-l_505|= g_359.f0=-92 *l_505=-92 g_26.f0=7
FUNC8 AFTER-l_505|=  g_359.f0=7   *l_505=7   g_26.f0=7  p_9=7
FUNC1 EXIT          g_359.f0=7
MAIN BEFORE-CRC     g_359.f0=7
```

— so the bug is precisely the side-effect of the giant statement at
line 467, specifically the embedded `(*l_505) |= g_26.f0`. Both gcc
and tcc agree at FUNC8 ENTER (the call setup is fine — the original
"middle-arg union" hypothesis was wrong).

### 3. Disassembled the failing statement

Added sentinel `volatile int __sentinel = 0` to func_8 with
`__sentinel = 0x7320` before the statement and `__sentinel = 0x7321`
after. These compile to `li r3, 0x7320 / stw r3, off(r31)` (= bytes
`38607320 ...`), easy to find with grep. The block between sentinels
is 315 instructions, but the offending sequence is 9 contiguous:

```
0000baa4    lbz   r12, 0x0(r12)        # r12 = *l_505 byte (= 0xA4)
0000baa8    extsb r12, r12             # sign-extend → r12 = 0xFFFFFFA4
0000baac    stw   r3, 0xff50(r31)      # spill another vstack value
0000bab0    lis   r12, 0x2             # *** CLOBBERS r12 ***
0000bab4    lbz   r3, 0xa008(r12)      # r3 = g_26.f0 = 7
0000bab8    extsb r3, r3
0000babc    or    r12, r12, r3         # r12 = 0x20000 | 7 = 0x20007
0000bac0    lwz   r3, 0xff58(r31)      # reload l_505 address
0000bac4    stb   r12, 0x0(r3)         # *l_505 = byte(r12) = 0x07
```

The `lis r12, 0x2` at 0xbab0 is the high-half of g_26's literal
address. It's emitted by tcc's `load(r, sv)` for the
VT_CONST|VT_SYM|VT_LVAL path (ppc-gen.c around line 893+, the
`addr_gpr = 12` branch when `want_load=true`).

### 4. Root cause

`reg_classes[9]` is `RC_INT | RC_R(9)` → r12 is allocatable by tcc.
The codegen for global symbol loads (and several other paths:
`ppc_load_fp_const`, `load()` literal-address case, `store()`
literal-address case, `gfunc_call` struct-arg base) hardcodes r12 as
scratch without going through the allocator. So a vstack value
placed in r12 by an earlier `get_reg(RC_INT)` is silently destroyed
by the next `lis r12, ha`.

This is a high-register-pressure bug. Seed-732 packs enough live
pointers/values into the expression that r3..r11 are all in use,
forcing the allocator to pick r12 for `*l_505`'s loaded byte. Then
the load of `g_26.f0` clobbers it.

### 5. Fix

Single-line change in `tcc/ppc-gen.c`:

```diff
-    /* 9: r12 (scratch) */
-    RC_INT | RC_R(9),
+    /* 9: r12 — NOT allocatable. Hardcoded as scratch by many
+     * codegen paths ... */
+    0,
```

After this, tcc's int allocator has 9 slots (r3..r11) instead of 10.
r12 is true scratch.

## Files

- `repro_v1.c` ... `repro_v8.c` — progressively-larger hand-built
  reproducers. None triggered the bug — even a 9-pointer
  assignment chain (`repro_v8.c`) doesn't push tcc's allocator into
  r12, because tcc picks the lowest-numbered free slot and 9
  vstack values fit in r3..r11. seed-732's bug-triggering shape
  needs an even tighter set of simultaneously-live values plus a
  specific allocation order that csmith produced but I couldn't
  reverse-engineer by hand.
- `seed-732-sentinel-block.dis` — the 315-instruction disassembly
  between the sentinels. The r12-clobber is at lines 23-29.
- `creduce/` — creduce setup with a test.sh that ssh's to imacg3.
  Stopped at 73 lines / 39KB when the build started racing the
  reducer. `test.c` is the partial reduction at kill-time
  (~13% smaller than `test.c.orig`); `test.sh` is the predicate.

## Status of carry-forward items from session 065

| # | Item | Status |
|---|---|---|
| 1 | seed-732 fix | landed (this session) |
| 2 | v0.2.48-g3 tag | pending: tag after regression confirms |
| 3 | v0.2.48 demo | pending: a reduced repro + run script |
| 4 | libtcc1.a clz/ctz | unchanged, future |
| 5 | csmith on PPC host | unchanged, future |
| 6 | ibookg38 revival | unchanged, future |
| 7 | OSO STAB / self-link / AltiVec / bcheck | unchanged |

## Next session

`HANDOFF.md` once regression + re-sweep are clean.
