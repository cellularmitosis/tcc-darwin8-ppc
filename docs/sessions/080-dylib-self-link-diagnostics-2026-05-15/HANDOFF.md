# Handoff — end of session 080 (2026-05-15)

## TL;DR

**v0.2.59-g3 closes the dylib half of roadmap #7.** The four
pre-write sanity-check invariants v0.2.50 added to
`macho_output_exe` are now mirrored into `macho_output_dylib`,
so a dylib gone wrong (LC_DYSYMTAB miscounting, stub/slot
mispairing, common-symbol-in-missing-bss, PIC layout drift)
gets a tcc-level `ppc-macho: dylib …` diagnostic at link time
instead of surfacing as `dlopen("…") returned NULL` with no
useful context.

The change is one new ~230-line block in
[`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) inserted between
the `Compute __LINKEDIT layout` block and `Serialize: Mach
header`, parallel in spirit to the EXE block at line 3084.
Adapted for the dylib writer's different fixed points
(`DYLIB_TEXT_VMADDR_BASE` instead of `EXE_TEXT_VMADDR_BASE`,
`__mh_dylib_header` instead of `__mh_execute_header`, no
`entry_addr`, no `__DWARF`, 32-byte PIC stubs instead of 16-byte
absolute). Every invariant verified by hand-injection on
imacg3 (see [README §Verification](README.md#hand-injected-breaks-one-per-invariant)).

* **HEAD at session start:** `6ff2879` (end of session 079, v0.2.58-g3).
* **HEAD at session end:** v0.2.59-g3, tag pushed.
* **Source changes:** one file
  ([`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c)).
* **Demo added:**
  [`demos/v0.2.59-dylib-self-link-diagnostics.{c,sh}`](../../../demos/).
* **tests2 111/111; abitest cc+tcc 48/48; bootstrap fixpoint
  holds; eight existing dylib demos all PASS.**

## What landed

### Files edited

