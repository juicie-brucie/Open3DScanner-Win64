# PLAN #<n> — device_message_listener_test preflight block — BLOCKED

- Date: 2026-10-20
- Worktree: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw
- Branch: hw/win-ready
- Target: device_message_listener_test (binary never selected — preflight failed before binary selection step was reached in observed output)
- Other copy: not observed — preflight did not reach that check
- Preflight: `hw-preflight: FAIL - daemon: NOT running - launch CrealityScan.exe before the run`
- Interposer: N/A for interposer (macOS-only); DLL path not verified — preflight did not reach that check
- Run start: n/a — preflight blocked before the target was launched

## Command

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/hw-preflight.ps1 device_message_listener_test
```

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: `2026-10-20-device-message-listener-test-blocked.raw.log` (same directory)
- Interposer confirmation: n/a — macOS-only construct this lane cannot reproduce (see the agent doc)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | CrealityScan daemon running | **[FAIL]** `hw-preflight: FAIL - daemon: NOT running - launch CrealityScan.exe before the run` |

## Crash sweep

Not performed. Preflight exited non-zero before any run start existed and before `device_message_listener_test` was launched, so there is nothing for the postflight sweep to correlate against.

## On failure or block — observable + one lead

- Observable: command `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/hw-preflight.ps1 device_message_listener_test` exited with code `1` and printed verbatim: `hw-preflight: FAIL - daemon: NOT running - launch CrealityScan.exe before the run` (plus an informational `SDK version 3.5.3.260921` line). No binary was selected, no freshness check ran, no target process was launched.
- Lead: CrealityScan.exe daemon is not running on this host — this is an environment/setup precondition, not a code defect in device_message_listener_test.

## Open questions

- Is CrealityScan.exe expected to be running persistently on this host, or does it need to be started before each hardware run?
- Does starting CrealityScan.exe require device/user action that this agent is not permitted to take unilaterally?

## Board

- Row move: none proposed — this run did not reach a pass/fail of the target; dispatcher should decide whether this blocks the row or simply needs the daemon started before re-dispatch.
