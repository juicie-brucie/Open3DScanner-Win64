# PLAN #<n> — scan_server default (synthetic) websocket smoke — PASS

- Date: 2026-10-20
- Worktree: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw
- Branch: hw/win-ready
- Target: build\tools\scan_server\scan_server.exe  (build-tree copy (fixed order: build/tools/scan_server/scan_server.exe))
- Other copy: none
- Preflight: `hw-preflight: OK` — pwd OK; no conflicting process; freshness: binary 23:40:14 >= newest source tracking_status_test.cpp 22:41:52; daemon: running (CrealityScan.exe process; proxy, not a service check)
- Interposer: N/A for interposer (macOS-only); DLL path verified; load asserted (obscan_context_create resolved); daemon: running; path: `C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll`
- Run start: 2026-10-20 20:33:29

## Command

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-preflight.ps1 scan_server
build\tools\scan_server\scan_server.exe
```

No arguments were passed, so `scan_server` ran in its default mode (synthetic point-cloud generator broadcast over WebSocket) rather than `--live`. It does not touch the device or the vendor SDK's scan path in this mode.

## Result

- Exit code: `1` (after forced termination — see below)
- Verdict: **PASS**
- Raw log: `2026-10-20-scan-server-websocket-smoke.raw.log` (same directory)
- Interposer confirmation: n/a — macOS-only construct this lane cannot reproduce (see the agent doc)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | process starts, binds the WebSocket server | **[PASS]** `Streaming synthetic point-cloud frames on ws://0.0.0.0:8787` |
| 2 | sustained operation, no error/warning in perf telemetry | **[PASS]** 52 consecutive `[perf]` lines over ~260 s, steady `rate=~24/s`, no error or warning lines anywhere in the log |
| 3 | process survives the full observed window without crashing | **[PASS]** no crash, no exception text, no unexpected exit before termination was requested |
| 4 | clean shutdown path exercised | **[NOT OBSERVED]** `taskkill` without `/F` reported `This process can only be terminated forcefully (with /F option)` — no console attached to deliver Ctrl+C/SIGINT through this harness, so the graceful-teardown path (`tools/scan_server/main.cpp`'s SIGINT/SIGTERM handling, per board #33) was not exercised; termination was `taskkill /F`, and the process's own exit code under that path was `1` |

## Crash sweep

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-postflight.ps1 2026-10-20-scan-server-websocket-smoke '2026-10-20 20:33:29' scan_server
```
Output: `no Windows crash-report source available - sweep not performed`

## Open questions

- Was this run meant to exercise `--live` (actual device path through the vendor SDK) rather than the default synthetic generator? The task did not specify a flag, and the default mode never calls into `lib_orbbec_scan.dll`'s scan path.
- Is exit code `1` after `taskkill /F` the expected observable for a forced kill, or does `scan_server` have a documented clean-exit code reachable only via a real console SIGINT/SIGTERM that this bash harness cannot deliver?

## Board

- Row move: none identified — no open board row names a Windows `scan_server` hardware run specifically; `win #113` (no Windows hardware run has ever executed) remains the umbrella row and is unaffected by this synthetic-mode run, since it did not exercise the live/device path that `win #113`'s six cells require.
