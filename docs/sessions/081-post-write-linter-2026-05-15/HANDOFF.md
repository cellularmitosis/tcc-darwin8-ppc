# Handoff — end of session 081 (2026-05-15)

## TL;DR

**v0.2.60-g3 closes the emission-side complement to the
v0.2.50 / v0.2.59 pre-write blocks.** A single new ~470-line helper
in [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) re-stats every
just-emitted Mach-O exe / dylib, reads it back through a fresh
`open` + `read` path that doesn't share state with the writer, and
verifies high-level shape invariants the file alone has to satisfy
(Mach header magic / cputype / filetype, `header.sizeofcmds` ==
sum of per-command cmdsizes, `header.ncmds` == walked count, each
LC_SEGMENT's per-section file / vm bounds, LC_SYMTAB + LC_DYSYMTAB
cmdsize + partition arithmetic + side-table reachability, LC_LOAD_DYLIB
/ LC_LOAD_DYLINKER / LC_ID_DYLIB / LC_UNIXTHREAD counts vs writer-
derived expected values). The helper is called from both writers
after `chmod(filename, 0755)` and before `ret = 0`. Catches
`obuf` corruption, byteorder regressions, cmdsize literal/actual
drift, truncated `fwrite`s, missing/duplicated load commands —
exactly the bug class the v0.2.57 strip work tripped on.

