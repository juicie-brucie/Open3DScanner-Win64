# hw-guard — the OpenCode hardware backstop (Windows fork)

The parent repository's hardware safety is a Claude Code **PreToolUse hook**
(`.claude/hooks/guard-hw-run.sh`) that blocks a shell command by exiting `2`.
That hook **does not fire on this platform** — it is a bash script, and this is
OpenCode. `WINDOWS-FORK.md` §7 records the consequence: the fork starts with no
hardware backstop at all. This is the equivalent, and it is what §8 item 4 asks
for.

**Do not mistake this for the macOS hook.** It is a different mechanism, in a
different language, with a different (smaller) coverage surface. This file states
both.

## Mechanism, and why

| Layer | File | Role |
| --- | --- | --- |
| Plugin (primary, room-scoped) | `.opencode/plugins/hw-guard/` (`index.ts` → `guard.mjs`) | registers `ctx.tool.hook("execute.before", …)` and **throws** to abort a tool call |
| Permission rules (path-only backstop) | `opencode.jsonc` `permissions` | deny `read`/`edit` of a vendor *path* — safe repo-wide because it fires on the artifact, never on prose |

OpenCode V2 loads `.opencode/plugins/**/index.ts` automatically. The plugin
shape is a plain `{ id, setup }` object, so nothing here imports
`@opencode/plugin` — the same default export is importable by the offline test
with no dependencies installed.

Blocking is the documented V2 path: the "Migrate plugins from V1" guide shows the
V1 `tool.execute.before` `throw new Error(...)` becoming a V2
`ctx.tool.hook("execute.before", …)` that throws. `guard.mjs`'s `setup()`
registers exactly that; the test invokes the captured hook, which is the
"actually invoked" evidence — a guard that never runs is worse than none.

## Scope: the hardware room — and why

The guard is **room-scoped**. The hardware rules fire only when the command's
working directory is the hardware worktree (its path contains the room token,
`wt-hw` by default; see `DEFAULT_ROOM` / the `room` option). Outside that room the
plugin is **inert except for one universal rule** (below).

The deciding argument is not "fewer false positives". A text-level rule applied
to a session that *writes prose* teaches the agent to paraphrase around it — and
a guard everyone routes around reads as protection while enforcing nothing. When
the dispatcher started dodging the matcher by rewording, the signal had already
stopped meaning anything. Scoping by the room removes the false positives *by
construction* for the dispatcher, builder and verifier rooms, so nobody has to
route around it.

**What stays universal:** the listener-test rule. It is *defined* by being outside
the room — running the device test outside its worktree is the one thing wrong
everywhere — so it cannot be gated on the cwd being the room. It fires on an
**unquoted** mention of the listener binary (an invocation), not on the name in
prose.

**The broader wall is not this guard's job.** The clean-room wall (no vendor
reads, no REA tools outside recon) is carried by per-role permission denies in
each agent definition, which fire on the *actual artifact* and never on prose
about it. That is the right enforcement point, and it is already how this repo
works. This plugin is for run integrity against the device.

## What it covers

**Inside the hardware room** (cwd = the room), three invariants, strict:

1. **No manual module/library resolution** — `SetDllDirectory`,
   `AddDllDirectory`, `SetDefaultDllDirectories`, `LoadLibrary`/`LoadLibraryEx`,
   `DYLD_INSERT_LIBRARIES`, `LD_PRELOAD`; and a **module/interposer in the
   directories a `PATH` write prepends** (only the assignment's RHS is inspected,
   so prepending tool dirs is allowed).
2. **No reading or editing vendor artifacts** — `.recon-cache`, `libOrbbecSDK`,
   `lib_orbbec_scan` / `liborbbec_scan`. For `read`/`edit`/`write`/`patch` the
   input IS the path. For a shell command the guard fires on an **operation** on
   the artifact (an unquoted path-like vendor operand, or a file op / redirection
   whose operand is exactly a vendor path); NAMING an artifact in prose is not
   access.
3. **No listener tests outside the worktree** — universal, as above.

**Outside the hardware room**, only the listener rule applies.

## What it does NOT cover (known gaps — read before trusting it)

- **Outside the room, the hardware rules are inert by design.** A `SetDllDirectory`
  call or a `.recon-cache` read in a non-hardware room is not stopped by *this*
  plugin — the clean-room wall is the per-role permission denies, and a session
  that merely discusses the device is doing nothing wrong. If a rule should hold
  in every room, it belongs in those permissions, not here.
- **Subtle DLL shadowing.** A `PATH` write is blocked only when the directories
  it PREPENDS name a module/interposer (`.dll`/`.so`/`.dylib` / `interpos`). A
  prepend of a directory that merely shadows a same-named DLL, without naming
  one, is **not** caught. Deliberate: blocking every `PATH` write would fire on
  `vcvars64.bat` and every toolchain setup.
- **A vendor name inside quoted prose is not an operand.** The shell rule fires
  on an unquoted path-like vendor operand, or on a file op/redirection whose
  operand IS exactly a vendor path (quoted or not). A prose string that merely
  contains the name is allowed.
- **The room must be named.** The token defaults to `wt-hw`; when the Windows
  hardware worktree exists its path must contain it, or the plugin's `room`
  option must be set to match. Until then, the hardware rules never fire.
- **Only OpenCode-routed tool calls.** The guard inspects the arguments of the
  tool call once. Anything a spawned process does afterwards — a script that
  re-exports `PATH`, a Makefile that calls `SetDllDirectory` — is not visible.
  MCP/custom tools whose argument names are not in `candidateTexts()` are not
  inspected.
- **The parent's fourth rule is not ported.** `guard-hw-run.sh` also treats
  `obscan_api.h` as recon-only; that is a lane rule enforced by agent
  permissions, not by this guard.
- **Not a substitute for the macOS hook**, and not a claim of parity.

## Test

`tools/hw_guard_test/run.mjs` drives a matrix of cases through **both** layers —
the pure evaluator, and the real `execute.before` hook captured from `setup()` —
with the **room in/out dimension visible** in every case name (`IN-ROOM block`,
`IN-ROOM allow`, `OUT-OF-ROOM block`, `OUT-OF-ROOM allow`). It also checks the
`opencode.jsonc` backstop is path-only (vendor path rules, no repo-wide shell
rules).

No Node is on this machine's PATH. Run it with the Node bundled in VS Code:

```powershell
$env:ELECTRON_RUN_AS_NODE="1"
& "C:\Users\Bruce\AppData\Local\Programs\Microsoft VS Code\Code.exe" tools\hw_guard_test\run.mjs
```

Exit `0` = every case behaves as specified; `1` = a mismatch.
