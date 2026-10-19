# Windows hardware learning smoke — 2026-10-19 — FAIL

**Learning smoke, not `win #113`; no six-cell claim.**

- Date: 2026-10-19
- Worktree: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw
- Branch: hw/win-learning
- Target: build\tools\scan_server\scan_server.exe  (live smoke; listener binaries under build\tests\hardware\)
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 22:42:12 >= newest source tracking_status_test.cpp 22:41:52; daemon: running (CrealityScan.exe process; proxy, not a service check)
- Interposer: N/A for interposer (macOS-only); DLL path verified; load asserted (obscan_context_create resolved); daemon: running; path: C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll
- Run start: 2026-10-19 15:42:18

## Windows preconditions

- Build: `bcd9d0f489629b7fd29392ebe3a3d8412abdab19` (current `origin/main`; branch `hw/win-learning`).
- Toolchain: MSVC 19.44.35229 for x64, CMake 4.4.4, Ninja 1.13.2; generator Ninja; `cmake --build` 44/44.
- Device: Creality Raptor Pro, `USB\VID_2BC5&PID_0C03\CR0886100SN`, composite USB, `Port_#0009.Hub_#0001`, status OK; six `VID_2BC5` interfaces enumerated (Depth/IR/RGB + HID).
- SDK: `lib_orbbec_scan.dll` 3.5.3.260921 (vendor banner on load); the WER fault report names fault-module version 3.5.3.921.
- CrealityScan: 4.3.2 (dispatcher-reported; `CrealityScan.exe` file version 2022.3.20.6406910, Unity 2022.3.20f1). PID 24332 running before the preflight and through both listener tests; still running after the live crash.
- Mac: operator-confirmed not scanning; not observable from this host.
- Browser: **not opened**; no HUD `data`/`render` fps captured. The synthetic page was checked over HTTP only; the live worker crashed before a page session.
- Room cleanup: the pre-existing dirty `engine/src/obscan_loader.cpp` was a subset of main's merged `LoadLibraryExA` fix (same `_WIN32` branch and error form); `git diff` against main showed no unique work, so it was reset and the room checked out `bcd9d0f`.

## Commands

All from the room root, strictly one process at a time; CrealityScan stayed open.

1. `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-preflight.ps1 scan_server`
2. `build\tests\hardware\device_message_listener_test\device_message_listener_test.exe`
3. `build\tests\hardware\data_process_message_listener_test\data_process_message_listener_test.exe`
4. `build\tools\scan_server\scan_server.exe 8787` (synthetic)
5. `build\tools\scan_server\scan_server.exe --live 8787` (attempted ~35 s)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Preflight load + daemon | **PASS** — `load asserted (obscan_context_create resolved)`; `daemon: running`; exit 0 |
| 2 | Device listener test, CrealityScan open (pid 24332) | **FAIL** — preconditions met, `obscan_context_create` `OB_SCAN_ERROR_INVALID_ARGUMENT`; exit 1 |
| 3 | Data/process listener test, CrealityScan open (pid 24332) | **FAIL** — same init failure; exit 1 |
| 4 | Synthetic `GET /` | **PASS** — `200 text/html; charset=utf-8`, 3593 bytes |
| 5 | Synthetic WebSocket | **PASS** — `Open` (101 handshake) |
| 6 | Live smoke reaches serving/streaming | **PASS** - `obscan_scan_start -> OB_SCAN_SUCCESS`; 44 `FRAME_READY` lines; frames on `ws://0.0.0.0:8787` |
| 7 | Live smoke stability | **FAIL** — vendor worker crashed, `0xC0000005`, fault module `lib_orbbec_scan.dll` |
| 8 | `[perf] window` lines | **N/A** — 0 lines before the crash; no perf window emitted |
| 9 | HUD `data`/`render` fps | **N/A** — browser not opened; live crashed before a page session |
| 10 | Crash sweep (WER + Event Log 1000) | **PASS** — both sources yielded reports; archived below |

The preflight PASS above is also captured in the raw log under `=== preflight re-run for capture ===` (exit 0).

### Device listener test — exact output (CrealityScan open)

```
[launcher] no interposer on Windows: DYLD_INSERT_LIBRARIES is macOS-only and this build has no vulkan_disable_interpose. The worker runs WITHOUT an interposer; do not record this as equivalent to a Mac run in that respect.
[worker] Loading OBScan SDK from: C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll
[worker] Resolved bootstrap symbols OK.
[worker] precondition: CrealityScan.exe (RPC daemon host): running
[worker] precondition: VID_2BC5 device: enumerated
[worker] remove entry point resolved: NO (Windows: the existing loader exposes no arbitrary-symbol resolver; OQ-3 remove/re-add checks SKIPPED - not equivalent to a Mac run)
[worker] obscan_context_create() -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)
[worker] FAIL: preconditions met but obscan_context_create() failed; this is a genuine init failure.
worker exit code: 1
```

Exit code: **1**. App/daemon state: CrealityScan open (pid 24332), device enumerated. No contention error was reported; the app was not closed.

### Data/process listener test — exact output (CrealityScan open; ANSI color codes omitted)

