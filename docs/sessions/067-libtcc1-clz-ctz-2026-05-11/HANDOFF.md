# Handoff — end of session 067 (2026-05-11)

## TL;DR

Closed open item #4 from session 066's HANDOFF: tcc's libtcc1.a
`__builtin_clz` / `__builtin_ctz` (and the long / long-long variants)
now match gcc-4.0 on PPC at the UB input `x == 0`.

* **HEAD at session start:** `5a80c8f` (session 066 roadmap update).
* **HEAD at session end:** `<docs commit, see below>`.
* **v0.2.49-g3 tag:** created locally on `e6d435a` (the
  `tcc/lib/builtin.c` source fix commit). **Not pushed** — per
  session convention, awaits explicit user sign-off.

**Fix:** four `if (!x) return XX;` short-circuits in
`tcc/lib/builtin.c`'s `BUILTIN(clz)` / `BUILTIN(clzll)` /
`BUILTIN(ctz)` / `BUILTIN(ctzll)` function bodies. `clzl` / `ctzl`
inherit through `__attribute__((alias))`. `clrsb` keeps the bare
`CLZI` / `CLZL` macros (`clrsb(0)` is well-defined and the macros
already give the right value).

**Regression suite:** all green.
* Bootstrap fixpoint: `tcc-self2.o == tcc-self3.o`.
* tests2: 111/111.
* abitest cc + tcc: all `success`.
* Demos sampled: `v0.2.45..v0.2.48`, `v0.2.27-jit-heisenbug`,
  `v0.2.42-python`, `s025-self-link` — all PASS.

**New demo:** `demos/v0.2.49-clz-ctz-ub.{c,sh}` runs 18 sub-tests
(6 UB inputs, 8 defined-behavior smoke, 4 clrsb cases). All PASS
on imacg3.

**ibookg38 update:** written off as bad hardware. Memory files
updated (`host_ibookg38.md`, `MEMORY.md`); future sessions should
treat the fleet as `uranium + imacg3 + ibookg37`.

**csmith binary location captured:** uranium is the only fleet
host with a csmith binary — `/opt/homebrew/bin/csmith` (Homebrew
2.3.0). `host_ibookg37.md` updated to reflect this.

## What landed

* `tcc/lib/builtin.c` — `e6d435a`. The four `if (!x) return XX;`
  short-circuits plus a short comment above each pair explaining why.

* `demos/v0.2.49-clz-ctz-ub.c` / `.sh` — the runnable demo.

* `demos/README.md` — table row for v0.2.49.

* `docs/roadmap.md` — version line bumped to v0.2.49-g3, new row
  at the top of the reverse-chrono table section.

* `docs/sessions/067-libtcc1-clz-ctz-2026-05-11/`:
  * `README.md` — narrative.
  * `HANDOFF.md` — this file.

Memory updates (uranium-side, not in git):
* `host_ibookg38.md` — marked **WRITTEN OFF**.
* `host_ibookg37.md` — csmith binary note narrowed to uranium-only.
* `MEMORY.md` — index lines re-flowed to match.

## Build-infra gotcha captured

imacg3's `/usr/bin/make` is **GNU Make 3.80**, which is **too old**
to understand `$(or ...)` (added in make 3.81). `tcc/lib/Makefile`
uses `$(or $(CROSS_TARGET),$(NATIVE_TARGET),unknown)` — under 3.80
this evaluates to empty, `T` ends up empty, `OBJ-libtcc1` ends up
empty, and `make libtcc1.a` builds an **empty** archive (8 bytes,
just `!<arch>\n`) with **no error**. The first link against the
empty archive then dies with `dyld: Symbol not found:
___builtin_clz`.

The right binary on imacg3 is `/opt/make-4.3/bin/make` (installed
via `tiger.sh make-4.3`). Same as `host_ibookg38.md` /
`host_ibookg37.md` already document. Worth a heads-up because the
failure mode is silent.

## Status of session 066's open items

| # | Item | Status |
|---|---|---|
| 1 | Push v0.2.48-g3 tag | already on origin (was pushed in cleanup; HANDOFF was stale) |
| 2 | v0.2.48 demo | landed in 066 |
| 3 | Sibling r11 watch | unchanged (no surface yet) |
| 4 | **libtcc1.a clz/ctz to match gcc-PPC** | **landed (this session)** |
| 5 | csmith building on a PPC host | unchanged (still uranium-only) |
| 6 | ibookg38 — back online or written off | **written off** (bad hardware) |
| 7 | OSO STAB / self-link / AltiVec / bcheck | unchanged |

