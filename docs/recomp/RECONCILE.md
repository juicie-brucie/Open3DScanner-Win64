# Reconcile — the analyze step for the Windows lane

**Owner:** the `openscanner/planner` agent. **Runs after** a record lands; **consumed by** the
dispatcher, who applies the board moves. Implements `win #114`.

This is the procedure that turns a record in `tests/results/**` into board moves and tag
changes. It is a **procedure, not a design doc**: work it in order. Each step says what to read,
what to check, and what to write down. It is short because it is a checklist, not an essay.

**The one rule that governs every step:** *a tag is cleared by a run, never by argument.* A claim
that is not visible in the record's own evidence does not clear a tag — it becomes a finding.

**Who writes what:** this procedure produces a **proposal**. The **dispatcher** applies the board
moves. It never edits `PLAN.md` (§6).

**Why it exists:** the parent's handover order step 9 — *"The fork's `win #114` reconcile runs
**before any board move** is claimed from the record"* — and the fork runbook's own *"Reconcile -
after the record lands"* section, which names this row as its automated form.

## 1. Inputs

Name these before reading anything. If one is missing, stop: the run is not reconcilable yet.

| # | input | where | note |
| --- | --- | --- | --- |
| I1 | the record | `tests/results/<date>-win-live-rate-<slug>.md` | the **newest** record; one file per run, never edited in place |
| I2 | its raw log | `tests/results/<date>-win-live-rate-<slug>.raw.log` (or `.log`) | **in the same PR** — the gate's `record-provenance` check requires it |
| I3 | the board | `docs/recomp/PLAN.md` at a **named commit** | write that SHA in the proposal; moves are proposed against it |
| I4 | the findings it cites | `docs/recon/findings/**` | only the documents the record names |
| I5 | the artifacts it cites | `*.jpg`, `*.wer`, `*.hdmp`, `*.event.txt`, `*.ips` | cited by filename; check each exists |

**Read I2 before I1.** The raw log is the ground truth; the record is a claim *about* it.

## 2. Evidence gate

Check every item. **A record missing any of them is a finding, not a board move** — file it as
one (§5) and stop.

| # | gate | passes when |
| --- | --- | --- |
| G1 | raw log | present beside the record in the same PR, and the record names it |
| G2 | eight header fields | `Date` / `Worktree` / `Branch` / `Target` / `Other copy` / `Preflight` / `Interposer` / `Run start`, in that order, none empty |
| G3 | Windows preconditions block | build SHA + compiler/CMake/Ninja; device variant + serial + USB link; vendor versions as pinned; machine spec (RAM, OS build, GPU — both the WMI figure and the real one); app/daemon state; browser used for the HUD |
| G4 | condition table | all six conditions (2 client states × 3 phases), 30 s each |
| G5 | result-field table | all six result fields, each naming the condition it was taken in |
| G6 | crash sweep | the archived-report line with its sha256, **or** the exact sentence `no Windows crash-report source available - sweep not performed` — never "no crash" |
| G7 | verdict and exit code | `PASS`/`FAIL`/`SKIP`/`BLOCKED` and the exit code, consistent with the raw log |

G2-G6 are the **Windows** record contract (`docs/recomp/windows-handover-order.md` on the parent
side, `tests/results/_TEMPLATE.md` and `tests/hardware/plan-win-live-rate.md` here). A record
that is not a Windows live-rate run **branches** — see §7.3.

## 3. Claim-by-claim check

For every result the record claims, name the evidence. Write this table into the proposal.

| claim in the record | supporting raw-log line / artifact | verdict |
| --- | --- | --- |
| `<the claim, quoted>` | `<file>:<line>` or `<artifact> + what in it shows>` | supported / **not supported** / contradicted |

- **No table row moves on prose.** A claim with no raw-log line and no artifact is *not
  supported*, however plausible it reads.
- A number must appear **verbatim** in the raw log. A derived number (a rate, an average) must be
  recomputable from the lines cited.
- **A negative claim** ("no `obscan_scan_stop` was logged") is supported by citing the last line
  before where it should have appeared **and** the search run (pattern and where). Silence is
  evidence only if you show the silence.
- **A conditional claim** ("if the HUD showed usable fps...") supports nothing. It is a finding.
- An artifact supports only what it can show: one end-of-session frame cannot evidence
  "throughout the phase".

## 4. Proposed moves

Propose; do not apply. Format:

```
Row move: #<n> `<from>` → `<to>` — <one-line reason>
Evidence:  <record> §<section> / `<file>:<line>`
```

- Statuses: `DONE` / `HW` / `BUILD` / `TODO`.
- **A tag is cleared by a run, never by argument.** `[inferred]` → `[hw-confirmed: <run>]` only
  where the record's evidence actually shows it.
- `win #113` moves to `DONE` **only** when all six conditions × six result fields are present,
  the raw log supports each of them, and the record's verdict is `PASS`. Anything less is `HW`
  (observed, not accepted) with the gap named.
- A run that raises something it does not answer becomes a **new row**, not a footnote.
- If the evidence supports no move, say so. **"No moves" is a valid outcome** — a procedure that
  manufactures a move to look busy is worse than one that does not.

## 5. Corrections

When a record contradicts a board note or a finding:

- **Never edit the record.** File a dated addendum `tests/results/<record-slug>-correction.md`
  that names the record, quotes both texts and cites the evidence. The gate treats an addendum as
  a dated correction, not an in-place edit.
