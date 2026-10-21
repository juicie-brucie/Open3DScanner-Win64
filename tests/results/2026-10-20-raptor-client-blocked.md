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
- Lead: the fork deliberately excludes `tools/raptor_client` (`docs/recomp/PARITY.md`:23; `PLAN.md` `win #115`); the binary will not appear in this tree. The parent's `_WIN32` shim plus a later `tools/` sync is the only route. This is a scope exclusion, not a build omission.

## Open questions

- The target is excluded by design; the route is the parent's `_WIN32` shim plus a later `tools/` sync. No local build is owed.

## Board

- No board move. The target is excluded by design, not a build omission; the run returns when the parent's shim lands and a `tools/` sync brings it.
