# Handoff — end of session 083 (2026-05-15)

## TL;DR

**v0.2.62-g3 fixes `PPC_RELOC_JBSR` non-extern resolution** — the
exact bug that v0.2.61's `-vv` reader diagnostic surfaced on its
first real-world link (the four "non-extern stub-slot resolve
failed" lines on `crt1.o`). One-block fix in `macho_translate_relocs`:
split BR24 from JBSR. BR24 keeps reading the instruction's
displacement (correct for direct branches); JBSR now reads the
**PAIR record's full 32-bit `r_address`** (the long-branch
trampoline convention). Post-fix, the four crt1.o calls (_exit,
_main, _atexit, ___keymgr_dwarf2_register_sections) resolve through
the indirect symtab to their externs and route via tcc's existing
stub-allocation machinery.

The runtime worked pre-fix by accident: the local thunks that
JBSR's BR24 was branching to had their own HI16/LO16 relocs
against `__symbol_stub1` that tcc resolved correctly through the
indirect symtab, and tcc's writer synthesized a parallel
`__picsymbolstub1` entry for those externs that the thunk's
`mtctr/bctr` happened to reach. Fragile-but-correct. Post-fix the
path is explicit and traceable.

* **HEAD at session start:** `f55983b` (end of session 082, v0.2.61-g3).
* **HEAD at session end:** v0.2.62-g3, tag created.
* **Source changes:** one file
  ([`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c)) — 19 lines added
  (split the `BR24 || JBSR` arm into two, ~7 LOC comment).
* **No new demo:** v0.2.61's demo + a `-vv` hello-world link is the
  observable change; the assertions in v0.2.61's demo are loose
  enough that the dropped 4 lines don't fail it.
* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint
  holds; ten v0.2.25..v0.2.61 demos all PASS.**

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) at line ~7263 — the
  non-extern reloc-translation arm. Before:

  ```c
  } else if (r_type == PPC_RELOC_BR24 || r_type == PPC_RELOC_JBSR) {
      disp = insn & 0x03fffffc;
      if (disp & 0x02000000) disp |= 0xfc000000;
      target_value = sec->addr + r_address + disp;
  }
  ```

  After:

  ```c
  } else if (r_type == PPC_RELOC_BR24) {
      disp = insn & 0x03fffffc;
      if (disp & 0x02000000) disp |= 0xfc000000;
      target_value = sec->addr + r_address + disp;
  } else if (r_type == PPC_RELOC_JBSR) {
      /* JBSR encodes the real far-call target in the PAIR's
       * r_address field (full 32-bit, not just the low 16
       * like HA16/LO16/HI16). The BR24 immediate in the
       * instruction itself points at a local thunk used as
       * a 24-bit-reachable trampoline ... */
      pair_w0 = (k + 1 < sec->nreloc) ? mach_get32(r + 8) : 0;
      target_value = pair_w0;
  }
  ```

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped ("v0.2.61-g3"
  → "v0.2.62-g3", "Thirty-nine" → "Forty"); v0.2.62-g3 row prepended.
* `docs/sessions/083-crt1-stub-slot-investigation-2026-05-15/README.md`
  — the full investigation writeup with otool dumps, target VA
  arithmetic, raw thunk instruction bytes, and the why-hello-world-
  worked-anyway diagnosis.
* This `HANDOFF.md`.

### Memory updates

None. Reusable findings are in the
[session README's "Findings worth remembering"](README.md#findings-worth-remembering)
section — JBSR vs. BR24, Mach-O PAIR record's 32-bit r_address vs.
16-bit-truncated low half for HA16/LO16/HI16, and the validation
that v0.2.61's diagnostic paid for itself on day one.

## Status of session 082's open items

| # | Item | Status |
|---|---|---|
| (082 #1, carried 067 #3) | Sibling r11 watch | unchanged |
| (082 #2, carried) | AltiVec intrinsics | unchanged |
| (082 #3, carried) | bcheck.c port | unchanged |
| (082 #4, new from 082) | Investigate 4 crt1.o non-extern stub-slot resolve fails | **landed this session (v0.2.62)** |
| (082 #5, new from 082) | Source-level look at crt1's internal-branch oddity | **landed this session (v0.2.62)** — the `otool -rv` + raw-bytes dump in the session README covers it |
| (082 #6, carried 069) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (twelve+ rebuild cycles since 069) |
| (082 #7, carried) | `rsync` exclude-list polish | unchanged (sidestepped again this session — single-file `scp tcc/ppc-macho.c` was enough for a one-block fix) |

## Open work for next session

### 1. Sibling r11 watch (carried, 067 #3)

PPC sibling-register concern to v0.2.48's r12 fix. csmith hasn't
surfaced a repro yet. If a future seed does, the symmetric fix is
`reg_classes[8] = 0`. Just keep an eye on it.

### 2. AltiVec intrinsics (carried)

PowerPC SIMD. Would unlock vector-heavy programs (libpng's filter
loops, openssl's AES-NI fallback paths on PPC, gimp's pixel ops).
New parser front-end work + codegen. Multi-session lift.

### 3. `bcheck.c` port (carried)

PowerPC port of tcc's bounds-check runtime. Currently stubbed out
via `bcheck-ppc.c` (no-op shims). Real port would need to intercept
allocations, maintain a redzone map, and emit per-load/store
bounds-check calls. Multi-session lift.

### 4. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after twelve+ confirmed
rebuild cycles since 069 (most recent: this session, post-fix).

### 5. (optional, low priority) `rsync` exclude-list polish

`tcc/c2str.exe` (built on uranium for testing, arm64) is not in
the current rsync exclude list. Sidestepped this session and the
prior three by single-file `scp tcc/ppc-macho.c`. If sync becomes
a recurring pain point in a session that touches many files, a
`scripts/sync-to-imacg3.sh` wrapper with the canonical exclude
list is the right move.

### 6. Eight `tcc -run` mode: does it also need this fix? (new)

The v0.2.61 demo and verification used the EXE output path
(`tcc foo.o bar.o -o prog`). `macho_translate_relocs` is shared
between EXE/OBJ/RUN output types in `macho_load_object_file`, so
the JBSR fix should benefit `-run` mode too. But: `tcc -run` mode
doesn't auto-link `crt1.o` (it runs in-process), so the specific
hello-world repro doesn't apply. **If** a JBSR-routed call appears
in some loaded `.a` archive member or in libtcc1.a's PPC helpers,
`-run` mode benefits transparently. Worth keeping in mind, but
not a blocker — current `-run` tests all pass, so no JBSR appears
in the in-memory test surface.

## How to pick up

### Verify v0.2.62 end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
echo 'int main(){return 0;}' > /tmp/x.c
./tcc/tcc -vv /tmp/x.c -o /tmp/x 2>&1 | grep "tcc: reader:" | wc -l
# Expect: 7 (not 11 like pre-v0.2.62)
./tcc/tcc -vv /tmp/x.c -o /tmp/x 2>&1 | grep "stub-slot resolve failed"
# Expect: no output (the four pre-fix lines are gone)
```

