# Recomp roadmap

Forward strategy for the recon → build → hw → reconcile loop. Where
[`PLAN.md`](PLAN.md) is the live **queue** (one line per task, move the status as you
go), this file is the **plan**: the critical path, what is blocked and on what, the
risks, and the objective of the next round.

Owner: the `openscanner/planner` agent. The dispatcher consumes it and applies the row
moves it proposes. Statuses, claims and evidence live on the board — this file points at
them and never restates them.

_Last planned: 2026-10-18 (rev 9 — adds the Windows fork workstream; its charter and the fork-coordination file are
**landed** at `docs/recomp/WINDOWS-FORK.md` and `docs/recomp/FORK-COORDINATION.md`. rev 8 was the refresh pass; its architecture-doc staging
block has landed and moved to `docs/architecture/scan-viewer.md`. rev 7 added the viewer
family; rev 6/5/4/3/2/1 are superseded — see **Superseded plans**)._

## Objective for the next round

**Give the interface a shape: land the viewer's frame protocol (`V1`) — which currently has
no board row.**

The live path works and now holds at scale (`#25` → `#32` → `#38` → `#39`). What the app
lacks is a *shape*: the wire format is positions-only, unversioned and pose-free, so
scanner-POV following and a second client are both impossible today. `V1`
(`frame_protocol.h` + a codec on each end) is the foundation every other viewer row needs.

**First action for the dispatcher: file the `V1` row.** Rev 7 proposed three rows and the
dispatcher filed them (`#39`, `#40`, `#41`) — but not the foundation itself, so the
objective's own row is missing. `V4`'s blocker (`#40`) is already filed and needs no
further planning.

## Viewer family (V1–V5) — the interface, captured 2026-10-18

The UI is still a proof of concept: `ui/app.js`, a dependency-free WebGL2 `gl.POINTS`
viewer with no camera interaction and no pose. The target is a robust interface that renders
the active scan, follows the scanner's point of view, and maps cleanly onto Windows, Linux
and macOS. Full architecture: `docs/architecture/scan-viewer.md` (**staged at the end of
this file**; boundaries below). The V-labels are the planner's.

| # | Goal | Board row | State |
| --- | --- | --- | --- |
| **V1** | `frame_protocol.h` + a codec on each end — versioned envelope, typed messages; today's wire shape becomes v1 | **none — file it** | **the objective**; nothing blocks it |
| **V2** | A point budget on the wire, plus client backpressure | `#32`, `#38` (budget) · `#41` (backpressure) | **budget landed**; backpressure open |
| **V3** | `ReplayCaptureSource` + one recorded session, so the viewer develops without the scanner | none | unfiled; needs V1 |
| **V4** | `pose` message + the `follow` camera mode (scanner POV) | `#40` (recon) | filed; needs V1 |
| **V5** | WebGPU renderer + orbit controls, WebGL2 fallback; optional thin native shell | none | unfiled; needs V1–V3 |

**Landed vs forward — do not read one as the other.** `#32` shipped a **fixed 20,000-point
*stride* sample per tick**, and `#38` capped the final-mesh frame; `#39` adds the on-demand
full-detail path. That is the *budget* half of V2, and it is **done**. The protocol rules in
the architecture doc — **voxel** decimation, delta encoding, quantization, and backpressure
— are a **forward design** layered on top; only backpressure is rowed (`#41`). The doc marks
which is which.

Device-layer portability is a separate, harder thread: the seam (`CaptureSource`) can be
built now, but the device layer only becomes OS-agnostic when the capture source moves off
OBScan to the Orbbec SDK — see `README.md`'s "Possible future direction" and **G7**.

## Dispatcher handoff — operator's manual `#25` rerun (2026-10-17) — **CLOSED**

