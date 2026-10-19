# Windows live-crash app-state isolation — 2026-10-19 — SKIP (blocked)

**Learning experiment, not `win #113`; no six-cell claim.**

- Date: 2026-10-19
- Worktree: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw
- Branch: hw/win-appstate
- Target: build\tools\scan_server\scan_server.exe  (not run)
- Other copy: none
- Preflight: N/A — experiment blocked before preflight (no `VID_2BC5` device enumerated)
- Interposer: N/A — not run
- Run start: 2026-10-19 15:55:08 (first state check)

## Premise

The dispatcher's state said the Creality Raptor Pro (`CR0886100SN`, Depth/IR/RGB) was attached and CrealityScan was open. At the first check, the app was open but the device was **not enumerated**: `Get-PnpDevice -PresentOnly` returned 0 `VID_2BC5` interfaces. The registry still held the six nodes (`USB\VID_2BC5&PID_0C03\CR0886100SN` and the five MI/HID children), all **Present=False**. The condition persisted after the app was closed and reopened. No cable move, power-cycle, driver change, or vendor update was performed.

## Phases and app/daemon state

1. **App open (15:55:08).** `CrealityScan` pid 24332, responding, started 15:39:36, main window titled `CrealityScan`; `VID_2BC5` present: **0**.
2. **Graceful UI close.** `CloseMainWindow()` returned true, but after 15 s the process and its main window were still there (pid 24332, responding); no device appeared. The app ignored the graceful close.
3. **Forced close / daemon survival.** `Stop-Process` ended pid 24332; no `Creality`/`Orbbec` process remained. The daemon host is therefore the CrealityScan process itself: an app-closed state cannot keep the daemon alive.
4. **Reopen.** CrealityScan reopened as pid 6476, responding, main window titled `CrealityScan`; `VID_2BC5` present: **0**.
5. **App-closed live trial.** **Not run.** The device was absent, and the daemon died with the app, so the app-closed state is not viable. Per the order's step 6, the experiment stopped here; no `scan_server --live` run, no crash reproduction.

## Comparison to the first smoke

The first learning smoke (`tests/results/2026-10-19-win-hw-learning-smoke.md`, PR #31) ran with six `VID_2BC5` interfaces present and the app open; it streamed 45 `FRAME_READY` messages and then the vendor worker crashed (`lib_orbbec_scan.dll` 3.5.3.921, `0xC0000005` at `0x872677`, WER archived).

This attempt never reached the live path: **0** `VID_2BC5` interfaces at the start, after the app close, and after reopen. There is no app-closed crash result to compare. The app-contention-vs-vendor/USB question remains **unresolved**; the one-variable trial needs the scanner enumerated first.

## Findings

- **Blocker: the scanner is not attached/enumerated on this host now.** All six `VID_2BC5&PID_0C03` nodes are Present=False while CrealityScan is running; the registry retains the serial `CR0886100SN`. This contradicts the dispatcher's "attached" state and is the reason the experiment could not run. The physical link/device state needs the operator (no cable move or power-cycle was made here).
- **The daemon is app-hosted.** A graceful close was ignored; a forced process close left no Creality/Orbbec process. There is no separate daemon host to keep alive across an app-closed trial.
- **No crash in this window.** No live run was performed, so there is no new WER/Event-Log report; the forced UI close was a process termination, not a crash. No crash-sweep archive is attached.
- **The first smoke's crash is not reclassified.** This experiment neither reproduced nor excluded vendor/USB causation; it could not hold the app-closed variable because the device was absent.

## Open questions

- Is the device physically reconnected/awake? (`Present=False` for the whole test window.)
- If the device returns, does the app-closed state even permit `scan_server --live` without the app-hosted daemon, or must the app remain open?

## Raw log / record provenance

- Raw transcript: [`2026-10-19-win-hw-learning-appstate.raw.log`](./2026-10-19-win-hw-learning-appstate.raw.log)
- No WER/event archive: no live run and no crash.

## Board

- No board move: blocked learning experiment, not `win #113`.
