# Windows fork — charter

Forward strategy for the Windows port of OpenScanner. This is the fork's **charter**: what it
is, what it shares with the parent, how work is coordinated, and what must be true before it
builds.

Owner: the `openscanner/planner` agent. Statuses, claims and evidence live on the board —
this file points at them and never restates them.

_Last planned: 2026-10-18._

## 1. The decision: name and shape

**Name: `Open3DScanner-Win64`** (owner `juicie-brucie`, sibling of `Open3DScanner`).

- `Win64` rather than `Windows` because the target is **x64** — the vendor ships an x64
  Windows build, and Windows-on-ARM is a separate question (Open question 2). The parent pins
  an **arm64** slice for the same reason; the arch belongs in the name.
- Alternatives: `Open3DScanner-Windows` (friendlier, less precise); `openscanner-win`
  (lowercase, breaks the repo family's naming).

**Shape: standalone, not a GitHub *fork* relationship.** A GitHub fork shares history and
routes PRs at the parent by default, which is wrong here — the fork's CI runs on Windows, its
vendor artifacts differ, and it must not accidentally PR platform-specific changes into the
parent's gate. Standalone, with the parent as a **pinned upstream**.

## 2. What stays canonical in the parent

The fork's most important rule, because it protects the one thing the recomp loop is built on.

**`engine/include/openscanner/obscan_api.h` is single-sourced in the parent.** A second
header in a second repo is a second contract, and two contracts drift. Therefore:

- Platform differences go into the **one** header as `#ifdef _WIN32`-guarded declarations,
  with a Windows findings doc alongside the macOS ones — never a forked copy.
- If the Windows DLL's ABI genuinely differs (calling convention, packing, export names), the
  difference is recorded as a **platform note in the findings**, not as a divergent header.
- The fork consumes the header as a **synced copy with a recorded pin** (commit + sha256),
  checked in CI. A submodule is the alternative; a synced copy is simpler for one file and
  fails loudly when stale.
- `docs/recon/findings/**` stays the home for recovered semantics. The fork adds Windows
  findings there; it does not fork the directory.

Everything else — `mesh/`, `ui/`, most of `engine/`, `tools/` — is shared code with a platform
split, and the fork tracks the parent as upstream.

## 3. Portability map

What actually needs work, from the current tree (the loader and CMake already carry the split
— this is not a guess):

| Layer | State | Windows work |
| --- | --- | --- |
| `engine/src/obscan_loader.cpp` | **already `#ifdef`-split** — `LoadLibraryA`/`GetProcAddress`/`GetLastError` vs `dlopen`/`dlsym` | point it at the Windows DLL path; the macOS app path is inside the Apple branch |
| `engine/CMakeLists.txt` | already has a `WIN32` branch | verify it compiles; the headers were built for clang |
| `mesh/` (Manifold, libigl, meshio) | portable, permissively licensed | build only; the `igl/copyleft` rule is unchanged |
| `ui/` | portable (web) | none |
| `tools/`, `tests/hardware/` | **`if(APPLE)` throughout** | port CMake + the worker/launcher split; the interpose-dylib mechanism is macOS-specific |
| `.claude/hooks/guard-hw-run.sh` | **bash**, POSIX path semantics | does not run on Windows — needs an equivalent before any hardware work |
| Recon artifacts (header offsets, findings) | recovered from the **macOS arm64** dylib | **a new recon target**, not a port — see §6 |

## 4. Branch and PR conventions

Mirror the parent's mechanics, so an operator switching machines keeps one mental model:

- **One worktree per role, one session per worktree.** A room belongs to the session in it;
  never share a checkout, never tear down someone else's room. **On this platform a room is a
  clone, not a linked worktree**, and the reason is a platform bug rather than a preference:
  OpenCode 2.0.32 on Windows reports `Is directory a git repo: no` for *every* tree, including
  an ordinary clone with a real `.git` directory (`git rev-parse`, `git remote -v` and
  `git status` all work in that same directory). The exception is named here so it cannot
  quietly become the default, and it holds only while that detector is broken.
- **Branches: `<role>/<thing>`** — `recon/win-plan1`, `build/win-loader`, `hw/win-plan2`,
  `dispatch/land-win-recon1`.
- **Every change lands through a PR with a merge commit.** Never commit to `main`; never
  squash. `main` is protected.
- **Two gates, same shapes:** a deterministic `verify-pr` run in CI (a Windows port of the
  script) plus an `openscanner/verifier` verdict on the exact head commit for agent-tier
  paths. Port the tier list; do not redefine it.
- **Verdict comments put the anchored line FIRST.** One line,
  `VERDICT: MERGEABLE|NEEDS CHANGE @ <40-char head sha>`, reasoning below it. The gate accepts
  an anchored line anywhere in the comment, so this is hygiene rather than a requirement — but
  a first-line convention survives a parser that later tightens, and it is legible to a human
  skimming the thread. Exactly one verdict-shaped token, always the full 40 characters: a
  short SHA, a quoted example, or a second token makes the comment inert and the merge blocks.
  The gate fails closed at every rejection path, so an unparsed verdict never becomes a pass.
- **Vendor artifacts are never committed.** `.recon-cache/` is gitignored in both repos; the
  Windows DLLs are a different target with their own sha256 pins, recorded in
  [`FORK-COORDINATION.md`](FORK-COORDINATION.md).
- **Run records** (`tests/results/**`) keep the parent's discipline: one file per run, never
  edited in place, corrections as dated addenda.
- **Rooms stay current when idle.** A room that is behind `origin/main` is missing merged PRs,
  and its next task starts from a stale tree. So whenever a session is **not mid-turn**:
  `git fetch`, then bring the room to `origin/main` — fast-forward when the room's branch is an
  ancestor of `main`; when the branch's own commits are all landed (check with
  `git cherry -v origin/main <branch>`: every line `-`), rebase onto `main` and lease-push the
  role branch; never rebase a branch carrying an unlanded `+` commit. Never touch a room whose
  session is working, and never update a room you do not own — the owner or the dispatcher does
  it, and the dispatcher reports what it moved.

