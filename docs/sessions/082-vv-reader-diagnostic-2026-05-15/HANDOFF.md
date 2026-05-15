# Handoff — end of session 082 (2026-05-15)

## TL;DR

**v0.2.61-g3 closes the reader-side symmetric counterpart to the
v0.2.50 / v0.2.59 / v0.2.60 writer-side diagnostics.** Those three
bracket the writer (pre-write covers in-memory state; post-write
covers on-disk bytes). v0.2.61 surfaces routing decisions on the
**read** path — pass-1 section skips, pass-3 wholesale reloc drops,
per-reloc drops inside `macho_translate_relocs`, per-symbol filters
inside `macho_translate_sym` and pass-2 of `macho_load_object_file`.
Output is gated on `s1->verbose >= 2` (the existing `-vv` counter,
already supported by the option parser); zero overhead at default
verbosity. 21 emit sites cover every silent decision; STAB symbols
and standalone PPC_RELOC_PAIR continues are intentionally not logged
(noise mechanics, not routing decisions).

Format: `tcc: reader: <basename>: <category>: <details>` to stderr,
where category is `section-skip` / `reloc-drop` / `sym-skip`. Lookup
of the filename comes from `s1->current_filename` (set in
`tcc_add_binary` at `libtcc.c:1129` right before the reader is
called). Helper is ~30 lines; call sites total ~110 lines added to
`tcc/ppc-macho.c`.