- The **board note** gets a **supersession clause** — a dated sentence saying what supersedes
  what — not a rewrite. The board's history is its memory: the parent's row #115 keeps a
  superseded sentence in place, and records why.
- A correction to a *finding* goes to recon; a correction to a *row note* is proposed here and
  applied by the dispatcher.

## 6. One writer

- This procedure produces a **proposal**; the **dispatcher** applies the board moves. Never edit
  `PLAN.md` from here. The record template says the same: *"Report the move; do NOT edit
  PLAN.md. The dispatcher applies it."*
- One writer per file, always: `PLAN.md` → dispatcher · `FORK-COORDINATION.md` → the lane's
  planner (append-only) · `ROADMAP.md` → the lane's planner · findings → recon.

## 7. Dry run

Applied on paper to records already in the repo. **Nothing below was applied** — no board move
was made, no record edited.

### 7.1 Run A — `2026-10-18-scan-server-scale-acceptance-rerun.md` (raw companion present)

**Inputs.** I1 the record; I2 `...-rerun.raw.log` (12 183 lines) ✓; I3 `PLAN.md` @ `bcd9d0f`;
I5 `...-rerun-final-hud.jpg` ✓.

**Gate.** G1 ✓. **G2-G5 branch** — this is an inherited macOS record, not a `win-live-rate` run:
its header is the parent's older one (Date / Worktree / Branch / HEAD at run time / Target /
Binary mtime / Command / Exit code / Raw log), there is no Windows preconditions block and no
condition × result table. macOS equivalents that do hold: preconditions → the record's *"Pre-run
checks"* and *"Banner check (rule 6)"* sections; crash sweep → the *"Crash report present for this
run"* check against `~/Library/Logs/DiagnosticReports/` (none in the window). G6 ✓ (the
equivalent line is present). G7 ✓ (verdict `FAIL`, exit 2, matching the log's tail).

**Claim by claim.**

| claim in the record | evidence | verdict |
| --- | --- | --- |
| "crossed 1,000,000 ... between tick 400 (991,963) and tick 450 (1,195,834)" | `...rerun.raw.log:1665` (991,963), `:1837` (1,195,834) | supported |
| "max live value of 1,412,644 at tick 2950" | `...rerun.raw.log:11952` — `num_points=1412644` | supported |
| "**52 consecutive printed samples** ... at or above 1,000,000" | the log carries **51** samples ≥ 1,000,000 (60 `color_cloud` tick lines in all; the 9 before tick 450 are below the bar). The record's own arithmetic — 450 → 2950 in steps of 50 — is 51 markers, not 52 | **contradicted** (off by one) |
| "render held steady at ~60fps throughout the live-scanning phase" | **not in the raw log** — it carries no `render`, `latency` or `[perf]` line. It rests on the record's assertion plus `...-rerun-final-hud.jpg`, a single end-of-session frame | artifact-backed, but a single frame cannot evidence "throughout" → not supported as stated |
| "no `obscan_scan_stop`, no `obscan_session_destroy`, no `obscan_context_destroy` ... ever appeared" | negative claim: `...rerun.raw.log:12174` `Pipeline destroyed!`, `:12176` `rotate pose for model.ply`, then `:12182` the watcher's kill line — no teardown line between 12176 and the kill | supported (silence shown) |
| "the worker hung and was force-killed by the tool's own 420 s watchdog" | `...rerun.raw.log:12182` | supported |

**Proposed moves: none.** The rows this record feeds — `#32`, `#37`, `#43`, `#46` — are already
`DONE`, and their notes cite it accurately. `#32`'s note even discloses that the teardown leg
failed and that the row's acceptance is narrower than the run's verdict, and `#37` carries the
hang as its own row. A procedure that invented a move here would be wrong.

**Proposed correction: one.** The "52 consecutive samples" count in `#32`'s note (and in the
record's own text) is not what the log shows. Under §5 this is a dated addendum
`...-rerun-count-correction.md` quoting the claim, the 51 samples, and the lines that count them;
`#32`'s note would get a one-clause supersession, not a rewrite.

### 7.2 Run B — `2026-10-17-live-scan-manual-test.md` (no raw companion)

**Gate G1 fails.** The record says so itself: *"No raw log survives; the run's worker output was
ephemeral."* → **a finding, not a board move.**

Row `#23` cites this record, so the finding is: *a record with no raw evidence cannot clear a
tag.* It can support "an observation was reported" — it was landed deliberately as the operator's
verbatim report, and that is legitimate history — but it cannot support a measurement, and no
future reconcile may cite it as numeric evidence.

### 7.3 Where the procedure branches for Windows

| aspect | Windows branch | inherited macOS branch |
| --- | --- | --- |
| record set | `...-win-live-rate-<slug>.md` + `.raw.log` | `<slug>.md` + `.raw.log` / `.log` |
| G2-G5 (header, preconditions, condition/result tables) | required | **not applicable** — the parent's own header and sections stand in |
| G6 crash sweep | WER `ReportArchive` + Application event log `EventID 1000`, or the exact "sweep not performed" sentence | `~/Library/Logs/DiagnosticReports/` |
| G7 exit code | `win #120`: exit 2 = precondition not met (SKIP, with the reason string), exit 1 = genuine failure | exit 2 = the launcher's watchdog killed the worker |
| result field 2 (tick breakdown) | `[perf] window` lines (from the parent sync, #17) | absent in these records |

The branch point is **the record's own kind**, decided at I1: a `win-live-rate` record takes the
Windows column, any other record the macOS column, and the rest of the procedure is identical.