* [`tcc/ppc-macho.c`](../../../tcc/ppc-macho.c) — new sanity-
  check block in `macho_output_dylib` between the
  `Compute __LINKEDIT layout` block (ending at the previous
  `linkedit_filesize = loff - linkedit_file_off;` at line 4680)
  and `Serialize: Mach header`. ~230 lines (a fair chunk of
  which is the comment block calling out each adjustment from
  the EXE block's invariants). Pure read-and-check; no writer-
  state mutations. Same don't-bail-on-first-failure pattern as
  the EXE block.

### Files added

* [`demos/v0.2.59-dylib-self-link-diagnostics.c`](../../../demos/v0.2.59-dylib-self-link-diagnostics.c)
  — dlopen driver. Loads `libdiag.dylib` and exercises every
  section class via dlsym'd entrypoints.
* [`demos/v0.2.59-dylib-self-link-diagnostics.sh`](../../../demos/v0.2.59-dylib-self-link-diagnostics.sh)
  — driver. Builds the dylib (an inline `cat > $WORK/libdiag.c`
  source that touches text / rodata / data / bss / libSystem-
  imported externs / function-pointer-in-data producing a
  locrel slide-time fixup), then builds the dlopen driver,
  runs it, expects `ro=ok | data=42 | bss=11 22 33 | got=ok | call_count=2`.

### Docs

* [`docs/roadmap.md`](../../roadmap.md) — header bumped
  ("v0.2.58-g3" → "v0.2.59-g3", "Thirty-six" → "Thirty-seven");
  v0.2.59-g3 row prepended to the table.
* [`demos/README.md`](../../../demos/README.md) — v0.2.59 row
  added above v0.2.58.
* `docs/sessions/080-dylib-self-link-diagnostics-2026-05-15/README.md`
* This `HANDOFF.md`.

### Memory updates

None. The two reusable findings (perl `-pe` `next` does NOT skip
the implicit print → use `$_ .= …` instead; the
`linkedit_filesize = loff - linkedit_file_off;` line appears in
both EXE and dylib writers so unique-anchoring is required)
are captured in the [session README's Findings section](README.md#findings-worth-remembering).

## Status of session 079's open items

| # | Item | Status |
|---|---|---|
| (079 #1, carried 068 #2) | dylib self-link diagnostics | **landed this session (v0.2.59)** |
| (079 #2, carried 068 #3) | Post-write linter (otool/nm) over emitted bytes | unchanged |
| (079 #3, carried 068 #4) | `-vv` diagnostic for reader's silent dropped relocs | unchanged |
| (079 #4, carried 067 #3) | Sibling r11 watch | unchanged |
| (079 #5, carried) | AltiVec intrinsics | unchanged |
| (079 #6, carried) | bcheck.c port | unchanged |
| (079 #7) | Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/` on imacg3 | unchanged (this session counted as one more clean rebuild cycle) |
| (079 #8) | `rsync` exclude-list polish | unchanged (sidestepped this session by single-file `scp tcc/ppc-macho.c`) |

## Open work for next session

Roadmap #7 is now fully done (EXE half v0.2.50, dylib half
v0.2.59). The natural next-arc candidates are the carried items:

### 1. (carried, 068 #3) Post-write linter (otool / nm sanity)

Run `otool -lv` / `nm` over `macho_output_exe` (and now
`macho_output_dylib`) output before returning success, parse
for shapes the writer is supposed to produce (LC_LOAD_DYLIB
count == nb_dlls + 1 for exes / + 0 for dylibs, LC_DYSYMTAB
extreloff + nextrel within file, LC_SYMTAB nsyms == n_local +
n_extdef + n_undef, etc.). The pre-write checks in v0.2.50 +
v0.2.59 verify the *design*; a post-write linter would verify
the *emission* — a different class of bug (`obuf` corruption,
byteorder regressions, command-size mismatches). The kind of
backstop the v0.2.57 strip work could have used.

### 2. (carried, 068 #4) `-vv` diagnostic for reader's silent dropped relocs

When `macho_load_object_file` skips a section or drops a reloc
(e.g. SECTDIFF target lookup miss; debug section skip), it does
so silently. A `-vv` flag enabled at link time should log every
such decision to stderr with `file:section:reloc-index reason`.
Would have made the v0.2.40 sed-bug and the v0.2.44 SECTDIFF
shadow-section bug easier to triage.

### 3. (carried, 067 #3) Sibling r11 watch

Sibling-register concern to v0.2.48's r12 fix — r11 is hardcoded
scratch in the rare large-stack-offset struct-arg path, while
still in the int allocator pool. csmith hasn't surfaced a repro
yet. If a future seed does, the symmetric fix is `reg_classes[8]
= 0`. Just keep an eye on it.

### 4. (carried) AltiVec intrinsics

PowerPC SIMD. Would unlock vector-heavy programs (libpng's
filter loops, openssl's AES-NI fallback paths on PPC, gimp's
pixel ops). New parser front-end work + codegen.

### 5. (carried) `bcheck.c` port

PowerPC port of tcc's bounds-check runtime. Currently stubbed
out via `bcheck-ppc.c` (no-op shims). Real port would need to
intercept allocations, maintain a redzone map, and emit per-
load/store bounds-check calls.

### 6. (optional, low priority) Delete `/Users/macuser/tcc-darwin8-ppc.bak-pre-069/`

Inherited from session 069. Safe to drop after a few more
confirmed rebuild cycles on imacg3 — session 080 counts as one
more.

### 7. (optional, low priority) `rsync` exclude-list polish

`tcc/c2str.exe` (built on uranium for testing, arm64) is not in
my current rsync exclude list. Sidestepped this session by doing
a single-file `scp tcc/ppc-macho.c` (only one file changed). If
sync becomes a recurring pain point in a session that touches
many files, the right move is a `scripts/sync-to-imacg3.sh`
wrapper with the canonical exclude list.

## How to pick up

### Verify the v0.2.59 release end-to-end

```sh
ssh imacg3
cd /Users/macuser/tcc-darwin8-ppc
bash demos/v0.2.59-dylib-self-link-diagnostics.sh
```

Expected last line:

```
ro=ok | data=42 | bss=11 22 33 | got=ok | call_count=2
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

### Re-run the dylib demos

```sh
ssh imacg3 'cd /Users/macuser/tcc-darwin8-ppc && \
    for d in v0.2.25-dylib v0.2.26-link-dylib v0.2.29-dylib-slide \
             v0.2.31-libtcc-slide v0.2.40-sed v0.2.41-gzip v0.2.42-python \
             v0.2.59-dylib-self-link-diagnostics; do
        printf "%-40s " "$d"; bash demos/$d.sh 2>&1 | tail -1
    done'
```

Each line should end with a one-line success summary.

### Trigger one of the diagnostics deliberately

The session-080 README has a [verification table](README.md#hand-injected-breaks-one-per-invariant)
with the exact perl one-liners that inject each break (anchored
on `int sanity_ok = 1;` with `$. > 4690` to skip the EXE writer's
identical line). To exercise diagnostic (a), rebuild with
`linkedit_vmaddr = 0xdeadbeef;` injected just after
`int sanity_ok = 1;` in the dylib block, then run any
`tcc -shared` invocation; tcc should error out with
`ppc-macho: dylib __LINKEDIT vmaddr 0xdeadbeef != __TEXT+__DATA end …`
and produce no output file.

### Tag + push status

v0.2.59-g3 is tagged. To push (push authority granted per
`memory/feedback_push_authority.md`):

```sh
git push origin main
git push origin v0.2.59-g3
```

## Subagent log

| When | Task | Model | Outcome |
|---|---|---|---|

No subagents.

## Closing notes

The actual code change is small — a single new block in one
file, structurally parallel to the existing EXE block. The
interesting part of the session was the verification: getting
the perl injection script to land in the right place (the
dylib-writer half of `linkedit_filesize = loff - linkedit_file_off;`,
not the EXE-writer half) and getting it to inject without
duplicating lines (use `$_ .= "…\n"` — `print; …; next` doesn't
skip perl `-p`'s implicit print). Both gotchas are documented
in the session README in case anyone reaches for the same trick
later.

User-visible benefit of v0.2.59: a dylib-build regression that
would otherwise produce a working-but-broken `.dylib` and
surface as a no-context `dlopen() returned NULL` at runtime now
errors out at compile time with a `ppc-macho: dylib …` message
naming the violated invariant — file:line of the bad layout
constant, segment / section names, addresses involved.

Next session: [docs/sessions/080-dylib-self-link-diagnostics-2026-05-15/HANDOFF.md](docs/sessions/080-dylib-self-link-diagnostics-2026-05-15/HANDOFF.md)
