# PLAN #<n> — device_message_listener_test retry (daemon now running) — PASS

- Date: 2026-10-20
- Worktree: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw
- Branch: hw/win-ready
- Target: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw\build\tests\hardware\device_message_listener_test\device_message_listener_test.exe  (build-tree copy — no build/tools/device_message_listener_test/device_message_listener_test.exe exists; first found under build/)
- Other copy: none
- Preflight: `hw-preflight: OK` — pwd OK; no conflicting process; freshness: binary 23:40:14 >= newest source main.cpp 23:39:41; daemon: running (CrealityScan.exe process; proxy, not a service check)
- Interposer: N/A for interposer (macOS-only); DLL path verified; load asserted (obscan_context_create resolved); daemon: running; path: `C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll`
- Run start: 2026-10-20 20:31:52

## Command

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-preflight.ps1 device_message_listener_test
build\tests\hardware\device_message_listener_test\device_message_listener_test.exe
```

## Result

- Exit code: `0`
- Verdict: **PASS**
- Raw log: `2026-10-20-device-message-listener-test-retry.raw.log` (same directory)
- Interposer confirmation: n/a — macOS-only construct this lane cannot reproduce (see the agent doc)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | add(NULL handle, listener) == DEADLINE_EXCEEDED | **[PASS]** `add(NULL handle, listener) -> OB_SCAN_ERROR_DEADLINE_EXCEEDED (845283591)` |
| 2 | add(handle, NULL listener) == INVALID_ARGUMENT | **[PASS]** `add(handle, NULL listener) -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)` |
| 3 | add(handle, listener_a) #1 == SUCCESS, delivers DEVICE_CONNECT on registering thread | **[PASS]** `add(handle, listener_a) #1 -> OB_SCAN_SUCCESS (0)`; `messages delivered during add: 1; callback thread id hashes=1569106733; registration thread id hash=1569106733` |
| 4 | double add (no remove) == INVALID_ARGUMENT | **[PASS]** `add(handle, listener_b) #2 (no remove) -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)` |
| 5 | no extra async messages after 2s wait | **[PASS]** `total messages observed: 1; messages after 2s wait: 1` |
| 6 | worker overall result | **[PASS]** `DONE (failures=0)`; `worker exit code: 0` |

Not run (environment-level skip, not a failure): `remove-then-add` — `[worker] skipping remove-then-add (remove symbol unavailable)`; `remove entry point resolved: NO (Windows: the existing loader exposes no arbitrary-symbol resolver; OQ-3 remove/re-add checks SKIPPED - not equivalent to a Mac run)`.

A `GVCPClient.cpp:355 sendto failed with error:10038` line appeared twice (20:32:11.048760 and a burst of 3 logs ending 20:32:13.076864/14.063254) during device/network enumeration; it did not affect any `[PASS]` check and the worker reported `failures=0`.

## Crash sweep

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-postflight.ps1 2026-10-20-device-message-listener-test-retry '2026-10-20 20:31:52' device_message_listener_test
```
Output: `no Windows crash-report source available - sweep not performed`

## Open questions

- Does the repeated `GVCPClient.cpp:355 sendto failed with error:10038` (WSAEACCES) during network-device enumeration indicate a benign secondary-NIC binding race, or is it worth recon's attention even though it did not fail any check in this run?
- Is `remove entry point resolved: NO` (and the consequent skip of remove-then-add / OQ-3) an accepted, permanent Windows limitation, or should the loader eventually expose that symbol?

## Board

- Row move: #<n> `blocked` → `pass` — daemon precondition (CrealityScan.exe) is now satisfied after the user's power cycle and restart; the retry ran to completion with `failures=0` and no crash-report artifact. Prior blocked record (`2026-10-20-device-message-listener-test-blocked.md`) left unedited.
