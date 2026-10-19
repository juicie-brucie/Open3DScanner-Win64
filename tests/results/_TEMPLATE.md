# PLAN #<n> — <short title> — <PASS|FAIL|SKIP|BLOCKED>

<!-- Copy this file to tests/results/YYYY-MM-DD-<slug>.md and delete these comments.
     Files beginning with "_" are support files, not records: verify-pr.py skips
     them in its record-provenance check. -->
<!-- Paste the header block that scripts/hw-preflight.ps1 printed. -->
- Date: YYYY-MM-DD
- Worktree: C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw
- Branch: <branch>
- Target: build\...\<binary>.exe  (<why that copy>)
- Other copy: <the other binary copy and its mtime, or "none">
- Preflight: <the preflight summary line>
- Interposer: N/A for interposer (macOS-only); DLL path verified: <the path preflight printed>
- Run start: YYYY-MM-DD HH:MM:SS

## Command

```
powershell -NoProfile -ExecutionPolicy Bypass -File <script or target>
```

## Result

- Exit code: `<n>`
- Verdict: **<PASS|FAIL|SKIP|BLOCKED>**
- Raw log: `<slug>.raw.log` (same directory)
- Interposer confirmation: n/a — macOS-only construct this lane cannot reproduce (see the agent doc)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | <what check 1 asserts> | **[PASS/FAIL]** <verbatim evidence> |

<!-- One row per [PASS]/[FAIL] the run printed. Quote failures verbatim. -->

## Crash sweep

<!-- Always run
       powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hw-postflight.ps1 `<slug>` '<run start>' `<target>`
     and paste its outcome here: the archived-report line (with sha256), or the
     exact sentence it printed. Windows has no .ips; the sweep matches the report's
     own fields (AppName/AppPath + time) across WER ReportArchive and the
     Application event log (EventID 1000), and archives <slug>.wer (+ optional
     <slug>.hdmp) or <slug>.event.txt. If neither source yields a report it prints
     exactly: no Windows crash-report source available - sweep not performed -->
<the postflight line>

## On failure or block — observable + one lead

<!-- ONLY the observable. Do NOT trace source, quote more than the error,
     or propose a fix. One Lead line, one area, for the fixer. Delete this
     section on a clean PASS. -->

- Observable: <command, exit code, verbatim error, first on-disk symptom>
- Lead: <single most-likely area for the fixer, one line, e.g.
  "likely in ParseReplayFile's argument handling (tools/raptor_client/main.cpp)">

## Open questions

<!-- State them as questions. Do NOT answer them — that is recon's/builder's job. -->
- <question the run leaves open>

## Board

<!-- Report the move; do NOT edit PLAN.md. The dispatcher applies it. -->
- Row move: #<n> `<from>` → `<to>` — <one-line reason tied to the verdict above>