* **HEAD at session start:** `e3de11e` (end of session 081, v0.2.60-g3).
* **HEAD at session end:** v0.2.61-g3, tag pushed.
* **Source changes:** one file
  ([`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c)).
* **Demo added:**
  [`demos/v0.2.61-vv-reader-diagnostic.{c,sh}`](../../../demos/).
* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint
  holds; ten existing v0.2.25..v0.2.60 demos all PASS.**

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — new
  `macho_reader_log(s1, category, fmt, ...)` static helper inserted
  right after `macho_section_to_elf`. Returns early if
  `s1->verbose < 2`. Looks up `s1->current_filename`, strips the
  directory prefix with `strrchr`, writes
  `tcc: reader: <basename>: <category>: <details>\n` to stderr via
  `vfprintf`. Plus 21 call sites at all the previously-silent
  decision points: 2 section-skip, 13 reloc-drop, 7 sym-skip
  (inventory in the session [README](README.md#what-landed)).
  ~140 lines added net (helper + emit sites).

### Files added

* [`demos/v0.2.61-vv-reader-diagnostic.c`](../../../demos/v0.2.61-vv-reader-diagnostic.c)
  — calls `helper(7)` from a sibling `.o`.
* [`demos/v0.2.61-vv-reader-diagnostic.sh`](../../../demos/v0.2.61-vv-reader-diagnostic.sh)
  — five assertions: default-silent, `-v`-silent-for-reader,
  `-vv`-produces-lines, `-g`+`-vv`-zero-sym-skips (STAB filter), and
  the linked program still runs.

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped
  ("v0.2.60-g3" → "v0.2.61-g3", "Thirty-eight" → "Thirty-nine");
  v0.2.61-g3 row prepended to the table.
* [`demos/README.md`](../../../demos/README.md) — v0.2.61 row
  added above v0.2.60.
* `docs/sessions/082-vv-reader-diagnostic-2026-05-15/README.md`
* This `HANDOFF.md`.

### Memory updates

None. The reusable findings (`-vv` already in the option parser via
counting `v`s; `s1->current_filename` is the right hook for the
loaded file's path; stderr for diagnostics vs stdout for writer
summaries) are captured in the
[session README's Findings section](README.md#findings-worth-remembering).

## Status of session 081's open items

| # | Item | Status |
|---|---|---|
| (081 #1, carried 068 #4) | `-vv` diagnostic for reader's silent dropped relocs | **landed this session (v0.2.61)** |
| (081 #2, carried 067 #3) | Sibling r11 watch | unchanged |
| (081 #3, carried) | AltiVec intrinsics | unchanged |
| (081 #4, carried) | bcheck.c port | unchanged |
| (081 #5, carried) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (counted as one more clean rebuild cycle — eleven+ now since 069) |
| (081 #6) | `rsync` exclude-list polish | unchanged (sidestepped again this session by single-file `scp tcc/ppc-macho.c`) |

## A finding worth investigating

The `-vv` output on a hello-world link against `crt1.o` reveals
**four "non-extern stub-slot resolve failed" entries** on
`crt1.o:__text` (relocs 21, 23, 29, 85 → target VAs 0x330, 0x340,
0x350, 0x370 in `__symbol_stub1`). The runtime works regardless —
hello-world runs cleanly — but those branches are silently dropped
by tcc's static link. Either they're handled at runtime by dyld's
stub binder (likely; matches how Apple's crt1 routes its internal
calls into `_exit` etc.), OR there's a latent bug masked by some
fallback. The diagnostic now surfaces the question; investigation
deferred to a future session if anyone wants to chase it. Not
release-blocking — every regression suite still passes.

## Open work for next session

### 1. Sibling r11 watch (carried, 067 #3)

Sibling-register concern to v0.2.48's r12 fix. csmith hasn't
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

### 4. Investigate the 4 crt1.o non-extern stub-slot resolve fails (new)

Per the "finding worth investigating" above. Quick: read crt1.o
with `otool -rv crt1.o` + cross-reference the indirect symtab + the
stub section's `reserved1` / `reserved2` to figure out whether
`macho_resolve_stub_slot`'s indexing is off-by-something or whether
those branches genuinely have no static-link translation. If it's
the first, fix `macho_resolve_stub_slot`; if it's the second,
upgrade the message from "resolve failed" to "expected runtime-bind"
and stop logging at `-vv` (or log at `-vvv`). Single-session task.

### 5. Investigate the 4 crt1.o internal-branch oddity at SOURCE level (new)

Adjacent to #4. The four crt1.o relocs targeting `__symbol_stub1`
aren't external-call stubs (those go through extern relocs); they
look like internal branches from crt1's code to its own stub
section. The source of crt1.o (Apple's libcrt1) is in Apple's
Libc, available in the Darwin sources. Worth a glance to understand
what code shape produces those.

### 6. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after eleven+ confirmed
rebuild cycles on imacg3.

### 7. (optional, low priority) `rsync` exclude-list polish

`tcc/c2str.exe` (built on uranium for testing, arm64) is not in
my current rsync exclude list. Sidestepped this session and last
by single-file `scp tcc/ppc-macho.c`. If sync becomes a recurring
pain point in a session that touches many files, the right move is
a `scripts/sync-to-imacg3.sh` wrapper with the canonical exclude
list.

## How to pick up

### Verify the v0.2.61 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.61-vv-reader-diagnostic.sh
```

Expected last line:

```
PASS: -vv reader diagnostic surfaces 17 reader lines (9 section-skip, 8 reloc-drop); default verbosity silent
```

The exact reader-line count varies with the link inputs — what
matters is `default verbosity silent` at the tail and the assertion
chain along the way.

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

Each line should end with its expected success summary.

### See `-vv` in action

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    echo "int main(){return 0;}" > /tmp/x.c && \
    ./tcc/tcc -vv /tmp/x.c -o /tmp/x 2>&1'
```

You should see 11–12 `tcc: reader:` lines describing the section
skips and reloc drops from `crt1.o`, plus the existing tcc writer
summary on stdout interleaved at the end. Stderr is the reader
diagnostic; stdout is the writer summary; mixed when you don't
redirect.

### Tag + push status

v0.2.61-g3 is tagged. Push authority granted per
`memory/feedback_push_authority.md`:

```sh
git push origin main
git push origin v0.2.61-g3
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|
| Session start | Scout `ppc-macho.c` for silent-skip / silent-drop sites in `macho_load_object_file` and helpers (`macho_translate_relocs`, `macho_translate_sym`) | Explore (Sonnet) | Clean report — 25 sites across section skips, reloc drops, symbol filtering, pass-2 loop. file:line refs for each; the existing `tcc_error_noabort` sites for comparison surface. |
| Session start (parallel) | Survey `-v` / `-vv` plumbing in tcc — option parser, state field, existing verbose-print sites and convention, logging conventions in ppc-macho.c | Explore (Sonnet) | Clean report — `-vv` already parses through repeated-`v` counter; `unsigned char s->verbose` on TCCState; level-2 convention used by tccpe/tccelf/tccrun/tccpp; ppc-macho's existing writer summaries are on stdout. No new option needed. |

## Closing notes

The actual code is one helper + 21 emit sites, mechanically
applied. The interesting work was the **gating decision** (extend
existing `-v` counter rather than add a new flag), the **filter
decision** (don't log STAB or PAIR mechanics — they're noise, not
routing), and the **stream choice** (stderr, not stdout, for
greppability and to avoid mixing with `tcc -run` program output).

User-visible benefit of v0.2.61: any future bug shaped like the
v0.2.40 sed-bug (silent pass-1 section skip causing later mis-link)
or the v0.2.44 SECTDIFF shadow-section bug (silent reloc drop) now
gives a `tcc: reader: ...: <category>: ...` line under `-vv` naming
the exact decision and the surrounding context (section name, reloc
index, target VA, target sym index, etc.). The self-link-diagnostic
arc (writer + reader, pre- and post-) is fully closed.

The v0.2.61 diagnostic also immediately surfaced an interesting
**not-a-bug-or-is-it?** finding on `crt1.o` — four internal
branches targeting `__symbol_stub1` that the stub-slot resolver
drops silently. The runtime tolerates it (hello-world links and
runs), but the diagnostic now makes the question askable. See
the [session README](README.md#what-the-diagnostic-surfaces-on-a-real-link)
for the captured output and the open items above for follow-up.

Next session: [docs/sessions/082-vv-reader-diagnostic-2026-05-15/HANDOFF.md](docs/sessions/082-vv-reader-diagnostic-2026-05-15/HANDOFF.md)