## Open work for next session

### 1. Push v0.2.49-g3 tag (user confirmation required)

Tag exists locally on `e6d435a` (the source fix commit, not the
docs commit). Pushing it makes the release public:

```sh
git push origin v0.2.49-g3
```

Per session convention, **do not push without explicit user
sign-off**.

### 2. (carried) csmith building on at least one PPC host

Uranium remains the only host with a csmith binary. Building csmith
on Tiger PPC is non-trivial (C++, libstdc++ on PPC32). If we want
independent corpus generation, this is a half-day to one-day lift
(probably gcc-4.9 from `/opt`, plus working around csmith's
configure assumptions). Not blocking — `scp` from uranium is fine.

### 3. (carried) OSO STAB / self-link diagnostics / AltiVec / bcheck

Unchanged. Each is its own multi-session arc; pick one with the user.

### 4. (optional) Trim or retire `builtin_compat.h`

The clz/ctz wrappers in
`docs/sessions/062-bswap-shim-builtins-2026-05-10/builtin_compat.h`
are no longer required for csmith differential testing (tcc and
gcc now agree natively on the UB cases). The bswap32/bswap64 and
ia32_crc32qi declarations in the same header are still needed —
those are a separate ABI-prototype issue, not a UB-value issue.
Possible cleanup pass: strip the clz/ctz wrappers, leave the
prototypes. Small docs-only change; might be worth it for clarity.

### 5. (optional) Confirm with a real csmith `--builtins` campaign

Run a few hundred `--builtins` seeds **without** `-include
builtin_compat.h` for the clz/ctz part and confirm tcc and gcc
agree. The setup is already on imacg3 (`/Users/macuser/tmp/csmith-
builtins-8020/`); a one-line edit to `csmith_campaign.sh` would
strip the `-include`. Not blocking — the direct-value test in the
demo is already the authoritative confirmation.

## How to pick up

### Reproduce the fix's effect

```sh
ssh imacg3 '
cat > /tmp/clz_check.c <<EOF
#include <stdio.h>
int main(void) {
    printf("clz(0)=%d ctz(0)=%d clzll(0)=%d ctzll(0)=%d\n",
           __builtin_clz(0u), __builtin_ctz(0u),
           __builtin_clzll(0ULL), __builtin_ctzll(0ULL));
    return 0;
}
EOF
/usr/bin/gcc-4.0 -O0 -w /tmp/clz_check.c -o /tmp/g; /tmp/g
/Users/macuser/tcc-darwin8-ppc/tcc/tcc -B/Users/macuser/tcc-darwin8-ppc/tcc \
    -I/Users/macuser/tcc-darwin8-ppc/tcc/include /tmp/clz_check.c -o /tmp/t; /tmp/t
'
```

Both lines should now print
`clz(0)=32 ctz(0)=-1 clzll(0)=64 ctzll(0)=31`.

### Rebuild libtcc1.a on imacg3 if needed

```sh
ssh imacg3 '
cd /Users/macuser/tcc-darwin8-ppc/tcc &&
rm -f libtcc1.a lib/*.o &&
/opt/make-4.3/bin/make libtcc1.a
'
```

Do **not** use `/usr/bin/make` — Make 3.80 silently builds an empty
archive. See "Build-infra gotcha captured" above.

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

This was a small, targeted fix that retires the clz/ctz half of
`builtin_compat.h`. The source-level intent now matches what we
want during differential testing — gcc-PPC compatibility — so
nothing has to be conditionally included for that purpose. The
bswap / ia32_crc parts of the shim remain orthogonal (and live
for a different reason: prototype-declaration ABI).

The Make 3.80 vs 4.3 gotcha is the only real surprise — the silent
empty-archive failure mode is worth knowing about for anyone who
rebuilds libtcc1.a on imacg3 in the future. The two host-memory
files (`host_ibookg38.md`, `host_ibookg37.md`) already documented
the right incantation; the surprise was that the wrong incantation
fails silently rather than loudly.

Next session: [docs/sessions/067-libtcc1-clz-ctz-2026-05-11/HANDOFF.md](docs/sessions/067-libtcc1-clz-ctz-2026-05-11/HANDOFF.md)
