# PLAN #123 — Windows live-crash isolation (operator runbook)

**Learning isolation runs, not `win #113`; no six-cell claim.** The operator runs
these from Claude Code; agents prepare the tree/build and verify the evidence,
never the device.

## Preconditions — do not start until all are true

1. Device enumerated: `Get-PnpDevice -PresentOnly` shows `VID_2BC5` Depth / IR /
   RGB interfaces. **An unenumerated device is a precondition failure, not a
   test result.**
2. `CrealityScan` is open (`Get-Process -Name CrealityScan`); it is the RPC
   daemon host (proxy check, not a service query).
3. One SDK user: no `scan_server`, `device_message_listener_test`,
   `data_process_message_listener_test`, or `live_capture_test` is running.
4. The operator has scheduled the run and no other SDK user is active.
5. Tree/build current: room `Open3DScanner-Win64-wt-hw`, branch `hw/win-ready`;
   `build\tools\scan_server\scan_server.exe` exists.
6. The preflight passes **load asserted** + `daemon: running` (below). The
   preflight does not check the device; precondition 1 does.

## Knobs found in the live path

All live-path knobs are in `tools/scan_server/main.cpp`; the ABI header is
`engine/include/openscanner/obscan_api.h`.

| Variable | Where / exact knob | Per-run change |
| --- | --- | --- |
| Session length | `main.cpp:1614-1629`: `--live [port] [library_path] [session_seconds]` | pass `60` as the third positional |
| Profile / fps | `main.cpp:1004-1017` builds `ObScanConfig`; `:1006` `frame_rate = OB_SCAN_FRAME_RATE_AUTO`; enum `obscan_api.h:136-139` (`AUTO`, `STANDARD`, `BALANCED`, `FAST`); `:1007` projection, `:1008` scan mode, `:1009` accuracy, `:1011` FOV, `:1015-1016` voxels | **hardcoded on the main-room build; no CLI flag.** Minimal C variant: change `:1006` to `OB_SCAN_FRAME_RATE_STANDARD` (or `BALANCED`) and rebuild. On the variant branch it is a flag instead — `--frame-rate`, see the variant note below — so no rebuild is needed there. The parent's `start_stream [6,9,1920,1200,50]` vector is not used by this server; there is no resolution field in `ObScanModeConfig`. |
| Exposure / laser | `main.cpp:1028` `obscan_scan_config_combined_exposure_and_laser_current(scan, 900)`; range/default `:459-461` (1–1000, default 900); UI command `set_exposure` `:507-517`, e.g. `{"cmd":"set_exposure","auto":true}` or `{"cmd":"set_exposure","value":N}`; default manual 900 at `:1090-1093`; re-assert `:1187-1193` | UI command; no CLI. The separate `config_ir_exposure_time` / `config_color_exposure_time` / `config_laser_current` setters (`obscan_api.h:736-738`) are not used by this server. |
| Frameset / queue budget | no setter in the ABI; only the `FRAME_BUFFER_FULL` message (`obscan_api.h:492`). The vendor queue is internal. `main.cpp:1246` `kLiveBroadcastPointBudget=20000` is the UI broadcast budget, not the vendor frameset. | **not exposed; do not improvise a vendor call.** |
| Start cadence | `main.cpp:702-704` `kDeviceWaitAttempts=30`, `kDeviceWaitIntervalMs=200`, `kWantStableFrames=10`; wait loop `:1031-1035`; `obscan_scan_start` `:1041-1042` | hardcoded on the main-room build: a slower start is a code change (raise `kWantStableFrames` or add a `sleep_for` before `:1041`) and rebuild. On the variant branch it is the `--slow-start` flag (5 s before `obscan_scan_start`), no rebuild. **Separate variable from C** — its own run (C2), never combined with a frame rate. |
| Tick / poll | `main.cpp:715` `kLiveTickIntervalMs=100` | hardcoded; our polling, not the vendor pipeline |

## Runs — one variable per run, 60 s each

Baseline first. Do not combine variables. Use a new record and a new raw log for
every run.

| Run | Variable | How |
| --- | --- | --- |
| Baseline | current port/cable, default profile (as built), app open | 60 s live smoke |
| A | different USB port | direct rear I/O, **not a hub**; label the port in the record |
| B | different cable, if one is available | label the cable; otherwise record "no second cable" |
| C | lower-bandwidth profile (frame rate) | the **variant binary** (see the variant note below), built from `build/win-isolation-variant` in a **separate worktree and build dir** so the main hw room and its baseline binary stay undisturbed: `--frame-rate standard` or `--frame-rate balanced`. The frame rate is the only variable |
| C2 | slower start | the **variant binary**, same separate worktree and build dir: `build\tools\scan_server\scan_server.exe --live 8787 "" 60 --slow-start` (5 s before `obscan_scan_start`). One variable, and the same stop rule as every other row — a run that streams without crashing is a candidate configuration |
| D | auto-exposure / frameset budget | D is only half exposed: the default is already no auto-exposure (manual 900). The one D variable that exists is auto-exposure via the UI `set_exposure` (`auto:true`, or a different manual value). The frameset-budget half is **not exposed** by the ABI; record that and do not fake it. |

