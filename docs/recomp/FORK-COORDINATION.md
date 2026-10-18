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
| Windows x64 | _(none yet — open question 1)_ | | | |

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
