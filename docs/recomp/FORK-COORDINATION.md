# Fork coordination — `Open3DScanner` ↔ `Open3DScanner-Win64`

The two repos run independent loops and share one contract. This file is the cross-repo
coordination record: the contract pin, the platform registry, and an append-only log.

Owner: the parent's `openscanner/planner`. The fork keeps its own copy, seeded from this one.

## 1. The two-writer problem, and the rule

Both loops append to a log; neither can write the other's repo. So:

- **One writer per file.** This file is owned by the parent's planner. The fork keeps its own
  copy, seeded from here.
- **The parent's copy is canonical** for cross-platform decisions. A fork decision that
  affects the parent is folded back by the operator (or a fork→parent PR) — never by an agent
  writing across repos.
- **Entries are append-only, dated, and prefixed** `[mac]` / `[win]`. Appending is the only
  edit; nothing above the last entry is rewritten. That keeps a merge trivial.

## 2. The contract pin

The header is single-sourced in the parent (see `WINDOWS-FORK.md` §2). Each repo records the
revision it consumes and checks it in CI, so a stale copy fails loudly instead of quietly
diverging.

| Repo | Parent commit | `obscan_api.h` sha256 | Checked in CI |
| --- | --- | --- | --- |
| `Open3DScanner` (parent) | `main` (self) | regenerated per commit | n/a |
| `Open3DScanner-Win64` (fork) | `dac0e7dedd6674335d7b4abb4e65f9efd540ccbc` | `e6946c5e3110f179d4ac090e2c371d145bd108e2b068557f0ca56bbecd09018c` | not yet wired |

## 3. Platform registry

One row per artifact a platform's recon depends on. Vendor artifacts are never committed; only
their pins are.

| Platform | Artifact | Role | sha256 | Pinned by |
| --- | --- | --- | --- | --- |
| macOS arm64 | the parent's scan dylib | the parent's recon target | per its findings docs | parent findings |
| Windows x64 | `lib_orbbec_scan.dll` (installed CrealityScan 4.3.2) | the fork's recon target — carries the `obscan_*` surface | `9E7B677191A1460E620F2089D972CC231BCCF27666565A1DBF1FFA1566E48787` | [`win64-enum-parity.md`](../recon/findings/win64-enum-parity.md) |
| Windows x64 | `OrbbecSDK.dll` | the SDK layer beside it — no readable `VS_VERSION_INFO` | `D36C0E8EA1E2123C5BC8288D8676608DA255BFFD8063E2B051DCF39C3FA79231` | same doc |
| Windows x64 | `OrbbecGPL.dll` | beside it — no readable version resource | `2CB4622CFDAB5C5F4DAF9BE7992EF0D7012730940BA0F3C56D3D4DAFF67091DE` | same doc |

## 4. Seeding the fork

The fork receives this file at repo-creation time with its **`[win]` seed entry already
written**, so the log exists as a log before anyone needs it and the fork's first dispatch has
somewhere to record. Seeded once, by whoever creates the repo; after that it is appended only
by the fork's own planner.

For the fork's copy: keep the parent's `[mac]` lines as history and add the seed below.

- `[win]` 2026-10-18 — repo `Open3DScanner-Win64` created; `WINDOWS-FORK.md` and this file
  seeded from the parent at `<parent-commit>` (the contract pin's parent commit, §2). Contract
  pin not yet wired. Blocked on: does a Windows `obscan_*` binary exist?

## 5. Log

_Append dated one-liners: what changed, which side, and the outcome. Newest last._

- `[mac]` 2026-10-18 — Windows fork chartered; repo named `Open3DScanner-Win64`; contract
  single-sourcing and the pin rule agreed. Blocked on: does a Windows `obscan_*` binary exist?
- `[mac]` 2026-10-18 — coordination channel confirmed live: `Windows11` registered in the
  desktop app's server list and answering (OpenCode 2.0.32); `scripts/remote-opencode.sh` named
  as the channel in `WINDOWS-FORK.md` §5.

- `[win]` 2026-10-18 — repo `Open3DScanner-Win64` created (standalone: `origin` is this repo, `upstream` is the parent); `WINDOWS-FORK.md` and this file seeded from the parent at `dac0e7dedd6674335d7b4abb4e65f9efd540ccbc` (the contract pin's parent commit, §2). Contract pin not yet wired. Gate answered: the Windows build ships the same `obscan_*` surface (a superset; both scan libraries are build 3.5.3.260921) — see the parent's `docs/recon/findings/win64-surface.md`. Next: the Win64 toolchain so the header's `_Static_assert`s can compile (with Bruce).

- `[mac]` 2026-10-18 — the initialization commits on this repo's `main` (the seed itself, the
  `[win]` entry, §4's room rule, the pin row) were **direct pushes, not PRs — the bootstrap
  exception**, recorded here so the history does not read as licence to skip §4. From the first
  change after initialization, changes land through PRs with a verdict, once this repo's gate is
  wired (the Windows port of `verify-pr`, §4).
- `[win]` 2026-10-18 — the gate is wired: `verify-pr.py` ported to Windows (env-configurable
  worktree paths, repo resolved from `origin` rather than `gh`'s inference, `gh`/`git` tool
  resolution, the macOS-only halves reported NOT APPLICABLE with their reason rather than
  passing), `--selftest` ported with the tier table kept identical to the parent's, and the
  contract pin now **enforced** by `--pincheck`, which fails naming the recorded and present
  values. The pin's value is meaningful for the first time: it has moved once upstream, so it
  has something to catch. PRs #3 and #7.
- `[win]` 2026-10-18 — the hardware backstop exists (§7 was previously unbacked on this
  platform): an OpenCode plugin plus a static deny-only fallback in `opencode.jsonc`, the
  cwd-sensitive listener-test rule kept in the plugin because a static rule cannot see the
  working directory. PR #4. §8 item 4 closed.
- `[win]` 2026-10-18 — session watcher ported to PowerShell (PR #5); platform registry row 1
  set above; enum parity read against the installed DLL: 90 of 103 key values identical to
  macOS, the 13 not found attributed **all Windows-side, zero macOS-side residuals**, and
  tagged `[inferred]` rather than parity. PR #8.
- `[win]` 2026-10-18 — known defect, worth carrying: `gh` resolves the repository from the
  `upstream` remote rather than `origin`, so a bare `gh pr view N` in a fork room asks about
  the **parent's** PR N and can report a merged PR as open (observed on #7, false `MERGED`).
  Pass `-R` explicitly or set `GH_REPO`. `verify-pr.py` is unaffected because it derives the
  repo from `origin`. Fix in flight for `watch-sessions.ps1`; the bash original has the same
  latent behaviour.