Kept for provenance only. The rerun landed as
`tests/results/2026-10-17-plan25-manual-rerun.md`; both observations it raised were filed and
are `DONE` (`#32` the live-view stall, `#33` the `pkill`/worker linger — the latter was a
README-guidance defect, not a leak). Its follow-on chain ran to completion too: `#38` capped
the final-mesh frame and `#39` (in flight, PR #95) restores a full-detail path. The teardown
*hang* it exposed is still open as `#37`.

## Critical path

```
V1 (frame protocol — UNFILED, the objective) ─┬─► V4 (pose + follow POV; blocker #40 is filed)
                                              ├─► V3 (replay source) ─► V5 (WebGPU client)
                                              └─► V2 backpressure (#41; the budget half landed as #32/#38)

#39 (full-detail path, PR #95) ─► the viewer's final-model path
#34 (frame API) · #35 (loader release) ─► the listener live path
#36 · #37 (RECON) ─► scale / teardown        #11 (HW — unblocked, #20 DONE) ─► #5
#7 (BUILD) ─► OQ-7b/c/d                      #23 (SPEC-READY) ─► the wider scan surface
```

Ranked by what it frees, not by board order:

1. **`V1`.** It is the objective, it has no row, and every other viewer row waits on it.
   Filing it is the cheapest action available.
2. **`#39`.** In flight (PR #95) and user-visible: the final model on screen is currently a
   20k preview. Its row is the live edge of the viewer family.
3. **`#40`.** Filed and blocking `V4`; recon-only, so it does not compete for the hardware
   room.
4. **`#11` → `#5`.** Unblocked (`#20` is `DONE`; `#15` fixed the target to the remove-side
   race). A real `SIGSEGV` on a feature the board marks `DONE`, and it needs the
   **serialized** hardware room.
5. **`#34` → `#35`.** The listener live path's prerequisites.
6. **`#36`, `#37`, `#7`, `#23`.** Recon/analysis backlog.

## Ready now

| Row | Role | What it needs |
| --- | --- | --- |
| `V1` (unfiled) | dispatcher | File the row; then builder owns `frame_protocol.h` + codecs. |
| `#39` | builder | In flight (PR #95). The on-demand full-detail path. |
| `#40` | recon | V4's blocker: spec `get_global_pose`/`get_markers`. One target, one session. |
| `#41` | builder | V2's remainder: client backpressure on the frame stream. |
| `#11` | hardware | Unblocked: `#20` is `DONE`. The controlled remove-side-race re-run. |
| `#34`, `#35` | recon / build | The frame API in the header subset; the unresolved `realtime_model_mesh_release`. |
| `#36`, `#37` | recon | OQ-P5 (the five direct-out getters' layouts); the large-mesh teardown hang. |
| `#7` | recon / build | OQ-7b/c/d follow-ups ([`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md)). |
| `#23` | recon | `SPEC-READY`; its follow-ups have been spun out into their own rows. |

## Blocked, and on what

| Row | Blocked on |
| --- | --- |
| `#5` | `#11`. A `[hw-contradicted]` tag is cleared by a run, not by argument. |
| the listener live path | `#34` (the frame API in the header subset) then `#35`. |
| `V3`, `V4`, `V5` | `V1`. |
| `#21` | A **user decision** on tiering added run records. |
| G1–G5, G7 | Being picked up; each needs its own row, and a recon row before any build row. |

## Orphaned open questions

An OQ with no task row is unowned work — the spec claims that lean on it are load-bearing
on nothing. Current state:

- **All four PLAN #2 OQs are closed out** — OQ-P1…P4 were adopted as `#26`–`#29`, which have
  all landed. OQ-S1 was answered under `#24`; OQ-S5 resolved under `#25`.
- **OQ-S2** (does pause/stop publish temp→show?) — **still orphaned.** The `V1` protocol
  work touches the same publish path, so adopt it there rather than as a separate row.
- **OQ-S3** (`+0x2e0` vs `+0x320` lock asymmetry) — **still orphaned**, low priority. The
  landed decimation (`#32`) already sits on this path; re-check before rowing separately.
- **OQ-1** — the `[inferred]` residual sits under `#1` (`DONE`). **Flag on `#1`** or it is lost.
- **OQ-5** (`tips`/`control`/`calib` `*_name` jump tables) — **still orphaned**; propose a recon row.
- **OQ-6** — accepted as-is by decision. Correct to leave unrowed.
- **OQ-7b** — what `#12`'s `get_device_num()` note waits on. **Adopt as a hardware row.**

Not an OQ, but the same failure mode: **`V1` is the round's objective and has no row.** An
objective with no row is unowned work with extra steps.

## Goals carried forward from the research corpus (captured once)

`docs/research/**` (five docs, committed 2026-10-16, before the recon→findings structure)
holds the only statement of several of OpenScanner's own product goals. The corpus is
**frozen** — a historical artifact: no in-place maintenance, no adoption into findings.
Its vendor-fact claims are partly superseded by later recon, so any goal picked up from it
re-derives the facts it needs through recon like any other target. Captured once so they
are tracked and not lost. The dispatcher files the rows; the G-labels are the planner's.

| # | Goal | Origin | Needs first | Horizon |
| --- | --- | --- | --- | --- |
| **G1** | Live plane removal for **resource management** (cut data per frame during capture), not a post-scan cosmetic cut — the user's own "it does not work at all" complaint | `plane-removal-reverse-engineering.md` §5 | a recon row to adopt the ABI + decision-fn finding, **or** a build-only RANSAC/signed-distance path that skips vendor logic | after capture |
| **G2** | Coverage heatmap: replace the one-scalar "insufficient data" verdict with a spatial under-covered set painted on the live cloud | `scan-quality-and-coverage…` §5.2; `pipeline-quality-assessment.md` §3.3 | header-subset additions (`get_global_pose`, `get_markers`) — **overlaps `#40`** | after capture |
| **G3** | Owned tracking (marker detection + bundle adjustment) with per-pose rejection reasons surfaced | `pipeline-quality-assessment.md` §3.4 | G2's feedback layer to display them | medium–large |
| **G4** | Own denoising + fusion pipeline (outlier removal → PCA normals → bilateral/RIMLS → WLOP; ICP → pose-graph with loop closure → Poisson/BPA), Open3D as the MIT anchor | `point-cloud-denoising-and-fusion.md` §1–2 | a fusion feature actually being planned | long |
| **G5** | Fusion-accuracy evaluation harness (unidirectional MSD/RMS/min–max, non-parametric stats, repeatability across runs) | `point-cloud-denoising-and-fusion.md` §3 | G4, or any fusion change worth measuring | with G4 |
| **G6** | Fix our own SDK usage: drain the data listener, align voxel sizes | `pipeline-quality-assessment.md` §3.1 | **half open** — the listener is still registered and unconsumed (findings §6); `#31` added instrumentation, not a consumer. The voxel-size half has no row. | open |
| **G7** | Keep laser extraction + calibration behind the SDK until the "drop OBScan entirely" decision | `pipeline-quality-assessment.md` §3.5 | that decision (README, "Possible future direction") | standing |

Two design constraints from the corpus bind any row above: keep capture decoupled from
fusion via a bounded queue (`live-scanning-pipeline-overview.md`), and keep the
frame-acceptance gate and the bundle adjuster as **two separate systems** rather than one
unified heuristic (`pipeline-quality-assessment.md` §2).

**Why captured, not adopted.** All three vendor-RE docs rest on a "Ghidra cannot complete
on this binary" premise that no longer holds — recon now runs Ghidra/REA over a persisted
snapshot (see the method line of
[`findings/data-process-message-listeners.md`](../recon/findings/data-process-message-listeners.md)
and [`docs/tooling/rea-ghidra-setup.md`](../tooling/rea-ghidra-setup.md)) — and the corpus
predates the listener ABI and detection-path findings that supply the vendor's actual live
channel. The *goals* survive that; the *facts* behind them must be re-derived per goal.

One item for recon when G1 is picked up: the plane-removal doc attributes its three toggle
slots to `OBScanDataManagement`, while current recon homes the data-management subobject
at `scan+0xf0` on `OBScanBase` ([`findings/scan-live-data.md`](../recon/findings/scan-live-data.md) §1).
Same object, possibly a re-homing — verify, don't assume.

## Windows fork — lane roadmap, Open3DScanner-Win64 (this lane)

This section is the forward strategy for the `Open3DScanner-Win64` lane. The parent's
viewer-family objective above (`V1` … `V5`) is the parent's lane and is not this lane's
work. Charter: `docs/recomp/WINDOWS-FORK.md`; coordination:
`docs/recomp/FORK-COORDINATION.md` — both landed, both seeded at repo creation. The fork is
standalone; the parent is the pinned upstream.

### Gate — answered

Charter open question 1 (does Creality ship a Windows build with the `obscan_*` surface?) is
answered **yes**. See `docs/recon/findings/win64-surface.md` §gate: `CrealityScan_win_4.3.2.exe`
carries `lib_orbbec_scan.dll` (PE32+ x86-64) exporting the full pinned macOS surface, 0
missing, plus 36 Windows-only exports; same build 3.5.3.260921. This was the gate on
everything; it is no longer a question, and nothing waits on it anymore.

### Where §8 stands (ordered; each item gates the next)

1. Answer OQ-1 — ✅ answered (`win64-surface.md`).
2. Windows toolchain + CMake configure — ✅ row #101 (40/40 objects, `scan_server` linked;
   `M_PI` fix proven by clean-directory rebuild, PR #2).
3. Contract pin wired into CI — ✅ row #104 (`--pincheck`, PR #7). Hardening — hash the
   committed blob, never the working file; name both values and the parent commit on failure —
   rides with row #107 and is not yet landed.
4. PowerShell/Python equivalent of `guard-hw-run.sh` — ✅ row #105 (PR #4; scope defect fixed
   by #112).
5. First Windows recon target, with its own findings doc and pins — ⏳ row #108 (enum parity,
   PR #8 open, finding under verdict; see `docs/recon/findings/win64-enum-parity.md` on branch
   `recon/win-enum-parity`). Windows artifact pins are still absent from
   `FORK-COORDINATION.md` §3.

### Next objective (singular)

Complete charter §8 item 5 — land the first Windows recon target — before any hardware
acceptance:

- row #108: enum parity → findings doc `win64-enum-parity.md` (PR #8) + record Windows artifact
  pins in `FORK-COORDINATION.md` §3;
- row #107: consume the parent's current contract revision and harden `--pincheck` to hash the
  committed blob (PR #6).

Reason it is next rather than the alternatives: §8 orders item 5 last among the pre-build
items, items 1–4 are met, and the two open PRs (#6, #8) are the only uncommitted recon/build
work. The pin and the enum surface are what every later hardware claim will be judged against,
so landing them first keeps later claims falsifiable.

The rejected alternative — attach the scanner and run the first hardware acceptance now — is
not dispatchable this round: the scanner is not attached (physical wall; last present 17 Oct
13:08–13:53, Raptor Pro on VID_2BC5; needs the operator), there is no analyze/reconcile step
to convert a first run into trusted board moves, and rows #107/#108 would leave the contract
and enum surface unverified going into hardware. Hardware acceptance becomes the objective
once the scanner is attached (then row #113 clears).

### Sequencing (dependencies stated)

1. Land #107 (contract sync → parent tip; pincheck hashes the committed blob). Needs: parent
   tip stable; verify-pr gate green (#103, done). Drift alarm for all later work.
2. Land #108 (enum-parity findings + Windows pins). Needs: a Ghidra import of
   `lib_orbbec_scan.dll` (row #108 budgets hours); PR #8 under verdict. Blocks trusted enum
   semantics for the Windows surface.
3. Wire #110 (REA MCP registration). Needs: #103/#105 landed (done); now a config edit, not a
   wall.
4. Define the analyze/reconcile step (proposed row #114, below). Needs: this roadmap. Must
   exist before the first hardware run.
5. First Windows hardware acceptance (#113). Needs: scanner physically attached (operator);
   backstop armed (#105); build (#101); pin (#104); reconcile step (4). Not dispatchable until
   the scanner is attached.

Charter open questions still open: OQ-2 (x64 only, or Windows-on-ARM in scope — operator);
OQ-3 (does the `#ifdef _WIN32` loader split compile against a real DLL — builder, once an
artifact exists; #101 compiled it, not exercised against the real DLL); OQ-4 (what replaces the
interpose-dylib mechanism on Windows — builder/recon); OQ-5 (own agent definitions and
worktrees — operator). OQ-1 answered (yes). §8 unmet: item 5 (this objective). Items 1–4 met.

### Risks (ranked: what would make this lane produce confident wrong answers)

1. **Pincheck hashes the working file, not the committed blob** (#104's stated limit, hardened
   in #107). A stale or locally-edited header passes as pinned; the fork builds and runs
   against a divergent contract and every downstream claim inherits the wrong ABI. Retire: land
   the #107 hardening (hash the committed blob, name both values + parent commit on failure).
2. **Verdict posted to the wrong repository**. `gh` defaults to `upstream`, which here is the
   parent; a fork verdict or merge recorded against the wrong repo makes the board record
   landings that did not happen, or block real ones. Retire: always pass `-R
   juicie-brucie/Open3DScanner-Win64` on gh writes; record the fork remote explicitly so the
   intent is not inferred from a remote name.
3. **Thirteen `[inferred]` Windows-side enum gaps treated as settled** (#108 under verdict;
   `win64-enum-parity.md` not merged). Code or behavior keyed on unconfirmed enum values gives
   wrong message/device handling with high confidence; only the Ghidra import settles each
   gap. Retire: land #108; do not build header `#ifdef` guards on the 13 gaps until the
   findings doc is merged.
4. **No Windows hardware run has ever executed** (#113). Every tag here is build or recon; a
   first run can contradict multiple `[inferred]` claims at once. The scanner is not attached
   now (physical wall). Retire: attach the scanner, then run; clear tags by run, not argument.
5. **No analyze/reconcile step** (weakness #4; row #111). After a run nothing reconciles the
   board against what actually happened, so stale statuses and wrong tags persist across
   rounds. Retire: define reconcile before the first hardware run (proposed row #114).

### Rows this plan implies (proposed; the dispatcher applies them)

- #107 → DONE: land PR #6 (contract sync to parent tip + pincheck hardening).
- #108 → DONE: land PR #8 (enum-parity findings `win64-enum-parity.md`); add Windows artifact
  pins to `FORK-COORDINATION.md` §3.
- #110 → DONE: wire REA MCP registration (config edit).
- NEW #114 (analyze/reconcile step): define the Windows-lane reconcile process — compare the
  latest `tests/results/**` record against `docs/recomp/PLAN.md` statuses/tags and
  `docs/recon/findings/**`; propose row moves; corrections as dated addenda; one writer; tags
  cleared by run, not argument. Feeds the analyze/reconcile half of row #111.
- #113 → DONE (future): first Windows hardware acceptance — scanner attached, backstop armed,
  run recorded in `tests/results/**`, then reconcile applied.

### View on analyze/reconcile (#4)

This lane should have an analyze/reconcile step, and it should exist **before** the first
hardware run, not after. The first Windows hardware run is the lane's first
`tests/results/**` record and the first time any tag could become hardware-cleared; without a
predefined reconcile step there is no mechanism to convert that run into trusted board moves
(clearing #108's `[inferred]` enum gaps, resolving #113, re-checking `win64-surface.md`
conclusions against the real device). Reconcile reads the latest run record, compares it to
the board's statuses/tags and the findings, and proposes row moves; the dispatcher applies
them. One row, defined now, used at the first run. It does not need to run before this round's
recon work (#107/#108 — those are static and verifier-gated), but it must be in place before
row #113 is executed.

### Superseded

- ROADMAP.md rev 9's Windows-fork section ("no rows are proposed this round; the gate is an
  operator answer"): superseded by this section. The gate is answered (`win64-surface.md`) and
  rows #100–#113 exist. Dated 2026-10-18.

## Risks

1. ~~The live path is unbounded in the cloud size~~ — **retired by `#32`**, and the
   follow-on chain (`#38`, `#39`) is closed or in flight. Kept for the record only.
2. ~~A leaked worker is a second SDK user~~ — **retired by `#33`** (a README-guidance
   defect). The teardown *hang* it exposed is still open as `#37`.
3. **Evidence provenance (`#21`).** Added records and logs under `tests/results/**` are
   script-tier, so a fabricated or mis-transcribed record auto-merges with no judgment
   pass. Manual reruns keep arriving from outside the repo — the class of evidence that
   most needs a home. Cheapest retirement: the user decision on tiering; interim, land each
   manual report as a record.
4. **A second evidence corpus outside the gate.** `logs/**` is tracked and is the evidence
   base for `pipeline-quality-assessment.md` §1, but it is neither `tests/results/**` nor
   `docs/recon/**`, so the `run-records` check and the tier rules do not reach it. Cheapest
   retirement: a **user decision** on whether `logs/**` stays (and under what discipline).
5. **One serialized hardware resource.** `#11` and OQ-7b queue behind one manual session
   (the scale re-runs are done). Batch per session, cheapest-first.
6. **Recon step budget.** `recon` is capped at `steps: 40`, one board row per session.
   `#36`, `#37` and `#40` are each a session; an order that also asks for adjacent context
   comes back empty.
7. **Verification debt.** Spec claims tagged `[inferred]` that code already depends on:
   OQ-S2/S3, OQ-1's residual, and the tips/control rows `#9` left `[inferred]`.

## Next round, as work-order seeds

For the dispatcher to expand — not dispatches in themselves.

1. **File `V1`** → dispatcher: the objective has no row. `frame_protocol.h` + a codec on
   each end; today's wire shape becomes v1. **Wall:** builder edits `engine/src`, `tools/`,
   `ui/`; denied the header.
2. **Land the staged architecture doc** → dispatcher: move the staged block into
   `docs/architecture/scan-viewer.md` (boundaries in the staging header below), delete the
   block from this file, and PR it. `docs/architecture/**` is agent-tier → needs a verifier
   verdict.
3. **`#39`** → builder: finish the on-demand full-detail path (in flight, PR #95).
4. **`#11`** → hardware: the controlled remove-side-race re-run; archive raw log and crash
   report per the record rules. Unblocked. **Wall:** hardware writes only `tests/results/**`.

Also ready, non-competing: `#40` (recon), `#41` (builder), `#34`/`#35` (recon/build),
`#36`/`#37` (recon), `#7` follow-ups (recon/build).

## Superseded plans

- **2026-10-18 rev 7 → rev 8 — refresh pass, no objective change.** Rev 7's objective
  (`V1`) stands; what changed is that rev 7 restated loop state instead of pointing at it,
  and the loop moved under it. Rows `#3`/`#4`, `#26`–`#30`, `#38` were described as open
  while the board had them `DONE`; the landed decimation (`#32`) was described as pending
  alongside the forward protocol design. Rev 8 points at the rows and separates the two.
  Reversal reason: a verifier read of PR #97, not a change of direction.
- **2026-10-18 rev 6 → rev 7 — objective change.** rev 6's aim was "keep the live view live
  as the cloud grows"; `#32` (and `#38`) landed that, and the operator opened a new
  direction — the viewer architecture — so rev 7 moved the aim to the viewer's foundation
  (`V1`). Reversal reason: the objective was achieved and a new input arrived.
- **2026-10-17 rev 5 → rev 6 — objective change.** rev 5's aim was "make the live view
  stream real points"; `#25` landed and OQ-S5 is resolved, so rev 6 moved the aim to
  *scaling* it.
- **2026-10-17 rev 2 — "Close the scan family's live-data loop: `#24` → `#25` → OQ-S5."**
  Superseded by rev 3, and now fully landed. rev 2 treated `#25`'s getter-publish path as
  the settled answer; the research corpus recorded a second, vendor-chosen live path, so
  rev 3 added the decision pass — which landed as findings §6.
- **2026-10-17 rev 1 — "Drain the hardware queue, and open the scan family in parallel."**
  Superseded by rev 2. Half landed (the scan family was opened via `#23`); the other half
  was wrong as written — the queue's most obvious item (`#2`/`#19`) already had a passing
  run on `main`.

---

# Windows fork drafts — landed

`WINDOWS-FORK.md` and `FORK-COORDINATION.md` were landed as
`docs/recomp/WINDOWS-FORK.md` and `docs/recomp/FORK-COORDINATION.md`; the staged text was
removed so each document keeps one home.