### Re-run the full regression

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    bash scripts/run-tests2.sh && \
    cd tcc/tests && /opt/make-4.3/bin/make abitest && cd ../.. && \
    FIXPOINT=1 bash scripts/bootstrap-tcc-self.sh'
```

Expected: `Total: 111  Pass: 111  Fail: 0`, 48 abitest successes,
`FIXPOINT HOLDS`.

### Re-run the ten v0.2.25..v0.2.61 demos

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    for d in v0.2.25-dylib v0.2.26-link-dylib v0.2.29-dylib-slide \
             v0.2.31-libtcc-slide v0.2.40-sed v0.2.41-gzip v0.2.42-python \
             v0.2.59-dylib-self-link-diagnostics v0.2.60-post-write-linter \
             v0.2.61-vv-reader-diagnostic; do
        printf "%-45s " "$d"; bash demos/$d.sh 2>&1 | tail -1
    done'
```

The v0.2.61 demo's "surfaces N reader lines" should report a
slightly lower N (was 17 pre-fix, 13 post-fix).

### Tag + push status

v0.2.62-g3 is tagged locally. Push authority granted per
`memory/feedback_push_authority.md`:

```sh
git push origin main
git push origin v0.2.62-g3
```

## Subagent log

None this session — investigation was direct (otool dumps,
ppc-macho.c reads, raw byte inspection via perl on imacg3). The
target was already well-scoped by session 082's HANDOFF and the
single-line code path was easy to locate via `grep "JBSR\|BR24"`.

## Closing notes

This session is the satisfying counterpart to session 082. v0.2.61
landed a diagnostic specifically to surface silent reader decisions;
on its very first real-world link, the diagnostic surfaced four
mystery lines; one session later it's a real bug fix. The
diagnostic's value proposition was "future bugs shaped like the
v0.2.40 sed-bug or the v0.2.44 SECTDIFF shadow-section bug now
give a `tcc: reader: ...` line under `-vv`" — and it caught a
sleeper bug on day one.

The fix itself is small (one split arm in the dispatch, ~20 lines
including a comment that explains the JBSR convention). The
interesting work was the **investigation** — distinguishing
target_value=0x330 (the local thunk) from target_value=0x3b0 (the
real stub) required understanding both Mach-O's JBSR convention and
PPC's BR24 displacement encoding. The session README captures the
arithmetic at every step (decoded instructions, indirect-table
indices, the side-effect-aligned reason hello-world worked pre-fix)
so a future-Claude can follow the trail without re-running otool.

Next session: [docs/sessions/083-crt1-stub-slot-investigation-2026-05-15/HANDOFF.md](docs/sessions/083-crt1-stub-slot-investigation-2026-05-15/HANDOFF.md)
