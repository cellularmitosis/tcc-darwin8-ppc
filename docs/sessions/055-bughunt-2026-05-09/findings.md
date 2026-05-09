# Findings — session 055

## BE bitfield ABI was non-standard

Pre-v0.2.45, on PPC BE, tcc laid out bitfields **LSB-first** within
the storage container — meaning the byte at the lowest memory
address held the LSB of the bitfield value. The SysV/AIX PowerPC ABI
(and gcc-4.0) uses **MSB-first**: the byte at the lowest memory
address holds the MSB of the bitfield value.

The v0.2.14 fix made tcc internally consistent (forced byte-wise
load/store/init agreeing on the LSB-first layout), but it was
ABI-incompatible. The bug only surfaces when:

1. The bitfield's storage is initialized via a sibling union member
   (e.g. `union U {uint32_t f0; unsigned f1:14;}; u.f0 = ...; read u.f1;`)
2. The bitfield is read via a pointer cast
   (e.g. `((union U *)&plain_int)->f1`)
3. The bitfield-containing struct is shared with a gcc-built object,
   or written to disk and read by a standard-ABI consumer

In a tcc-only program where bitfields are written and read only via
their bitfield members, both write and read use the same (non-standard)
byte layout, so it round-trips correctly. Almost all small programs
fall into this category. csmith found the bug because it generates
unions with sibling uint and bitfield members.

The fix lives in tccgen.c's three byte-wise bitfield paths
(`load_packed_bf`, `store_packed_bf`, `init_putv`'s VT_BITFIELD
branch). Struct-layout (`struct_decl`) is unchanged: bit_pos is a
running counter from 0 as before. The MSB-first interpretation is
applied at access time on PPC BE.

## csmith is high-yield

200 csmith seeds, default options + `--no-volatiles --no-packed-struct`,
single bug surfaced (the BE bitfield issue), reduced from a 983-line
random program to a 16-line repro by running with `--print_hash_value=1`
to see which global checksum diverged first.

Pre-v0.2.45 fail rate: 30/200 = 15%. All 30 fails were the same root
bug (BE bitfield); after fixing, the campaign is clean.

`csmith` and `creduce` are both in homebrew on uranium; csmith.h
(runtime header set) lives at
`/opt/homebrew/include/csmith-2.3.0/`. The campaign script
(`csmith_campaign.sh`) ships those headers to imacg3 and runs both
gcc-4.0 and tcc on each seed, comparing checksum output.

Tiger doesn't ship `seq` or `timeout` — the campaign script uses a
while-counter loop and a `perl -e 'alarm; exec'` for timeout.

## False alarms to filter

Some csmith programs are pathologically slow (deep recursion, long
loops). Both gcc-built and tcc-built take ~minutes. The campaign
classifies these as SKIP (gcc-timeout) so they don't show as bugs.

Seed 20 in our 1-200 batch had this property — runs effectively
forever even though csmith claims its programs are guaranteed to
terminate.

## LL `addend+4` paranoid-fix is dead code

The session-054 HANDOFF flagged a possible LL store path that emits
`stw hi at addend; stw lo at addend+4` where `addend+4` could
overflow signed-16 when addend ∈ [0x7ffc, 0x7fff]. On inspection,
this path is never taken: the framework splits VT_LLONG stores into
two VT_INT calls, each going through `ppc_adjust_extra_off` with
its own (correctly-bumped) offset. Repro at
`docs/sessions/055-bughunt-2026-05-09/repro_ll_addend4.c` confirms
the path is dead (the disassembly shows two separate PIC
indirections with their own `addis r12,r12,0x1` anchor bumps for
the high half).

The dead VT_LLONG case in the PIC store path could be deleted as
a follow-up cleanup, but it's harmless as-is and the analysis is
in this file in case someone re-reads the HANDOFF and wonders why
the "fix" wasn't applied.
