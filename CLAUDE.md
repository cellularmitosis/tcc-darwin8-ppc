# tcc-darwin8-ppc

A Mac OS X 10.4 Tiger / PowerPC backend for [tcc](https://repo.or.cz/tinycc.git),
the Tiny C Compiler. The TCC source tree lives in-tree (the tarball is
small enough that maintaining a patch set against upstream is more
ceremony than it's worth) — modifications are made directly and tracked
through git history.

## Repo layout

```
docs/
  roadmap.md            — prioritized forward work.
  sessions/             — one dir per session, globally numbered.
    NNN-<slug>/
      README.md         — narrative: arrival state, what was done, exit state.
      findings.md       — "things learned that will matter later." (optional)
      commits.md        — commits landed, one-liner each. (optional)
  notes/                — reference material that outlasts any one
                          session (PPC ABI quirks, Mach-O layout notes,
                          TCC internals, etc.).
tcc/                    — the upstream source, in-tree. Modify directly.
scripts/                — build orchestration, cross-compile wrappers,
                          test runners.
tests/                  — regression suites we own (separate from tcc's
                          own tests/ subdir).
demos/                  — one runnable demo per milestone, showcasing
                          what that milestone unlocked. See
                          demos/README.md.
```

The `tcc/` source is pulled from upstream once and lives in git as
ordinary files. There is no `patches/` directory, no `external/` and no
regen-patches script. If we ever need to rebase onto a newer upstream,
that's a session task — not a routine workflow.

## README status tables — deferred

The sister projects use "Implementation status" tables on the front
README (✅ / 🟡 / ❌ matrices for languages, stdlib coverage, hardware,
etc.). That format works well when there's a natural feature matrix
that outside readers care about.

For a TCC PPC backend the work is jagged — codegen ops, ABI corner
cases, Mach-O emission, linker interop — and most of it is invisible
to a casual reader. We'll defer the status-table format until we
have something worth showing (e.g. a release that bootstraps tcc on
Tiger, or the tcc testsuite passing). Until then the README stays
narrative.

## Demos workflow

Every meaningful milestone ships a small runnable demo under
`demos/` that proves the new capability. A milestone might be a
GitHub release, but it might also be a session that lands a
visibly-new capability (e.g. "tcc -run executes PPC code", "if/while
loops compile correctly"). Pre-release milestones use
`demos/sNNN-<slug>.{c,sh}`; once we ship versioned releases, future
demos go under `demos/v<X>.<Y>.<Z>-<slug>.{c,sh}`.

Each demo:
- Is a tiny program that compiles and runs end-to-end on a real Tiger
  G3, with a one-line invocation a reader can paste.
- Is named by the milestone that introduced the capability it
  showcases.
- Has a row in [`demos/README.md`](demos/README.md)'s table linking
  the source, the runner, the milestone session, and the expected
  output.

The goal is "someone landing on the repo can scroll demos/ and see
the project's current capability surface in concrete C, not prose."

## Sessions workflow

Substantive work lives in [`docs/sessions/`](docs/sessions/). Each
session is its own globally-numbered dir (`NNN-<slug>/`).

Per-session contents:
- `README.md` — narrative: arrival state, what was done, exit state.
  Always.
- `findings.md` — durable lessons that will matter in future sessions.
  Optional.
- `commits.md` — one-liner per commit landed. Optional.

End-of-session ritual: make sure the README captures the exit state
clearly enough that the next session (you or a future Claude) can
pick up cold without re-reading the diffs.

## Document everything

Default to capturing liberally in the current session directory.
Plans, research, running work logs, decisions, dead ends, ambiguity
discussions, scope shifts, gotchas-in-the-moment — all worth writing
down. Surface what you create briefly so I have awareness. Err on
the side of MORE capture, not less. The goal is that a future-me
(or a future Claude session) can pick up the thread without me
having to re-explain.

## Unsupervised mode

When the user says "work unsupervised" (or similar wording), they're
unreachable — at work, asleep — and cannot answer questions. Under
this mode:

- **Don't stop to ask.** Unblock yourself: make assumptions, run
  experiments, search the web for the problem or prior art, read
  related source, try the obvious fixes.
- **Long runtimes are fine.** Eight or more hours of iteration is
  not too long if the task warrants it.
- **Only block for genuinely unreasonable actions.** E.g. "delete
  the user's games to free disk space" is unreasonable. A workaround
  is almost always available.
- **Document every judgment call** in the active session's
  `README.md` / `findings.md` — assumptions made, experiments
  tried, dead-ends rolled back. That log is what the user reviews
  on return.

### Risk tolerance by host

The line between reasonable and unreasonable is host-dependent:

- **uranium (this main Mac)** — low risk tolerance; this machine
  matters.
  - OK: `brew install`, downloading source tarballs, building from
    source, standard package installs.
  - Not OK: installing random hobbyist binaries off the internet
    (e.g. a stranger's ffmpeg build).
- **PowerPC fleet** — high risk tolerance; these are test machines
  and we can reinstall them.
  - OK: downloading and trying hobbyist Tiger/Leopard PowerPC
    builds found via web search, pulling patches from
    MacPorts/Fink/Debian/Gentoo as inspiration or direct drop-in,
    copying utilities between fleet hosts, experimental kernel
    installs, `tiger.sh` / `leopard.sh` package installs, building
    from source in-place.
  - The bar is "will this probably teach us something?" not "is
    this provably safe?"
- **indium (build VM)** — medium tolerance; reinstallable but we
  share it with other projects. Default to building in a scratch
  directory, clean up on failure.
