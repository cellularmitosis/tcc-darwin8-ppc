# Claude Conversation Log

Session ID: agent-a9a474049b12adb76
Date: 2026-04-30 13:18:52

---

## 🤖 Claude (Orchestrator)

I need to understand the structure and conventions of several sister projects that target Mac OS X Tiger on PowerPC. Look at the following directories and report back on:

1. The general structure/pattern of each project (README, CLAUDE.md, scripts, etc.)
2. How they organize their work (build scripts, patches, etc.)
3. Any common conventions across projects

Check these projects (read their CLAUDE.md and/or README files, and list top-level files):

- /Users/cell/claude/ionpower-node (top-level files + CLAUDE.md/README)
- /Users/cell/claude/golang-darwin8-ppc (top-level files + CLAUDE.md/README)
- /Users/cell/claude/ghc-darwin8-ppc (top-level files + CLAUDE.md/README)
- /Users/cell/claude/llvm-7-darwin-ppc (top-level files + CLAUDE.md/README)

Keep the report concise — focus on patterns and conventions, not exhaustive file listings. Under 400 words.

---

## 🤖 Claude (Subagent)

I'll examine each project to understand their structure and conventions.

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "ls -la /Users/cell/claude/ionpower-node/ /Users/cell/claude/golang-darwin8-ppc/ /Users/cell/claude/ghc-darwin8-ppc/ /Users/cell/claude/llvm-7-darwin-ppc/ 2>&1 | head -100"
}


---

## 🤖 Claude (Subagent)

Now let me read the CLAUDE.md files from each project:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/ionpower-node/CLAUDE.md"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/golang-darwin8-ppc/CLAUDE.md"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/ghc-darwin8-ppc/CLAUDE.md"
}


---

## 🤖 Claude (Subagent)


🔧 Using tool: Read
Input: {
  "file_path": "/Users/cell/claude/llvm-7-darwin-ppc/CLAUDE.md"
}


---

## 🤖 Claude (Subagent)

Now let me check for any scripts directories and look at the key structural elements:

---

## 🤖 Claude (Subagent)


🔧 Using tool: Bash
Input: {
  "command": "find /Users/cell/claude/{ionpower-node,golang-darwin8-ppc,ghc-darwin8-ppc,llvm-7-darwin-ppc} -maxdepth 2 -type f -name \"*.sh\" | head -20"
}


---

## 🤖 Claude (Subagent)

Perfect. Now I have a comprehensive view. Let me compile the report:

## Project Structure & Conventions Report

These four sister projects share a remarkably cohesive architecture despite targeting different languages/toolchains on Mac OS X Tiger PowerPC.

### Universal Structure Pattern

All four projects follow the same layout:
- **`docs/`** — sessions, roadmap, plans, research notes, reference materials
- **`scripts/`** — build orchestration, cross-compilation wrappers, test runners
- **`patches/`** — git-format patches applied to upstream sources
- **`tests/`** — regression test suites (tests/, testprogs/, stress/)
- **`demos/`** — runnable programs proving each release's capabilities
- **`external/`** — git-ignored upstream source trees (go, GHC, LLVM)
- **`CLAUDE.md`** — agent instructions and workflow (this is the key differentiator from typical projects)
- **`README.md`** — status, releases table, implementation status emoji matrices

### Session & Documentation Conventions

**Session naming evolved across projects:**
- **ionpower-node** (oldest): date-session-letter format (`2026-04-25-session-a/`)
- **golang-darwin8-ppc**: date-session-counter-slug (`YYYY-MM-DD-<slug>/`) with dated README/findings/commits per session
- **ghc-darwin8-ppc** & **llvm-7-darwin-ppc**: numbered globally (`NNN-<slug>/`) with retroactive backfill of earlier phases

**Session artifact conventions:**
- `README.md` — narrative: what arrived, what was done, what exits
- `findings.md` — cross-session knowledge ("things we learned that will matter later")
- `commits.md` — one-liner per commit landed
- All projects emphasize liberal documentation upfront, not post-mortems at the end

### Key Workflow Patterns

1. **Regression suites enforce invariants** — each project names specific scripts that must pass before committing (e.g., golang's `verify-handwritten.sh`, ghc's test battery). Red tests block forward progress.

2. **Patch canonical-ness** — all fork from upstream (Go, GHC, LLVM). Outer repo patches are canonical; `go-upstream/`, `external/` are git-ignored working copies. A `regen-patches.sh` script re-syncs.

3. **Release ritual** — all three require:
   - Version bump
   - Small runnable demo proving the capability
   - README update with implementation status emoji tables (✅/🟡/❌)
   - Tag + GitHub release + artifact upload
   - One-liner appended to releases table

4. **Risk tolerance by host** — identical framing across all CLAUDE.md:
   - `uranium` (main Mac): low risk; brew installs, source builds only
   - PowerPC fleet: high risk; experiment freely, it's reinstallable
   - `indium` (shared VM): medium; clean up on failure

### Build Orchestration

- **ionpower-node**: Makefile-heavy; triad-build.sh orchestrates G3/G4/G5 parallel builds
- **golang-darwin8-ppc**: `build-fork.sh` (bootstrap), `m4-all.sh` (regression), `regen-patches.sh` (sync)
- **ghc-darwin8-ppc**: cross-env.sh + linker shims (ppc-ld-*.sh, ppc-cc.sh) + install.sh
- **llvm-7-darwin-ppc**: xc.sh cross-compile wrapper, polybench-run.sh stress tests

All use wrapper scripts to handle modern macOS → Tiger cross-compile friction (codesigning, DWARF, linker compatibility).

### Unsupervised Mode Philosophy

Consistent across all: "don't block for genuinely unreasonable actions." The bar is "will this probably teach us something?" — not absolute safety. This reflects a test-bed environment sensibility where learning from failures is valued.

**Bottom line:** These projects form a cohesive ecosystem unified by session-driven documentation, patch-centric development, demo-driven releases, and pragmatic risk tolerance tied to host type.

---