```
[launcher] no interposer on Windows: DYLD_INSERT_LIBRARIES is macOS-only and this build has no vulkan_disable_interpose. The worker runs WITHOUT an interposer; do not record this as equivalent to a Mac run in that respect.
[worker] Loading OBScan SDK from: C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll
[worker] precondition: CrealityScan.exe (RPC daemon host): running
[worker] precondition: VID_2BC5 device: enumerated
[worker] obscan_context_create() -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)
[worker] FAIL: preconditions met but obscan_context_create() failed; this is a genuine init failure.
SDK version 3.5.3.260921
obscan_context_create
scan log dir: C:\Users\Bruce\AppData\Local\Temp\openscanner_ob_scan_log
worker exit code: 1
```

Exit code: **1**. App/daemon state: CrealityScan open (pid 24332), device enumerated. No contention error; the app was not closed and no retry was made.

### Synthetic sanity — exact result

```
GET / -> 200 text/html; charset=utf-8 bytes=3593
WS -> Open
```

The server was stopped before the live smoke; no leftover `scan_server` process.

### Live smoke — exact result

Started at ~15:42:58 (vendor `GVCPClient` bind). It reached `obscan_scan_start -> OB_SCAN_SUCCESS`, served the UI, streamed frames, then crashed:

```
[worker][data] msg_id=325189890 (FRAME_READY) data=... thread=...
[worker] obscan_scan_start                             -> OB_SCAN_SUCCESS (0)
[worker] Serving UI from: ...\ui
[worker] Frame protocol V1 (...)
[worker] Streaming live scan frames on ws://0.0.0.0:8787 (open ui/index.html)
...
[10/19 15:42:58.192450][error][14556][GVCPClient.cpp:355] sendto failed with error:10038
[10/19 15:42:59.515877][warning][4732][Pipeline.cpp:289] Pipeline source frameset queue fulled, drop the oldest frame!
[10/19 15:43:03.559148][error][9660][GVCPClient.cpp:355] sendto failed with error:10038 [**3 logs in 5366ms, last: 15:43:00.204111**]
[10/19 15:43:03.559182][warning][8708][Pipeline.cpp:289] Pipeline source frameset queue fulled, drop the oldest frame! [**30 logs in 4043ms, last: 15:43:00.515962**]
[10/19 15:43:03.559210][warning][22896][Pipeline.cpp:310] Wait for frame timeout, you can try to increase the wait time! current timeout=100
[worker][data] msg_id=325189890 (FRAME_READY) data=... thread=...
[worker][data] FRAME_READY tracking_status=1 (LOST)
[launcher] Worker crashed or timed out inside the vendor SDK (child crashed, exit code 0xC0000005).
```

- 44 `FRAME_READY` lines; 1 `tracking_status=1 (LOST)`; **0** `[perf] window` lines.
- The parent process had exited by the 35 s check; no leftover `scan_server`.
- WER: `AppName=scan_server.exe`, `AppPath=C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw\build\tools\scan_server\scan_server.exe`, fault module `lib_orbbec_scan.dll` version `3.5.3.921`, exception `c0000005`, offset `0000000000872677`, event time 15:43:01.
- HUD fps: not captured (browser not opened).

## Crash sweep

- Event log: [`2026-10-19-win-hw-learning-smoke.event.log`](./2026-10-19-win-hw-learning-smoke.event.log), sha256 `a33b1a657165032cf5b317473e3e25ff25f44aa2a2f5908347d4699991dcb131` (committed blob; Application Error, EventID 1000; faulting application `scan_server.exe`, faulting module `lib_orbbec_scan.dll` 3.5.3.921, exception `0xc0000005`).
- WER: [`2026-10-19-win-hw-learning-smoke.wer`](./2026-10-19-win-hw-learning-smoke.wer), sha256 `1b4340321c26cc3489cbbaeb192c02467206bf6970bdf7869c9d1d42944c5ce6` (committed blob); no `.hdmp` accompanied the report.
- The postflight matched both sources and archived the earliest (the event); the WER report was copied beside it as above. Neither source was unavailable, so the "sweep not performed" sentence does not apply.

## Findings / lead

- **Listener tests fail before any listener call.** Both tests pass `nullptr` for `input_algo_dependent_files_dir` because `FindStreamingAssetsDirForLibraryPath()` returns empty on Windows: `PathExists()` uses `fopen(path, "rb")` on a directory, which returns NULL on Windows. `scan_server --live` passed an empty string and created its context successfully, so the null-vs-empty value is the most likely cause of `OB_SCAN_ERROR_INVALID_ARGUMENT`. Lead: `engine/src/obscan_loader.cpp` (`PathExists` / `FindStreamingAssetsDirForLibraryPath`) or the tests' null fallback. Not fixed in this run.
- **Live worker crash inside the vendor SDK.** `lib_orbbec_scan.dll` 3.5.3.921, exception `c0000005` at offset `0x872677`, after GVCP `sendto` error 10038, a full pipeline frameset queue, a frame timeout, and tracking LOST. The WER report is archived above. Lead: vendor SDK/USB pipeline under live streaming; the crash is not in our code path by the report's fault module.
- No app-contention error was observed; CrealityScan was not closed.

## Open questions

- Does an empty (non-null) `input_algo_dependent_files_dir` let both listener tests reach their checks? (The live path suggests yes.)
- Is the `c0000005` reproducible with the app closed / a different USB port, or does it require the current app+driver state?

## Board

- No board move: this is a learning smoke, not `win #113`, and no six-cell acceptance was attempted.
