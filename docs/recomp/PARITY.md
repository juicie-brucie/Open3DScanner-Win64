# Parity ledger — `Open3DScanner-Win64`

This file is the **fork's own ledger**: where each area of the fork stands against the parent,
and the exact commit or blob the claim was read at. The **parent remains the source of truth
for the contract** — `engine/include/openscanner/obscan_api.h` is single-sourced there
(`WINDOWS-FORK.md` §2) and this fork consumes it as a pinned copy; the contract's *semantics*
live in the parent's header and in `docs/recon/findings/**`, not here. What this file records is
the fork's *state* against that contract, and every place the two have knowingly diverged.

Every row names the commit or blob it was read at, so a claim can be re-checked rather than
trusted. A row whose record is missing is a gap, not a parity claim.

Read at: fork `main` @ `a37205b`; parent `main` @ `2cf83f2`; parent tree `7537e44`.

| Area | Source of truth | Record |
| --- | --- | --- |
| contract header | parent | parent `7537e44`; blob `9c73210f`; sha256 `1ace2dc8…` — the fork's copy is the **same blob**, byte-identical (57 183 B). Pin: `FORK-COORDINATION.md` §2. `--pincheck` hashes the **committed blob** (`git cat-file blob`), never the working file. |
| `engine/` | parent sync | parent `7537e44` consumed (PR #17 @ `f2e9003`). Exactly **one** file diverges: `engine/src/obscan_loader.cpp` (see the loader row). |
| `tools/` | parent sync | parent `7537e44`. Excluded: `tools/macos_app/**`, `tools/raptor_client/**` (macOS-only; POSIX sockets). Preserved: `tools/hw_guard_test/run.mjs` (fork-only) and `_USE_MATH_DEFINES` in `tools/scan_server/main.cpp`. The only other delta is the Windows adaptation of `tools/scan_server/` (the `_USE_MATH_DEFINES` define, the `_WIN32` includes, the signal-handler path). |
| `ui/` | parent sync | parent `7537e44`; **zero delta** — `git diff 7537e44 main -- ui/` is empty. |
| `scripts/verify-pr.py` | parent copy + adaptations | parent blob `83a4de49` taken as a unit (parent delta `ba53665..83a4de49`); the fork's file is blob `52146f16`, **+532 / −96** lines over the parent blob. Windows adaptations: the repo is derived from `origin`; `gh`/`git` are resolved without a usable PATH; a check that does not apply on this host is reported **N/A with its reason**, never a silent skip or a false pass; committed-blob pincheck (`_committed_blob`); the guard is selected by **lane**, not host (`GUARD_PATH_UNIX` / `GUARD_PATH_WINDOWS`); WER-aware archives (`Report.wer` is UTF-16LE/BOM, matched on the report's own AppName/AppPath + EventTime). `--selftest`: 52 cases. |
| `tests/results` conventions | parent | `_`-prefixed support files are not records; an added record needs cited, condition-named evidence; `tests/results/**` is **agent-tier** (`AGENT_PATTERNS`), so an added record needs a verdict. |
| HW workflow | parent | parent `c57dbe6`; ported to `.ps1` — `scripts/hw-preflight.ps1`, `scripts/hw-postflight.ps1`, `.claude/hooks/guard-hw-run.ps1` (the parent's `.sh` is not in the fork). The interposer is **N/A** (macOS-only) and the vendor DLL path is asserted instead. Crash sweep: **WER `ReportArchive` + the Application event log, `EventID 1000`** — a divergence from the parent's `.ips`. |
| test binaries | parent + port | parent `c57dbe6`; both ported (`device_message_listener_test`, `data_process_message_listener_test`); both **build and link**; both print the explicit no-interposer banner — the launcher states the worker runs WITHOUT an interposer and that the run is not equivalent to a Mac run in that respect. **The old SKIP (2) claim was false at this ref.** With the SDK now loadable (`win #119`) the return-2 load-failure catch is unreachable, so with no device attached and the CrealityScan RPC daemon not running (the daemon is started by `CrealityScan.exe`) both currently **exit 1** at `obscan_context_create()` (`OB_SCAN_ERROR_INVALID_ARGUMENT`), and ctest records exit 1 as **failed**, not skipped — both `CMakeLists.txt` set `SKIP_RETURN_CODE 2`. This is a precondition-state observation, not a hardware run; `CrealityScan.exe` was not launched. Tracked as `win #120`. |
| loader | parent-synced | `engine/src/obscan_loader.cpp`; `win #119` `LoadLibraryExA(..., LOAD_WITH_ALTERED_SEARCH_PATH)` (Windows branch only) merged @ `bfa3df3`; **awaiting parent fold-in** — the parent has recorded the finding and asked how it is to be folded, and no parent branch carries the change yet. Until it is upstream, an `engine/` re-sync reverts it. |
