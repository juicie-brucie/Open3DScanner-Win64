# Windows live-rate hardware plan (first Windows hardware run)

*Prepared 2026-10-19 by the Windows dispatcher. This is a run procedure, not a
finding: it says how to take the measurement and what to write down. Evidence
lives in `tests/results/**` after the run; semantics live in
`docs/recon/findings/**`.*

## The question

The parent's UI reports **2–8 data fps** while the session requested 50 fps on IR
and depth and the device polled at ~51.2 Hz, against a vendor claim of up to
60 FPS for blue-laser modes. Three things are tangled in that gap and this run
separates them:

1. the device/profile rate actually offered,
2. the vendor pipeline's publish cost,
3. our own tick and broadcast cost, and the page's render cost.

The same workload runs on the parent's machines, so the numbers are only useful
if this procedure matches theirs.

## Preconditions — do not start until all are true

| # | Check | How |
| --- | --- | --- |
| 1 | Build present and current | `cmake --build <build> --parallel` completes; `scan_server.exe` exists |
| 2 | The page is served | `GET http://127.0.0.1:<port>/` returns **200** with `Content-Type: text/html` — *not* a zero-byte close |
| 3 | WebSocket streams | an upgrade on `/` returns **101** and frames arrive |
| 4 | Guard armed | the hardware rules are scoped to this room (`-wt-hw` in the path); a listener test outside it is blocked |
| 5 | Device attached | `Get-PnpDevice -PresentOnly` shows `VID_2BC5` interfaces (Depth/IR/RGB) |
| 6 | Daemon up | on Windows the RPC daemon is started by `CrealityScan.exe` — launch the app; there is no separate service |
| 7 | Interposer | **N/A for interposer (macOS-only)** — the Vulkan interposer is a macOS-only mechanism and this lane cannot reproduce it; the preflight instead asserts the loader's vendor DLL path `C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll` exists |

If 2 or 3 fails, the tree is behind the parent (see PLAN #115) and the run would
compare a number against nothing.

### Attach sequence — one SDK user at a time

The scanner is a single-client device; a second SDK user is the hard error. Do
this in order, before starting:

1. Confirm the Mac is not mid-run — no live session and no hardware test in
   progress.
2. Move the USB cable from the Mac to this Windows machine.
3. Attach the scanner and confirm it enumerates: `Get-PnpDevice -PresentOnly`
   shows `VID_2BC5` interfaces.
4. Confirm the daemon state: launch `CrealityScan.exe` so its RPC daemon is up;
   there is no separate service.

## Commands

Build (a shell that has called `vcvars64.bat`; nothing here is on PATH):

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul && C:\Users\Bruce\tools\cmake\bin\cmake.exe -S <repo> -B <build> -G Ninja -DCMAKE_MAKE_PROGRAM=C:\Users\Bruce\tools\ninja\ninja.exe && C:\Users\Bruce\tools\cmake\bin\cmake.exe --build <build> --parallel"
```

Run — **from the repo root**, because the server finds `ui/` relative to the
working directory, and the port is positional (there is no `--synthetic` flag;
synthetic is the default mode, live is `--live`):

```powershell
# synthetic sanity first (no device), then the real run
<build>\tools\scan_server\scan_server.exe 8787
<build>\tools\scan_server\scan_server.exe --live 8787
```

Then open `http://127.0.0.1:8787/` in the page's browser for condition (b).

## The six conditions

Two client conditions × three phases, **30 s each**, six cells total:

| | still | moved by hand | paused / resumed |
| --- | --- | --- | --- |
| **(a) no client connected** | 30 s | 30 s | 30 s |
| **(b) UI open** | 30 s | 30 s | 30 s |

"Paused/resumed" means: pause mid-phase, hold ~5 s, resume, continue to 30 s.

## What to record, per cell

Record these **as the run happens**, into the raw log alongside the record:

- **configured profile** — the `start_stream` parameters actually sent
  (`[6,9,1920,1200,50]` / `[7,9,1920,1200,50]` are the parent's observed values;
  record ours even if identical).
- **raw delivery cadence** — `get_frame` timing, not the post-processing rate.
- **our tick breakdown** — the `[perf] window` lines from `scan_server`: pose,
  publish-getter, our post-processing+encode, broadcast, plus points/bytes and
  the realized tick period.
- **the UI** — the page's `data` and `render` fps, read from the HUD.
- **machine spec** — Ryzen 9 5900X 12C/24T, 31.9 GB, Windows 11 Pro 26200,
  RTX 2080 Ti (11 GB; WMI's `AdapterRAM` caps at 4 GB and under-reports).

### Crash sweep — Windows (after the run)

Crash reports are part of the record. Windows has no `.ips`; sweep both sources:

- **WER** — `C:\ProgramData\Microsoft\Windows\WER\ReportArchive`: the `.wer` and
  the optional `.hdmp`.
- **Event Log** — `Application Error`, EventID 1000.

Match a report on its own `AppName`/`AppPath` and event time — **never on
filenames**. Archive what is found beside the record (`<slug>.crash.wer` plus
`<slug>.crash.hdmp`, or `<slug>.crash.event.txt`) and cite it from the record.
If neither source yields a report for the run's time window, the record must
print exactly:

`no Windows crash-report source available - sweep not performed`

Never write "no crash".

## Where the output goes

- **Record**: `tests/results/<date>-win-live-rate-<slug>.md` — one file per run,
  never edited in place; corrections are dated addenda that name the file.
- **Raw log**: committed beside the record, in the same PR. The gate's
  `record-provenance` check requires an added record to be accompanied by its
  raw evidence in the same change; a record without one is a claim without proof.
- Both are **agent tier**: the PR needs a verifier verdict naming the exact head
  commit before it merges.

### Record header and verdicts

Start every record with these fields, in this order (they are compared across
machines):

- Date:
- Worktree:
- Branch:
- Target:
- Other copy:
- Preflight:
- Interposer: N/A for interposer (macOS-only); DLL path verified: <the path preflight printed>
- Run start:

Use `PASS`, `FAIL`, `SKIP`, or `N/A` for each check and the run verdict; an
`N/A` is never blank — it names the reason and what was checked instead.

## Reconcile — after the record lands

Before this run, and required by the planner's roadmap:

1. Read the record against the board's statuses and `docs/recon/findings/**`.
2. Propose row moves and tag changes as a board PR — the dispatcher moves rows,
   the record does not move itself.
3. A tag is cleared by a run, never by argument: `[inferred]` becomes
   `[hw-confirmed: <run>]` only where the record actually shows it.
4. Anything the run raises but does not answer becomes a new row, not a footnote.

The automated form of this is PLAN #114; until it exists, this is the manual
procedure and it is not optional.

## Known limits, stated up front

- **60 fps is unobserved.** The parent has never seen a 60 fps profile; only the
  requested 50. The vendor UI's own selector is the direct check, and it needs
  the device attached and the app open. Do not report 60 fps as available on the
  strength of the vendor's advertising.
- **The Windows tree's divergence is recorded as a diff** (PLAN #115), so a
  number that differs from the parent's is first suspected as a tree difference,
  not a platform one.
- **One device user at a time.** If the Mac is running a hardware test, this run
  does not start — a second SDK user on the same device is the hard error.
