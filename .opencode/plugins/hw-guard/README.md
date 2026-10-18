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
| Plugin (primary) | `.opencode/plugins/hw-guard/` (`index.ts` → `guard.mjs`) | registers `ctx.tool.hook("execute.before", …)` and **throws** to abort a tool call |
| Permission rules (backstop) | `opencode.jsonc` `permissions` | deny rules Core enforces even if the plugin is disabled |

OpenCode V2 loads `.opencode/plugins/**/index.ts` automatically. The plugin
shape is a plain `{ id, setup }` object, so nothing here imports
`@opencode/plugin` — the same default export is importable by the offline test
with no dependencies installed.

Blocking is the documented V2 path: the "Migrate plugins from V1" guide shows the
V1 `tool.execute.before` `throw new Error(...)` becoming a V2
`ctx.tool.hook("execute.before", …)` that throws. `guard.mjs`'s `setup()`
registers exactly that; the test invokes the captured hook, which is the
"actually invoked" evidence — a guard that never runs is worse than none.

The permission rules are the static half. They cannot see the working
directory, so the cwd-sensitive listener rule is plugin-only (a static rule
would block a legitimate in-room run). Deny rules are durable: saved approvals
never override a configured `deny` (OpenCode V2 Permissions reference).

## What it covers (three invariants, unchanged in meaning)

1. **No manual module/library resolution** — blocks `SetDllDirectory`,
   `AddDllDirectory`, `SetDefaultDllDirectories`, `LoadLibrary`/`LoadLibraryEx`
   (the Windows analogues of `DYLD_INSERT_LIBRARIES`), plus `DYLD_INSERT_LIBRARIES`
   and `LD_PRELOAD` so the guard travels; and a `PATH` write that also names a
   module/interposer (DLL shadowing).
2. **No reading or editing vendor artifacts** — `.recon-cache`,
   `libOrbbecSDK`, `lib_orbbec_scan` / `liborbbec_scan`, for shell commands and
   for the `read`/`edit`/`write`/`patch` tools.
3. **No listener tests outside the designated worktree** — a shell command that
   runs `*message_listener_test*` is blocked unless the working directory, or
   the command itself, names the room. The room token is `wt-hw` by default
   (matching the parent's `Open3DScanner-wt-hw` naming), override via the plugin
   option `room`.

## What it does NOT cover (known gaps — read before trusting it)

- **Subtle DLL shadowing.** A `PATH` write is blocked only when the same command
  also names a module/interposer (`.dll`, `interpos`, `orbbec`, `obscan`). A
  command that merely prepends a directory containing a same-named DLL, with no
  such token, is **not** caught. This is deliberate: blocking every `PATH` write
  would fire on `vcvars64.bat` and every toolchain setup, and a guard that blocks
  ordinary builds gets disabled by whoever hits it first.
- **Invariant 3 is plugin-only.** The `opencode.jsonc` backstop deliberately has
  no listener-test rule, because a static rule cannot distinguish an in-room run
  from an out-of-room one.
- **Only OpenCode-routed tool calls.** The guard inspects the arguments of the
  tool call once. Anything a spawned process does afterwards — a script that
  re-exports `PATH`, a Makefile that calls `SetDllDirectory` — is not visible.
  MCP/custom tools whose argument names are not in
  `candidateTexts()` (`command`, `filePath`, `path`, …) are not inspected.
- **The parent's fourth rule is not ported.** `guard-hw-run.sh` also treats
  `obscan_api.h` as recon-only. That is a *lane* rule (the header is single-
  sourced in the parent), enforced by agent permissions here, not by this guard.
  Reading the header is allowed; editing it is a review concern, not a block.
- **Not a substitute for the macOS hook**, and not a claim of parity. Where the
  parent's rule is exact (`DYLD_INSERT_LIBRARIES=`), the Windows analogue is a
  set of heuristics; they are listed above rather than asserted as complete.
- **Requires the room to be named.** On Windows the designated hardware room
  does not exist yet (no hardware run is permitted until this guard lands). When
  it does, its path must contain `wt-hw`, or the plugin's `room` option must be
  set to match.

## Test

`tools/hw_guard_test/run.mjs` drives a matrix of must-block / must-allow cases
through **both** layers — the pure evaluator, and the real `execute.before` hook
captured from `setup()` — and checks the `opencode.jsonc` backstop carries every
invariant token, so the two layers cannot drift apart.

No Node is on this machine's PATH. Run it with the Node bundled in VS Code:

```powershell
$env:ELECTRON_RUN_AS_NODE="1"
& "C:\Users\Bruce\AppData\Local\Programs\Microsoft VS Code\Code.exe" tools\hw_guard_test\run.mjs
```

Exit `0` = every case behaves as specified; `1` = a mismatch.
