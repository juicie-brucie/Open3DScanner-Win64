# PLAN #<n> — raptor_client hardware run — BLOCKED

- Date: 2026-10-20
- Worktree: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw
- Branch: hw/win-ready
- Target: raptor_client.exe (not found — preflight blocked before any binary selection)
- Other copy: n/a — no built binary found anywhere under build\
- Preflight: `hw-preflight: FAIL - no built binary named 'raptor_client.exe' under build/ - build it first.`
- Interposer: n/a — raptor_client links no vendor SDK, exempt from interposer/vendor-artifact rules; also n/a because the run never started
- Run start: n/a — preflight blocked before run start

## Command

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-preflight.ps1 raptor_client
```

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: `2026-10-20-raptor-client-blocked.raw.log` (same directory)
- Interposer confirmation: n/a — macOS-only construct this lane cannot reproduce (see the agent doc); also moot since no run occurred

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Deterministic binary selection finds `raptor_client.exe` under `build\` | **[FAIL]** `hw-preflight: FAIL - no built binary named 'raptor_client.exe' under build/ - build it first.` |

## Crash sweep

Not run. The preflight gate blocked before any process executed, so there is
no run start timestamp and nothing to sweep for. Postflight was not invoked.

## On failure or block — observable + one lead

- Observable: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-preflight.ps1 raptor_client` exited `1` with verbatim stderr `hw-preflight: FAIL - no built binary named 'raptor_client.exe' under build/ - build it first.` No `build\tools\raptor_client\raptor_client.exe` or any `build\**\raptor_client.exe` exists in this worktree.
- Lead: likely the raptor_client target has not been built in this worktree (build system / CMake target configuration, not a runtime issue).

## Open questions

- Has `raptor_client` ever been built in this worktree, or is this the first attempt to run it here?
- Should the build step run before preflight is retried, and who owns triggering that build?

## Board

- Row move: raptor_client hardware run stays in its current "to run" / not-yet-attempted state — it cannot move to a passed/failed state because the gate never reached execution; the dispatcher should route this to whoever owns building `raptor_client` before re-dispatching the hardware run.