## 5. Coordination channel

The two machines talk through the OpenCode desktop app's **server list** plus
`scripts/remote-opencode.sh` (parent, PR #150). The Windows box is registered there as
`Windows11`; the script resolves the endpoint and credential from the app's state by display
name, so **neither the URL nor the credential is recorded in either repo.**

- **Who uses it: the dispatcher, not the planner.** The script's own header names its purpose —
  coordinating with "a dispatcher running on a second machine". Each side's dispatcher can read
  the other's sessions (`active`, `sessions`, `read`) and hand it work: `prompt` queues,
  `steer` exists only to stop a running agent before it commits something already known to be
  wrong.
- **Secrets.** Credentials are read at runtime and passed to `curl` through a stdin config,
  never `argv`, so they never reach `ps` or a transcript. Keep that property in anything built
  on it; never echo one, never put one in a prompt to either machine, never commit one.
- **What it is not.** Not a file channel and not an MCP server — it cannot write the other
  repo's files. That is exactly why §2's single-source rule and the coordination file's
  one-writer-per-file rule exist: the channel carries *orders and reports*, the repos carry
  *state*.
- **Conventions do not change with the channel.** A work order sent across machines has the
  same shape as a local one: the board row, the evidence, the deliverable, the wall, and what
  to report back.

## 6. Recon: the real cost

The parent's ABI claims were recovered from the macOS arm64 dylib, pinned per findings doc.
**None of that transfers silently.** A Windows build needs:

1. **The vendor's Windows artifacts.** Whether Creality ships a Windows build with the same
   `obscan_*` surface is **open question 1** — the gate on everything else. If no `obscan_*`
   DLL exists for Windows, this is not a port; it is the Orbbec-SDK path in `README.md`'s
   "Possible future direction".
2. **A fresh recon pass** against the Windows binaries — the C ABI is the same *interface*,
   but x64 MS calling convention ≠ AAPCS64, and struct layout/packing must be re-verified
   rather than assumed. A new target with new findings, not a reinterpretation.
3. **Its own pins** — sha256 per Windows artifact, in the coordination file.

Budget it as the parent's PLAN #1 was budgeted: one target per recon session.

## 7. Guardrails

The parent's hardware safety rests on a `PreToolUse` hook; on Windows that hook does not fire,
so **the fork starts with no hardware backstop at all**. Before any Windows hardware run:

- Port `guard-hw-run.sh` (or replace it) so the equivalent invariants hold: no manual
  library-path injection, no vendor-artifact edits, no listener tests outside the designated
  worktree.
- Keep the parent's hard rules verbatim — one SDK user at a time, no power-cycle shortcuts, no
  retry after a timeout, capture the exit code and any crash report.

**Hardware execution is operator-run.** The operator runs the hardware test binaries and any `scan_server` device run from Claude Code; that run is how the scanner's presence and placement are confirmed. Agents (builder, dispatcher, verifier) prepare the build, the preflight/readiness checks, the runbook, and the records, and verify the evidence — they do not launch a device test. The dispatcher must not dispatch a hardware run to a builder session. This lane has already paid for the alternative once: a learning smoke was dispatched to a builder and the operator's confirmation path was bypassed.

## 8. What the fork needs before its first build

In order, each gating the next:

1. **Answer open question 1** — does a Windows `obscan_*` binary exist?
2. A Windows toolchain decision (MSVC vs clang-cl) and a CMake configure that gets `mesh/` and
   `engine/` compiling.
3. The contract pin wired into CI (fails loudly when the header is stale).
4. A PowerShell/Python equivalent of `guard-hw-run.sh`.
5. A first Windows recon target, with its own findings doc and pins.

## Open questions

1. **Does Creality ship a Windows build of the scan SDK with the `obscan_*` surface?** The gate
   on the whole fork. (operator / vendor inspection)
2. Is the Windows target x64 only, or is Windows-on-ARM in scope? Affects the name and the
   artifact set. (operator)
3. Does the parent's `#ifdef _WIN32` loader split actually compile against a real DLL, or was
   it written blind? It has never been exercised. (builder, once an artifact exists)
4. What replaces the interpose-dylib mechanism on Windows, if the teardown crash needs the
   same containment? (builder/recon)
5. Does the fork need its own agent definitions and worktrees, or does one shared config do?
   (operator)
