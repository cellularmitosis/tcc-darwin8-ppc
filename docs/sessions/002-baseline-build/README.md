# 002 — Baseline build

Pull mob source in-tree, write the roadmap, attempt baseline builds
on uranium (host) and imacg3 (target). Establish ground truth before
any PPC work.

## Arrival state

After 001, mob was the chosen base, but no source was in-tree yet
and no build had been attempted on either machine.

## What was done

1. **`tcc/` populated** — `rsync -a --exclude='.git' ~/tmp/tcc-survey/mob/ tcc/`.
   Provenance noted in [`tcc/.UPSTREAM.md`](../../../tcc/.UPSTREAM.md):
   commit `b39da9f6`, "Fix false warnings with readonly atomics", pulled
   2026-04-30.

2. **`.gitignore`** — added at repo root, ignoring tcc build
   artifacts (`*.o`, `*.a`, `tcc/tcc`, `tcc/config.*`, etc.).

3. **Baseline build on uranium** —
   `./configure --prefix=/tmp/tcc-baseline-uranium && make -j4`
   succeeded. `tcc -B. /tmp/hello.c` compiled and ran. Confirms our
   mob snapshot is intact.

4. **Baseline build on imacg3** — pushed source to
   `imacg3:~/tcc-darwin8-ppc/tcc/`. Hit two issues:
   - **GCD missing** (`<dispatch/dispatch.h>`). 10.6+ feature.
     Worked around with `--config-semlock=no`.
   - **Makefile has no `ppc_FILES`**. Expected — there's no PPC
     backend, so this is the actual baseline state. `ar`
     fails on empty `LIBTCC_OBJ`.

5. **[`roadmap.md`](../../roadmap.md) written** — phases 0 through 6,
   plus testing methodology, out-of-scope list, and risk register.

6. **[`findings.md`](findings.md)** — captures the configure output,
   the two blockers, and questions for later sessions
   (`new_macho=no`, codesign on Tiger, DWARF disable).

## Decisions

- **Build natively on imacg3, not cross-compile from uranium.**
  TCC is small; G3 builds in minutes; cross-compile setup is more
  friction than the build itself. Workflow: edit on uranium →
  rsync to imacg3 → make/test.
- **`--config-semlock=no` is permanent for our build.** Tiger
  has POSIX semaphores but tcc.h's Apple branch hardcodes GCD
  (10.6+). We don't need libtcc thread safety for a self-hosting
  compiler driver. A proper fix (fall through to POSIX on pre-10.6
  SDK) is queued but not blocking.
- **Roadmap phases 1-6 estimated at ~30 sessions.** Granular
  enough to commit per session, coarse enough to not over-plan.

## Exit state

- `tcc/` populated with mob source, in-tree.
- `.gitignore` keeps build artifacts out of git.
- Roadmap documented.
- Both uranium and imacg3 build-paths verified up to "the missing
  ppc backend."
- Source pushed to `imacg3:~/tcc-darwin8-ppc/tcc/` for further
  iteration.

## Next session

Session 003 — scaffold the backend. Add `ppc_FILES` and
`ppc-osx_FILES` Makefile entries. Write stub `ppc-gen.c`,
`ppc-link.c`, `ppc-asm.c`, `ppc-tok.h` with the correct symbol
exports (matching what `arm64-gen.c` exports) but bodies that
`abort()`. Goal: `make` completes; resulting `tcc` runs and exits
1 the moment it's asked to generate code. That's the empty
canvas.