* **HEAD at session start:** `7776ce5` (end of session 080, v0.2.59-g3).
* **HEAD at session end:** v0.2.60-g3, tag pushed.
* **Source changes:** one file
  ([`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c)).
* **Demo added:**
  [`demos/v0.2.60-post-write-linter.{c,sh}`](../../../demos/).
* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint
  holds; eight existing dylib demos all PASS.**

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — new `pwl_get32be`
  helper + `macho_post_write_lint` function inserted between the
  previous `emit_stab_nlist` block and the `/* The big one. */`
  line preceding `macho_output_exe`. Plus two call sites: one in
  `macho_output_exe` right after `chmod(filename, 0755)` (passing
  `is_dylib=0`), one in the same position in `macho_output_dylib`
  (passing `is_dylib=1`). Each call site computes
  `has_externs = (nstubs > 0 || n_nlptrs > 0)` from local writer
  state and passes the existing `n_extra_dylibs` counter. ~470
  lines total — about a third of which is comment cells calling
  out the bug classes each check covers.

### Files added

* [`demos/v0.2.60-post-write-linter.c`](../../../demos/v0.2.60-post-write-linter.c)
  — small EXE exercising rodata + data + bss + a libSystem stub
  (`printf`).
* [`demos/v0.2.60-post-write-linter.sh`](../../../demos/v0.2.60-post-write-linter.sh)
  — builds the EXE (exercises `macho_output_exe` post-write path);
  builds an inline dylib `sum3` + a dlopen driver (exercises
  `macho_output_dylib` post-write path); runs both. Expected last
  line: `PASS: exe + dylib both built and ran; post-write linter
  silent on both paths`.

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped
  ("v0.2.59-g3" → "v0.2.60-g3", "Thirty-seven" → "Thirty-eight");
  v0.2.60-g3 row prepended to the table.
* [`demos/README.md`](../../../demos/README.md) — v0.2.60 row
  added above v0.2.59.
* `docs/sessions/081-post-write-linter-2026-05-15/README.md`
* This `HANDOFF.md`.

### Memory updates

None. The three reusable findings (`__DWARF` segments need a
`vmsize == 0` exemption in any post-write segment check; `set -e`
plus an intentionally-failing tcc invocation aborts the restore step
in test loops; restoring source isn't enough — you have to rebuild
the binary too) are captured in
[the session README's Findings section](README.md#findings-worth-remembering).

## Deviation from session 080's handoff

The 080 handoff sketched the linter as "shell out to `otool -lv` /
`nm`". In-process re-read is strictly better — same semantic check,
no fork-and-grep fragility, no tool-version drift, one C helper
shared by both writers, works the same on any host that runs tcc.
Decision explained in the session 081 [README §Design call](README.md#design-call-re-read-in-process-not-shell-out).

## Status of session 080's open items

| # | Item | Status |
|---|---|---|
| (080 #1, carried 068 #3) | Post-write linter (otool/nm sanity) | **landed this session (v0.2.60)** — implemented as in-process re-read rather than otool/nm shell-out |
| (080 #2, carried 068 #4) | `-vv` diagnostic for reader's silent dropped relocs | unchanged |
| (080 #3, carried 067 #3) | Sibling r11 watch | unchanged |
| (080 #4, carried) | AltiVec intrinsics | unchanged |
| (080 #5, carried) | bcheck.c port | unchanged |
| (080 #6, carried) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more clean rebuild cycle — now ten+ cycles since 069) |
| (080 #7) | `rsync` exclude-list polish | unchanged (sidestepped this session by single-file `scp tcc/ppc-macho.c`) |

## Open work for next session

The self-link-diagnostics arc (#7 EXE half v0.2.50, dylib half v0.2.59,
post-write linter v0.2.60) is fully closed. Natural next-arc
candidates:

### 1. (carried, 068 #4) `-vv` diagnostic for reader's silent dropped relocs

When `macho_load_object_file` skips a section or drops a reloc
(SECTDIFF target lookup miss; debug section skip), it does so
silently. A `-vv` flag enabled at link time should log every such
decision to stderr with `file:section:reloc-index reason`. Would
have made the v0.2.40 sed-bug and the v0.2.44 SECTDIFF shadow-section
bug easier to triage. Symmetric in spirit to v0.2.50 / v0.2.59 /
v0.2.60: pre-write checks emission-readiness; this would log
emission-relevant decisions during read.

### 2. (carried, 067 #3) Sibling r11 watch

Sibling-register concern to v0.2.48's r12 fix. csmith hasn't
surfaced a repro yet. If a future seed does, the symmetric fix is
`reg_classes[8] = 0`. Just keep an eye on it.

### 3. (carried) AltiVec intrinsics

PowerPC SIMD. Would unlock vector-heavy programs (libpng's filter
loops, openssl's AES-NI fallback paths on PPC, gimp's pixel ops).
New parser front-end work + codegen. Multi-session lift.

### 4. (carried) `bcheck.c` port

PowerPC port of tcc's bounds-check runtime. Currently stubbed out
via `bcheck-ppc.c` (no-op shims). Real port would need to intercept
allocations, maintain a redzone map, and emit per-load/store
bounds-check calls. Multi-session lift.

### 5. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Now safe to drop after many confirmed
rebuild cycles on imacg3 — session 081 counts as another one.

### 6. (optional, low priority) `rsync` exclude-list polish

`tcc/c2str.exe` (built on uranium for testing, arm64) is not in
my current rsync exclude list. Sidestepped this session by doing a
single-file `scp tcc/ppc-macho.c`. If sync becomes a recurring pain
point in a session that touches many files, the right move is a
`scripts/sync-to-imacg3.sh` wrapper with the canonical exclude list.

## How to pick up

### Verify the v0.2.60 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.60-post-write-linter.sh
```

Expected last line:

```
PASS: exe + dylib both built and ran; post-write linter silent on both paths
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

### Re-run the dylib + post-write demos

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    for d in v0.2.25-dylib v0.2.26-link-dylib v0.2.29-dylib-slide \
             v0.2.31-libtcc-slide v0.2.40-sed v0.2.41-gzip v0.2.42-python \
             v0.2.59-dylib-self-link-diagnostics v0.2.60-post-write-linter; do
        printf "%-40s " "$d"; bash demos/$d.sh 2>&1 | tail -1
    done'
```

Each line should end with a one-line success summary.

### Trigger a post-write diagnostic deliberately

The session-081 README has a [verification table](README.md#hand-injected-breaks-three-diagnostic-categories)
with the exact perl one-liners that inject each break. To exercise
category (a) (bad magic), on imacg3 run:

```sh
cd /Users/macuser/tcc-darwin8-ppc/tcc
cp ppc-macho.c /tmp/bak.c
perl -i -pe 'if ($. == 3987) { s/MH_MAGIC/0xcafebabe/; }' ppc-macho.c
cd .. && bash scripts/build-tiger.sh
echo 'int main(){return 0;}' > /tmp/x.c
./tcc/tcc /tmp/x.c -o /tmp/x
# expected: tcc: error: ppc-macho: post-write: bad magic 0xcafebabe != MH_MAGIC 0xfeedface (byteorder regression?)
cp /tmp/bak.c tcc/ppc-macho.c && bash scripts/build-tiger.sh
```

**Don't forget the final rebuild** — restoring source alone leaves
the compiled tcc with the break baked in (session 081 #Findings).

### Tag + push status

v0.2.60-g3 is tagged. Push authority granted per
`memory/feedback_push_authority.md`:

```sh
git push origin main
git push origin v0.2.60-g3
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|
| Session start | Scout `ppc-macho.c` insertion surface (pre-write block structure, writer file-output lifecycle, struct definitions, error-noabort vs error pattern) | Explore (Sonnet) | Clean report — file:line refs for both pre-write blocks, both writers' fwrite/fclose flow, all relevant Mach-O struct field offsets, error-handling convention. Saved several minutes of grep + read on a 7k-line file. |

## Closing notes

The actual code is one new helper + two call-site insertions. The
interesting work was the **design decision** (in-process re-read
vs `otool` shell-out) and the **edge case the linter immediately
caught on first run** — `__DWARF`'s `vmsize=0` file-only-segment
shape. That single false positive on `132_bound_test` validated
the whole approach: a real check, exercising a real corner of the
writer's design, that gave a clean diagnostic instead of mysterious
behavior. Two follow-up edits (`seg_vmsize > 0` gates) made the
linter consistent with the writer's documented `__DWARF`
exception, and every demo and regression suite passed afterward.

User-visible benefit of v0.2.60: any future emission-side bug
(obuf corruption, byteorder slip, cmdsize literal drift, truncated
fwrite, missing/duplicated load commands) errors out at link time
with a `ppc-macho: post-write: ...` diagnostic naming the violated
invariant. The pre-write blocks (v0.2.50, v0.2.59) cover writer-
state arithmetic; the post-write linter (v0.2.60) covers on-disk
bytes. Together they bracket the entire writer.

Next session: [docs/sessions/081-post-write-linter-2026-05-15/HANDOFF.md](docs/sessions/081-post-write-linter-2026-05-15/HANDOFF.md)