**Variant (confirmed).** If the USB port and cable runs (A, B) fail, the
lower-bandwidth profile is a **command-line flag**, not a rebuild: branch
`build/win-isolation-variant` @ `46dcf0b` (built 44/44 in a fresh separate dir)
adds two levers to `scan_server`:

- `--frame-rate auto|standard|balanced|fast` — selects
  `ObScanModeConfig.frame_rate`. The default `auto` is the pre-existing
  behaviour; an unknown value is refused (exit 2).
- `--slow-start` — waits 5 s before `obscan_scan_start`; default off.

So run C is — **frame rate only**:

```powershell
build\tools\scan_server\scan_server.exe --live 8787 "" 60 --frame-rate standard
build\tools\scan_server\scan_server.exe --live 8787 "" 60 --frame-rate balanced
```

`--slow-start` is a **separate run** (C2, in the table below), never combined
with a frame rate: one variable per run, or the survivor is unattributable.

The launcher passes both levers to the worker through the environment
(`OPENSCANNER_FRAME_RATE`, `OPENSCANNER_SLOW_START`) and sets them explicitly,
so an inherited shell value cannot change a default run; the worker's positional
argv contract is unchanged. The two levers are **never combined in one run** — one is the variable, the other stays at its default. The launcher and the worker each print
`isolation: frame_rate=<name> slow_start=<on|off>` — **record that line**, it is
the proof of which variant ran. The tick cadence, the teardown, the vendor calls
and the six-cell measurement are untouched by the variant.

**Fork-only experimental — do not merge.** The variant is a change over a
**parent-synced** file (`tools/scan_server/main.cpp`) and is experimental: it
must not be merged without a parent-sync decision. The **baseline binary stays
the main-room build**; the variant is built in its own worktree and its own
build dir, so the main hw room and its baseline binary are undisturbed. (The
branch also carries stale copies of the board and the runbooks, because it was
cut before the last board merge — rebase it before any PR, and never take the
baseline binary from it.)

The **baseline ladder stays first, in order A, B, C, C2, D**: the variant is only
reached if the port and cable runs fail, so that each run changes one variable.

## Per run

1. **Preflight**, from the room root:
   `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-preflight.ps1 scan_server`
   It must print `load asserted (obscan_context_create resolved)` and
   `daemon: running`, exit 0. If it fails, stop; that is the finding.
2. **Run** from the repo root (the server finds `ui/` relative to cwd):
   ```powershell
   build\tools\scan_server\scan_server.exe --live 8787 "" 60
   ```
   The empty library path is deliberate: the server resolves the installed
   vendor DLL itself. **Let it exit on its own** — the bounded session is the
   clean-exit mechanism (`main.cpp:707-713`: *"Let it finish on its own; don't
   pkill it"*): the worker stops streaming after the session and runs the
   normal pause/stop/destroy teardown. **Do not `taskkill /F` an isolation
   run**, and record the launcher's and the worker's exit code as the teardown
   result. Capture the full stdout/stderr to
   `tests/results/<date>-win-iso-<slug>.raw.log` (append, do not rewrite).
3. **Observe / record** in the new `tests/results/<date>-win-iso-<slug>.md`:
   - the eight header fields (see `tests/results/_TEMPLATE.md`);
   - preflight's exact `load asserted` / `daemon` lines and exit code;
   - app/daemon state before and after (CrealityScan PID; whether it survived);
   - USB port and cable label; build variant (baseline or C);
   - `FRAME_READY` count: count the two-line form. The first smoke's log had
     **44 `FRAME_READY` lines = 22 events** (22 data-message lines + 22
     tracking-status lines). Record both numbers.
   - every `[perf] window` line (the first smoke had **0** before the crash);
   - the crash text, exit code, and the WER/Event-Log signature: the baseline
     signature is `0xC0000005` in `lib_orbbec_scan.dll` 3.5.3.921 at offset
     `0x872677`, after GVCP `sendto` 10038 / queue full / frame timeout /
     tracking LOST (see `tests/results/2026-10-19-win-hw-learning-smoke.md`).
4. **Crash sweep** after the run:
   `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-postflight.ps1 <slug> '<run start>' scan_server`
   Archive the `.wer` (and `.hdmp` if present) or `.event.log`; cite the
   committed-blob sha256, not the working-file hash. If neither source yields a
   report, quote exactly:
   `no Windows crash-report source available - sweep not performed`
   Never write "no crash".
5. **New record only.** Never edit a prior record; corrections are dated
   addenda naming the record.

## Stop conditions

- A 60 s run that streams without crashing is the **candidate configuration** for
  `win #113`; record the port/cable/profile and stop.
- If every configuration crashes with the same signature, `win #123` is a **hard
  vendor blocker**; `win #113` waits.
- No retry after a timeout. A clean failure is a result.
- Do not combine variables; if a run changes two, it answers neither.

## Size-less framesets

The first smoke's **44 `FRAME_READY` lines (22 events) carry no payload size**
(the log records id/pointer/thread only; the callback releases the frame). A
payload-size log is a separate loader addition after #133 — the #122 C option
was not taken because the loader does not resolve
`obscan_frame_get_frame_cloud` and the cloud is a borrowed vendor buffer. Do not
infer sizes from this procedure.

## Who runs it

The operator, from Claude Code. Agents prepare the tree/build and verify the
evidence; they do not run device tests, preflight against hardware, or
`scan_server` live/synthetic.
