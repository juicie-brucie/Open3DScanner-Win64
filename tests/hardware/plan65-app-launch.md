# PLAN #65 — `A3`'s live launch (the app lifecycle)

**Goal**: row #65's acceptance — the app, launched for real. Four observations, plus A4's steps 5-6:

> **#65**: "lifecycle: spawn and supervise the worker, surface a crash, quit = graceful
> `pause`/`stop`/`destroy`."

## Why this run exists

A3 landed as PR #185 (verdict MERGEABLE at `7a8885c`), but that verdict **never launched it** —
the cold-launch retry is verified by its delay arithmetic and callback wiring only, not by a
live cold launch. The row is `HW` for exactly this run.

## Preconditions

1. In the hardware room (`Open3DScanner-wt-hw`, branch `hw/next`): `./sync-to-main.sh` — then
   check `git log --oneline -5` **actually shows the #185 merge**. A room without it tests
   nothing; stop if it is absent.
2. The vendor SDK installed (CrealityScan): the app resolves the dylib from it and does not
   package it (A2's shape — see PLAN row #63 and PR #180). The scanner attached, and no other
   SDK user (rule 7):
   `ps aux | grep -iE "scan_server|live_capture_test|smoke_test|_message_listener_test|obscan" | grep -v grep`
3. Build the app in the room: `cmake --build build --target OpenScanner` — the bundle lands at
   `build/tools/macos_app/OpenScanner.app`.

## Run

Launch from a terminal so the app's stdout/stderr is captured (the run's raw log):

```
build/tools/macos_app/OpenScanner.app/Contents/MacOS/OpenScanner 2>&1 | tee /tmp/plan65-launch.log
```

**Launch it the way a user does, at least once** — `open build/tools/macos_app/OpenScanner.app`
(Finder/Spotlight/double-click). The terminal form above is for capturing the worker's stdout,
but it runs with the build tree as its cwd and **masks cwd-dependent bugs** — it masked #72
(the bundled app's `ui/` lookup, which only fails under a real `.app` launch). If a run can do
only one, choose `open` and capture what you can from process state and the SDK's own log.

## Procedure

1. **Cold start** — launch as above, nothing else running. Expect: the worker spawns and the
   page loads. The first load *may* be refused — the worker's origin is not up yet (~2 s with
   a device, up to ~20 s without) — and the retry must close that gap (delays ramping
   0.25 s → 2 s, at most 40 attempts, ~73 s of total delay). Record: did the page load on the
   first attempt or after retries, and roughly how long the gap was? The HUD's `backend` line
   and its `scan: …` state are the evidence the page really connected.
2. **Crash surfaced** — while running, kill the worker (`pgrep -fl live-worker`, then
   `kill -9 <pid>`). Expect: the app notices the non-zero exit, shows the alert (the exact
   string is in `main.mm`), and stays responsive — no hang, no spinning cursor. Record the
   exit status the alert names.
3. **Clean exit** — relaunch and let the session end by itself (a clean status-0 exit; the
   worker takes `--live-worker <port> <library> <seconds>`, so a short session length will do;
   Cmd-, sets it once A4 (#192) lands).
   Expect: **no alert**, and the window title reads "OpenScanner — session ended" — with the
   session length appended as " (N s)" once A4 (#192) is merged, e.g. "… ended (300 s)".
   Record the title exactly.
4. **Graceful quit** — relaunch, wait for the session to start (the page connected), then Quit
   (⌘Q). Expect: SIGTERM to the worker, then the full `pause`/`mesh`/`stop`/`session_destroy`/
   `context_destroy` chain — the tail is ≈ 11 s (≈ 19 s on the 15.6M case) and the app must
   **not cut it** (`NSTerminateLater`, an unbounded wait for the real exit). Record the
   observed teardown duration and that the worker process is gone afterwards
   (`pgrep -fl live-worker`).

## A4's steps (settings and failure paths — merged as #192)

5. **Settings** — open Cmd-,: set a different port and a short session length. Expect: the dialog
   says the page reloads on a port change; the worker restarts through the same teardown chain
   (the SIGTERM → `pause`/`mesh`/`stop`/`session_destroy`/`context_destroy` tail shows in the
   log); the page reloads and reconnects on the new port (the HUD's `backend` and `scan:` state).
   Then set the length back and confirm the title carries it ("… (N s)").
6. **Missing SDK** — relaunch with `OPENSCANNER_LIBRARY=/nonexistent` in the environment. Since #74
   (merged), a set-but-missing override is **fatal**: the dialog reads
   `OPENSCANNER_LIBRARY is set but does not exist: • <override>` plus "Correct it, or unset
   OPENSCANNER_LIBRARY to use <installed default>" — no "Searched:" list, since only the override was
   probed. Expect no opaque failure and no crash. Record the dialog's text verbatim.

## Record

Write `<YYYY-MM-DD>-plan65-app-launch.md` + the raw log
(`<YYYY-MM-DD>-plan65-app-launch.log`) under `tests/results/`, one file per run: the
observations above, observed vs expected, and anything unexpected — the unexpected is the
point. Attach the app's log verbatim; if the app crashed, keep its `.ips` beside the record.

## Notes

- `OQ-S6`'s tail answer (≈ 11 s; ≈ 19 s on the 15.6M case) is the budget this run checks
  against — no separate timing run is owed.
- If the page never loads at all, that is a **finding**, not a failed run: record the retry
  delays visible in the log and the HUD's state at the end.
