# Handoff — end of session 084 (2026-05-15)

## TL;DR

Session 083's HANDOFF #6 closed. **The v0.2.62 JBSR fix benefits
`-run` mode transparently** — `macho_translate_relocs` is shared
across OBJ/EXE/RUN and the JBSR arm doesn't gate on `output_type`;
the emitted `R_PPC_REL24` is handled by `-run`'s existing GOT/PLT
path identical to ordinary extern calls. **No naturally-occurring
JBSR exists in the current `-run` test surface**: libtcc1's four
PPC objects contain zero JBSR relocs, tcc's codegen never emits
JBSR, gcc-4.0 emits BR24 (not JBSR) for ordinary calls, and `-run`
doesn't auto-link `crt1.o`. The fix's reach was demonstrated by
forcing `crt1.o` into the `-run` reader surface (`tcc -vv -run
/usr/lib/crt1.o hello.c`): pre-fix shows the same 4 "non-extern
stub-slot resolve failed" lines from session 083; post-fix they're
gone. Investigation-only — no source change, no commit, no tag.

* **HEAD at session start:** `7a55377` (end of session 083).
* **HEAD at session end:** `7a55377` (unchanged).
* **Source changes:** none on uranium. On imacg3, a temporary
  in-place revert of the JBSR arm was applied, used to capture the
  pre-fix diagnostic, then restored from a local backup
  (`/tmp/ppc-macho.c.v062`). md5-verified to match uranium HEAD
  after restore.

## What landed

### Source

None.

### Docs

* `docs/sessions/084-tcc-run-jbsr-probe-2026-05-15/README.md` —
  full investigation writeup with codepath analysis, libtcc1
  artifact probe, pre/post-fix diagnostic comparison on imacg3,
  and "findings worth remembering" capturing the
  default-shared-across-output-types property of the reader.
* This `HANDOFF.md`.

### Memory updates

None.

## Status of session 083's open items

| # | Item | Status |
|---|---|---|
| (083 #1, carried 067 #3) | Sibling r11 watch | unchanged |
| (083 #2, carried) | AltiVec intrinsics | unchanged |
| (083 #3, carried) | bcheck.c port | unchanged |
| (083 #4, optional, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` | unchanged |
| (083 #5, optional, carried) | `rsync` exclude-list polish | unchanged |
| (083 #6, new from 083) | `tcc -run` mode: does it also need this fix? | **closed this session** — yes (transparent via shared codepath), no demo needed |

## Open work for next session

### 1. Sibling r11 watch (carried, 067 #3)

PPC sibling-register concern to v0.2.48's r12 fix. csmith hasn't
surfaced a repro yet. If a future seed does, the symmetric fix is
`reg_classes[8] = 0`.

### 2. AltiVec intrinsics (carried)

PowerPC SIMD. Would unlock vector-heavy programs (libpng's filter
loops, openssl's AES-NI fallback paths on PPC, gimp's pixel ops).
New parser front-end work + codegen. Multi-session lift.

### 3. `bcheck.c` port (carried)

PowerPC port of tcc's bounds-check runtime. Currently stubbed out
via `bcheck-ppc.c`. Multi-session lift.

### 4. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Fourteen+ confirmed rebuild cycles now.

### 5. (optional, low priority) `rsync` exclude-list polish

Sidestepped again this session — investigation-only, no uranium→
imacg3 sync was needed. The toggle was a one-line in-place perl
edit on imacg3 with a same-host backup.

## How to pick up

If a future session encounters a JBSR-containing `.o` under `-run`
in the wild (unusual but possible — e.g. linking a user's
hand-rolled CRT or embedded-firmware-style PPC assembly), the
diagnostic to confirm v0.2.62 is doing the right thing is:

```sh
ssh imacg3
./tcc/tcc -vv -run <jbsr-bearing.o> ... 2>&1 | grep "tcc: reader:"
```

If `non-extern stub-slot resolve failed` lines appear, the JBSR
arm has hit a target section tcc didn't translate to a known stub
kind — investigate the source `.o`'s `__symbol_stub1` reserved1/2
fields and the indirect-symtab range with `otool -lv` + `otool -Iv`.

## Subagent log

None this session — investigation was direct: read `ppc-macho.c`
lines 7241–7414 to confirm the codepath; `otool -rv` on libtcc1's
PPC objects on imacg3; one-line perl revert + rebuild + run to
capture pre-fix diagnostic; restore + rebuild to verify
disappearance.

## Closing notes

A short investigation session that resolves an open question
cleanly without code change. The interesting finding is
structural: the reader's default-shared-across-output-types
property means future reader-side fixes don't need per-mode
verification unless they explicitly gate (like the scattered
SECTDIFF arm does). The session also confirmed by inspection that
naturally-occurring JBSR is rare on Tiger PPC outside Apple's CRT
objects — which explains why v0.2.61's `-vv` diagnostic stayed
clean on every link until hello-world+crt1.o exposed it.

Next session: [docs/sessions/084-tcc-run-jbsr-probe-2026-05-15/HANDOFF.md](docs/sessions/084-tcc-run-jbsr-probe-2026-05-15/HANDOFF.md)
