#!/usr/bin/env python3
"""Deterministic pre-verification checks for an OpenScanner PR.

Usage:
    scripts/verify-pr.py <pr-number> [--build] [--no-build]
    scripts/verify-pr.py --tier <pr-number>      # print: script | agent
    scripts/verify-pr.py --selftest              # assert the tier table, the fork-log rule and the pin (CI runs this)
    scripts/verify-pr.py --pincheck              # assert the recorded contract pin matches the committed header
    scripts/verify-pr.py --staged                # pre-commit mode: check the staged set only (the git hook calls this)

Works on git refs (no checkout required): fetches `refs/pull/<n>/head` and the
PR's base branch, then checks the diff. Exit 0 = all gating checks passed;
1 = a gating check failed; 2 = environment problem.

Tiers (printed with the report; used by verdict-gate.yml to decide whether a PR
is script-gated or needs an agent verdict):
    agent tier   engine/**, tools/**, mesh/**, ui/**, scripts/**, tests/hardware/**,
                 any CMakeLists.txt, .github/**, .claude/**, opencode.json(c),
                 docs/recon/**, docs/recomp/{WINDOWS-FORK,FORK-COORDINATION,ARCHITECTURE}.md,
                 tests/results/** (records, logs, addenda — #21)
    script tier  everything else (PLAN.md, docs/recomp/** except the documents listed above, CLAUDE.md, README.md, ...)
    An empty or unreadable file list is treated as agent tier. The list is
    pinned by `--selftest`; edit the table and the patterns together.

Gating checks
  whitespace      `git diff --check` (the same rule `scripts/git-hooks/pre-commit`
                  runs on the staged set via `--staged`)
  run-records     tests/results/: no existing record modified, deleted or renamed
                  in place (addenda *-correction.md / *-analysis.md / *-addendum.md
                  and `_`-prefixed support files excepted)
  record-provenance
                  an added tests/results/*.md record must be accompanied, in the
                  same PR, by raw evidence: a file whose name starts with the
                  record's stem, **or** a raw artifact the record cites in
                  backticks (so multi-condition runs can name their logs
                  `<slug>-baseline.log` / `<slug>-enabled.log`). Addenda,
                  corrections, analyses and `_`-prefixed support files are exempt.
  contract-pin    the fork's engine/include/openscanner/obscan_api.h must equal the
                  parent commit + sha256 recorded in FORK-COORDINATION.md section 2;
                  a mismatch fails, naming what is recorded and what is present.
                  LIMIT: it reads the RECORDED pin and the COMMITTED header only - the
                  gate does no network fetch, so it cannot detect that the parent
                  has moved to a newer revision.
  plan            PLAN.md row syntax: unique numbers, valid statuses, well-formed.
                  Status moves and new rows are reported, not gated.
  archives        cited `sha256 <hex>` values in changed tests/results/*.md must
                  match a file named next to the citation in the PR; a citation
                  that cannot be resolved fails. Citation resolution covers
                  .ips/.wer/.dmp/.log/.txt/.md (Windows crash artifacts included),
                  so a .wer/.dmp citation is verified, not silently ignored.
                  This half ALWAYS runs. Added *.ips files are compared against
                  ~/Library/Logs/DiagnosticReports/<same basename> when that
                  directory exists; when it does not (any non-macOS host, CI),
                  that half alone is reported N/A with the reason.
  fork-log        a PR that moves engine/include/openscanner/obscan_api.h must also
                  add its line to docs/recomp/FORK-COORDINATION.md §5 — the agreed
                  routine (one line per header change, in the same PR); the fork's
                  --pincheck is the drift alarm.
  hook            the hardware guard satisfies its invariants (must-block /
                  must-allow cases below); a base-vs-head probe matrix diff is
                  reported. macOS runs .claude/hooks/guard-hw-run.sh with bash;
                  Windows runs .claude/hooks/guard-hw-run.ps1 with PowerShell.
                  If PowerShell is absent, the check reports N/A with that reason.
  ui-js-syntax    changed ui/*.js files must parse (`node --check`) — the
                  #54 class: a parse error kills the whole page.
  build           cmake configure + build + ctest -LE hardware; runs only when
                  the working tree is the PR head or --build is passed (CI runs
                  it from the head checkout).

Reported only: tier, inventory (files not mentioned in the PR body), header
(comment-only vs code change in obscan_api.h), modes, hook probe matrix diff.

Fork notes (this copy also runs on Windows): gh and git are resolved via
GH_BIN/GIT_BIN, then PATH, then known Windows installs; the hook-matrix roots
(WT_HW/MAIN/WT_VERIFY) and the repo (VERIFY_PR_REPO) are environment-overridable
with the macOS/parent values as defaults. Every check that is not applicable on
this host is printed as N/A with its reason — never silently skipped.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

STATUSES = {"TODO", "RECON", "SPEC-READY", "BUILD", "HW", "DONE", "BLOCKED"}


def _resolve_tool(env_var, name, fallbacks):
    # Resolve an external tool once: env override, then PATH, then the known
    # Windows install locations. Windows sessions here have neither gh nor git
    # on PATH, so every subprocess below uses GH_BIN/GIT_BIN instead of a bare
    # name, and never relies on the caller's PATH.
    override = os.environ.get(env_var)
    if override:
        return override
    found = shutil.which(name)
    if found:
        return found
    for candidate in fallbacks:
        if os.path.exists(candidate):
            return candidate
    return name  # absent everywhere; let the call fail loudly


GH_BIN = _resolve_tool("GH_BIN", "gh", [r"C:\Users\Bruce\tools\gh\gh.exe"])
GIT_BIN = _resolve_tool("GIT_BIN", "git",
                        [r"C:\Users\Bruce\AppData\Local\GitHubDesktop\app-3.6.7\resources\app\git\cmd\git.exe"])


def _repo_from_remote():
    # gh cannot infer the repo here (it looks for git on PATH, which these
    # sessions do not have), so read origin directly through GIT_BIN.
    r = subprocess.run([GIT_BIN, "remote", "get-url", "origin"], capture_output=True, text=True)
    if r.returncode != 0:
        return None
    m = re.search(r"github\.com[:/]+([^/]+/[^/]+?)(?:\.git)?$", r.stdout.strip())
    return m.group(1) if m else None


def _resolve_repo():
    # The repo the gate judges. VERIFY_PR_REPO overrides; otherwise the
    # checkout's own origin (the parent resolves to the parent, the fork's
    # origin to the fork); the historical parent value is the fallback.
    override = os.environ.get("VERIFY_PR_REPO")
    if override:
        return override
    return _repo_from_remote() or "juicie-brucie/Open3DScanner"


REPO = _resolve_repo()

# Working-tree roots probed by the hook matrix. Defaults are the macOS layout so
# macOS behaviour is unchanged; a Windows host sets these only if it has the
# trees. Left unset on purpose here — nothing is guessed, and the hook check is
# not applicable on Windows anyway (see check 5).
WT_HW = os.environ.get("OPENSCANNER_WT_HW", "/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw")
MAIN = os.environ.get("OPENSCANNER_MAIN", "/Users/brucemackay/DevReposiMac/Open3DScanner")
WT_VERIFY = os.environ.get("OPENSCANNER_WT_VERIFY", "/Users/brucemackay/DevReposiMac/Open3DScanner-wt-verify")
DEVICE_TEST = "./build/tests/hardware/device_message_listener_test/device_message_listener_test"
DATA_TEST = "./build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test"

# The hardware guard the `hook` check exercises. The Unix hook and its .ps1
# replacement are the same PreToolUse guard in two shapes; the OpenCode plugin
# (.opencode/plugins/hw-guard) has its own matrix in tools/hw_guard_test and is
# reported by windows_guard_status(), not here.
GUARD_PATH_UNIX = ".claude/hooks/guard-hw-run.sh"
GUARD_PATH_WINDOWS = ".claude/hooks/guard-hw-run.ps1"

# Paths whose review needs judgment (semantics, prose claims, or the gate itself).
AGENT_PATTERNS = [
    r"^engine/", r"^tools/", r"^mesh/", r"^ui/", r"^scripts/",
    r"^tests/hardware/", r"CMakeLists\.txt$",
    r"^\.github/", r"^\.claude/",
    # The config that steers every agent in this repo (permissions, compaction,
    # MCP): script-tier would let it auto-merge with no judgment pass (rev 14's
    # H4 finding — verified against this script).
    r"^opencode\.jsonc?$",
    r"^docs/recon/", r"^docs/architecture/",
    # Contract-bearing docs under the otherwise script-tier docs/recomp/:
    # the Windows-fork charter and coordination file carry rules (the contract
    # pin, the two-writer rule) and deserve a verdict like docs/recon/**.
    r"^docs/recomp/(WINDOWS-FORK|FORK-COORDINATION)\.md$",
    # The doc that states the roles' walls and the clean-room principle: like the fork
    # documents, it carries rules rather than prose, so it earns a verdict.
    r"^docs/recomp/ARCHITECTURE\.md$",
    # Every evidence artifact under tests/results/** — records, logs and addenda —
    # is agent-tier (#21): the directory is the loop's highest-stakes artifact
    # class, and an added record auto-merging unreviewed was the hole.
    r"^tests/results/",
]


def is_agent_tier(path):
    return any(re.search(p, path) for p in AGENT_PATTERNS)


def fork_log_problem(changed_paths):
    """The agreed routine with the fork lane: a PR that moves the contract header adds its
    §5 line to the coordination file in the same PR. The fork's `--pincheck` is the drift
    alarm; the log is the revision trail, and this keeps it complete without a message per
    move. Returns the problem string, or None when the PR is fine."""
    hdr = "engine/include/openscanner/obscan_api.h"
    forklog = "docs/recomp/FORK-COORDINATION.md"
    if hdr in changed_paths and forklog not in changed_paths:
        return (f"{hdr} moved without a line in {forklog} §5 — the agreed routine is one "
                "line per header change, in the same PR")
    return None


# The tier list is load-bearing: a path that should be agent-tier returning
# `script` lets auto-verdict post success and the PR merges judged by nothing.
# This table pins the list; CI runs `--selftest` before trusting a tier
# (verify.yml: base and PR copies; verdict-gate.yml: before the tier decision).
# Edit the table and the patterns together — a mismatch fails the run.
TIER_CASES = [
    ("engine/src/ws_broadcast_server.cpp", "agent"),
    ("engine/include/openscanner/obscan_api.h", "agent"),
    ("tools/scan_server/main.cpp", "agent"),
    ("mesh/src/kernel.cpp", "agent"),
    ("ui/app.js", "agent"),
    ("scripts/verify-pr.py", "agent"),
    ("tests/hardware/plan56-stall-and-v5-visual.md", "agent"),
    ("docs/recon/findings/scan-live-data.md", "agent"),
    ("docs/architecture/scan-viewer.md", "agent"),
    ("docs/recomp/WINDOWS-FORK.md", "agent"),
    ("docs/recomp/FORK-COORDINATION.md", "agent"),
    ("docs/recomp/ARCHITECTURE.md", "agent"),
    ("tests/results/2026-10-18-run.md", "agent"),
    ("tests/results/2026-10-18-run.log", "agent"),
    ("tests/results/2026-10-18-run.ips", "agent"),
    ("tests/results/2026-10-18-old-addendum.md", "agent"),
    (".github/workflows/verify.yml", "agent"),
    (".claude/hooks/guard-hw-run.sh", "agent"),
    ("opencode.json", "agent"),
    ("opencode.jsonc", "agent"),
    ("CMakeLists.txt", "agent"),
    ("docs/recomp/PLAN.md", "script"),
    ("docs/recomp/ROADMAP.md", "script"),
    ("docs/recomp/log/2026-10-18-entry.md", "script"),
    ("CLAUDE.md", "script"),
    ("README.md", "script"),
    ("docs/research/plane-removal-reverse-engineering.md", "script"),
    ("docs/tooling/rea-ghidra-setup.md", "script"),
]


def is_support_file(path):
    """tests/results/_TEMPLATE.md and friends: they describe how to run, they are
    not records — exempt from the in-place rule and from provenance."""
    return os.path.basename(path).startswith("_")


def is_run_record(path):
    """An added tests/results/*.md is a run record unless it is an addendum or an
    underscore-prefixed support file (_TEMPLATE.md, _NOTES.md, ...). Support files
    describe how to run; they do not report a run, so they need no companion log."""
    return (path.endswith(".md")
            and not is_support_file(path)
            and not re.search(r"-(correction|analysis|addendum)\.md$", path))


def cited_evidence(text):
    """Raw-artifact filenames a record cites in backticks (.log/.txt/.csv/.ips/.py).

    Multi-condition runs legitimately name their logs `<slug>-baseline.log` and
    `<slug>-enabled.log`, which cannot share a prefix with a record named for its
    verdict (`<slug>-confirmed.md`). The provenance rule therefore also accepts a
    citation, provided that artifact is added in the same PR."""
    return {os.path.basename(m.group(1).strip())
            for m in re.finditer(r"`(?:tests/results/)?([A-Za-z0-9_.\-]+\.(?:log|txt|csv|ips|py))`", text)}


def record_accompanied(record, others, text):
    """True when an added record has raw evidence in the same PR: a companion
    whose name starts with the record's stem, or a cited artifact that was added."""
    stem = os.path.basename(record)[:-3]
    if any(o.startswith(stem) for o in others):
        return True
    return bool(cited_evidence(text) & set(others))


# --- contract pin (fork-only; measures the COMMITTED blob) --------------------
# FORK-COORDINATION.md section 2 records the parent commit and the sha256 of the
# header the fork consumes. The pin is a claim about what was committed, so this
# check measures `git cat-file blob <rev>:<path>` - never the working-tree file,
# which can differ through line-ending conversion or a touched mtime.
# WHAT IT DOES NOT COVER: it reads the RECORDED pin and the committed header only;
# it does not fetch the parent, so it cannot detect upstream movement.
PIN_DOC = "docs/recomp/FORK-COORDINATION.md"
PIN_HEADER = "engine/include/openscanner/obscan_api.h"
PIN_ROW = re.compile(
    r"^\|\s*`Open3DScanner-Win64`\s*\(fork\)\s*\|\s*`([0-9a-f]{40})`\s*\|\s*`([0-9a-f]{64})`\s*\|",
    re.M)


def _committed_blob(rev, path):
    """Bytes of <rev>:<path> from git, or None. Never touches the working tree."""
    r = subprocess.run([GIT_BIN, "cat-file", "blob", f"{rev}:{path}"], capture_output=True)
    return r.stdout if r.returncode == 0 else None


def check_contract_pin(rev="HEAD"):
    """Return (ok, detail). Detail names BOTH values on a mismatch."""
    doc_bytes = _committed_blob(rev, PIN_DOC)
    if doc_bytes is None:
        return False, f"check broken: cannot read {rev}:{PIN_DOC} from git"
    doc = doc_bytes.decode("utf-8", "replace")
    m = PIN_ROW.search(doc)
    if not m:
        return False, f"check broken: no fork pin row found in {PIN_DOC} section 2"
    parent_commit, recorded = m.group(1), m.group(2)
    blob = _committed_blob(rev, PIN_HEADER)
    if blob is None:
        return False, f"check broken: cannot read {rev}:{PIN_HEADER} from git"
    present = hashlib.sha256(blob).hexdigest()
    if present != recorded:
        return False, (
            f"HEADER DRIFTED from the recorded pin: FORK-COORDINATION section 2 records sha256 "
            f"{recorded} (parent {parent_commit}), but the committed {PIN_HEADER} ({rev}) has sha256 "
            f"{present}. The synced copy no longer matches the pin - re-sync the header and update the pin.")
    return True, f"recorded {recorded} == committed {present}; parent {parent_commit}"


def selftest():
    bad = []
    for path, want in TIER_CASES:
        got = tier_of([path])
        if got != want:
            bad.append(f"{path}: want {want}, got {got}")
    if tier_of([]) != "agent":
        bad.append("empty list: want agent (unknown -> require a verdict)")
    if tier_of(["docs/recomp/PLAN.md", "ui/app.js"]) != "agent":
        bad.append("mixed list: want agent (one agent path makes the PR agent)")
    # fork-log: the agreed routine's three paths
    H = "engine/include/openscanner/obscan_api.h"; F = "docs/recomp/FORK-COORDINATION.md"
    if fork_log_problem([H, "docs/recon/findings/x.md"]) is None:
        bad.append("fork-log: header-only must be a problem")
    if fork_log_problem([H, F]) is not None:
        bad.append("fork-log: header + its log line must pass")
    if fork_log_problem(["ui/app.js"]) is not None:
        bad.append("fork-log: no header must pass")
    # record-provenance: what counts as a run record
    if not is_run_record("tests/results/2026-10-19-x.md"):
        bad.append("record-provenance: a plain record must be a record")
    if is_run_record("tests/results/_TEMPLATE.md"):
        bad.append("record-provenance: _-prefixed support files must not be records")
    if is_run_record("tests/results/x-correction.md"):
        bad.append("record-provenance: corrections must not be records")
    # record-provenance: a stem companion, or a cited artifact added in the PR
    if not record_accompanied("tests/results/x.md", ["x.log"], ""):
        bad.append("record-provenance: a stem companion must satisfy")
    if record_accompanied("tests/results/x-confirmed.md", ["x-baseline.log"], ""):
        bad.append("record-provenance: an unrelated companion alone must not satisfy")
    if not record_accompanied("tests/results/x-confirmed.md", ["x-baseline.log"],
                              "raw log `tests/results/x-baseline.log`"):
        bad.append("record-provenance: a cited added artifact must satisfy")
    if record_accompanied("tests/results/x-confirmed.md", [],
                          "raw log `tests/results/x-baseline.log`"):
        bad.append("record-provenance: a citation of a file not in the PR must not satisfy")
    # guard selection is LANE-based, not host-based: CI runs on macOS but the
    # framework branch's head has the .ps1, so the .ps1 and its Windows shapes
    # must win there. These cases run on either host.
    guard_checks = 0

    def gcheck(ok, msg):
        nonlocal guard_checks
        guard_checks += 1
        if not ok:
            bad.append(msg)

    framework_head = {GUARD_PATH_WINDOWS}
    framework_changed = [GUARD_PATH_UNIX, GUARD_PATH_WINDOWS]
    gcheck(select_guard(framework_changed, lambda p: p in framework_head) == GUARD_PATH_WINDOWS,
           "guard select: adds .ps1 / deletes .sh -> .ps1 on any host")
    gcheck(select_guard([GUARD_PATH_UNIX], lambda p: p in {GUARD_PATH_UNIX}) == GUARD_PATH_UNIX,
           "guard select: a .sh-only head -> Unix guard")
    gcheck(select_guard([], lambda p: p in framework_head) == GUARD_PATH_WINDOWS,
           "guard select: an unchanged .ps1 head -> Windows guard")
    gcheck(select_guard([], lambda p: False) ==
           (GUARD_PATH_WINDOWS if os.name == "nt" else GUARD_PATH_UNIX),
           "guard select: no facts -> host path (unchanged report)")
    wtext = " ".join(hook_fixtures("windows").values())
    gcheck(not any(s in wtext for s in ("/tmp", "DYLD", ".dylib", "ls -la")),
           "hook fixtures: Windows shapes must not carry macOS literals")
    for token in ("libOrbbecSDK.dll", ".recon-cache", "obscan_api.h", "message_listener_test"):
        gcheck(token in wtext, f"hook fixtures: Windows shapes lost the class token {token}")
    unix_fix = hook_fixtures("unix")
    gcheck("DYLD" in unix_fix["inject"] and unix_fix["tmp"] == "/tmp",
           "hook fixtures: Unix shapes must stay macOS-shaped")
    for kind in ("windows", "unix"):
        runner = available_runner(kind)
        if runner is not None:
            try:
                run_hook(b"", ".", "dir" if kind == "windows" else "ls", kind)
            except GuardRunnerMissing:
                gcheck(False, f"hook runner: {kind} runner {runner} exists but run_hook raised N/A")
            else:
                gcheck(True, "")
        else:
            gcheck(bool(runner_missing_reason(kind)),
                   f"hook runner: {kind} runner absent without a named reason")
    # contract pin: asserted here too, so --selftest covers it wherever it runs
    # (CI runs it on the base copy and the PR copy).
    pin_ok, pin_detail = check_contract_pin()
    for b in bad:
        print("SELFTEST FAIL:", b)
    if not pin_ok:
        print("SELFTEST FAIL:", pin_detail)
    total = len(TIER_CASES) + 12 + guard_checks
    print(f"selftest: {total} cases (tier + fork-log + record-provenance + guard-select), {'FAIL' if bad else 'ok'}")
    print(f"contract-pin: {'ok' if pin_ok else 'FAIL'} - {pin_detail}")
    return 1 if (bad or not pin_ok) else 0


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def git(*args):
    return sh([GIT_BIN] + list(args))


def git_bytes(*args):
    return subprocess.run([GIT_BIN] + list(args), capture_output=True).stdout


# The whitespace rule, in one place: `scripts/git-hooks/pre-commit` runs this
# too, via `verify-pr.py --staged`, so the hook and CI cannot disagree.
# Verbatim run captures are exempt — a raw log is the device's own output
# (trailing spaces and all) and must never be edited to satisfy a linter.
WHITESPACE_EXCLUDES = (":(exclude)tests/results/*.log", ":(exclude)tests/results/*.ips")


def whitespace_check(range_args):
    """`git diff --check` with the repo's exclusions.

    `range_args` is e.g. `["origin/main...HEAD"]` for a PR or `["--cached"]`
    for the staged set. Returns (ok, raw report).
    """
    r = git("diff", "--check", *range_args, "--", ".", *WHITESPACE_EXCLUDES)
    out = (r.stdout + r.stderr).strip()
    return (not out), out


def staged_whitespace():
    """Pre-commit entry point: refuse a commit whose staged content is dirty."""
    ok, out = whitespace_check(["--cached"])
    if ok:
        return 0
    print("pre-commit: whitespace errors in the staged content — refusing the commit:", file=sys.stderr)
    print(out, file=sys.stderr)
    print("Fix the lines above and re-stage. `git commit --no-verify` bypasses this check.", file=sys.stderr)
    return 1


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def pr_files(pr):
    r = sh([GH_BIN, "api", f"repos/{REPO}/pulls/{pr}/files", "--paginate", "--jq", ".[].filename"])
    if r.returncode != 0:
        return None
    return [l for l in r.stdout.splitlines() if l]


def tier_of(paths):
    if not paths:
        return "agent"  # unknown -> require a verdict
    return "agent" if any(is_agent_tier(p) for p in paths) else "script"


# Probe fixtures are chosen by the GUARD KIND (the selected script), never by
# the host running the gate: CI for this fork runs on macOS but the lane's guard
# is the .ps1. The CLASSES are the semantics (vendor artifact, contract header,
# recon-cache, listener test outside/inside the room); the literals are platform
# shapes and stay fixed so a macOS host can exercise the Windows guard.
def hook_fixtures(kind):
    if kind == "windows":
        outside = r"C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-build"
        return {
            "room": r"C:\Users\Bruce\Documents\Github\Open3DScanner-Win64-wt-hw",
            "outside": outside,
            "wt_verify": outside,
            "tmp": r"C:\Users\Bruce\AppData\Local\Temp",
            "device": r"build\tests\hardware\device_message_listener_test\device_message_listener_test.exe",
            "data": r"build\tests\hardware\data_process_message_listener_test\data_process_message_listener_test.exe",
            "inject": 'powershell -c "[Dll]::SetDllDirectory(\'C:\\\\evil\')"',
            "vendor_sdk": r"dumpbin /headers C:\vendor\libOrbbecSDK.dll",
            "vendor_scan": r"dumpbin /headers C:\vendor\lib_orbbec_scan.dll",
            "recon": r"Get-Content .recon-cache\vendor\x.bin",
            "header": r"Get-Content engine\include\openscanner\obscan_api.h",
            "ls": "dir",
        }
    return {
        "room": WT_HW,
        "outside": MAIN,
        "wt_verify": WT_VERIFY,
        "tmp": "/tmp",
        "device": DEVICE_TEST,
        "data": DATA_TEST,
        "inject": "DYLD_INSERT_LIBRARIES=/x ./run",
        "vendor_sdk": "strings libOrbbecSDK.1.7.1.dylib",
        "vendor_scan": "cat liborbbec_scan.3.5.3.dylib",
        "recon": "cat .recon-cache/listings/x.asm",
        "header": "cat engine/include/openscanner/obscan_api.h",
        "ls": "ls -la",
    }


def probe_matrix(kind):
    f = hook_fixtures(kind)
    cases = []
    for cwd_name, cwd in [("wt-hw", f["room"]), ("outside", f["outside"]),
                          ("wt-verify", f["wt_verify"]), ("tmp", f["tmp"])]:
        for cmd_name, cmd in [
            ("device-test", f["device"]),
            ("data-process-test", f["data"]),
            ("cd-hw+device", f"cd {f['room']} && {f['device']}"),
            ("inject", f["inject"]),
            ("recon-cache", f["recon"]),
            ("header", f["header"]),
            ("vendor-sdk", f["vendor_sdk"]),
            ("vendor-scan", f["vendor_scan"]),
            ("ls", f["ls"]),
        ]:
            cases.append((f"{cwd_name}/{cmd_name}", cwd, cmd))
    return cases


def hook_cases(kind):
    """The must-block / must-allow classes, in the selected guard's shapes."""
    f = hook_fixtures(kind)
    must_block = [
        ("inject/tmp", f["tmp"], f["inject"]),
        ("vendor-sdk/tmp", f["tmp"], f["vendor_sdk"]),
        ("vendor-scan/tmp", f["tmp"], f["vendor_scan"]),
        ("recon-cache/tmp", f["tmp"], f["recon"]),
        ("header/tmp", f["tmp"], f["header"]),
        ("device/outside", f["outside"], f["device"]),
        ("device/wt-verify", f["wt_verify"], f["device"]),
        ("device/tmp", f["tmp"], f["device"]),
        ("data/outside", f["outside"], f["data"]),
        ("data/wt-verify", f["wt_verify"], f["data"]),
        ("data/tmp", f["tmp"], f["data"]),
    ]
    must_allow = [
        ("device/room", f["room"], f["device"]),
        ("data/room", f["room"], f["data"]),
        ("device/cd-room", f["tmp"], f"cd {f['room']} && {f['device']}"),
        ("ls/tmp", f["tmp"], f["ls"]),
    ]
    return must_block, must_allow


class GuardRunnerMissing(Exception):
    """The runner for the SELECTED guard is not installed on this host."""


def guard_kind(path):
    return "windows" if path.endswith(".ps1") else "unix"


def select_guard(changed_paths, head_has):
    """Pick the guard this LANE uses, not the host's.

    `head_has(path)` says whether the path exists in the head tree; the diff
    names every path the PR adds, changes or deletes. A .ps1 present in either
    wins because this fork's lane is Windows - even when the gate runs on the
    macOS runner. With no facts at all, keep the host's path for the
    "unchanged" report."""
    changed = set(changed_paths)
    if GUARD_PATH_WINDOWS in changed or head_has(GUARD_PATH_WINDOWS):
        return GUARD_PATH_WINDOWS
    if GUARD_PATH_UNIX in changed or head_has(GUARD_PATH_UNIX):
        return GUARD_PATH_UNIX
    return GUARD_PATH_WINDOWS if os.name == "nt" else GUARD_PATH_UNIX


def available_runner(kind):
    if kind == "windows":
        for exe in ("powershell", "pwsh"):
            if shutil.which(exe):
                return exe
        return None
    return shutil.which("bash")


def runner_missing_reason(kind):
    if kind == "windows":
        return "no PowerShell runner found (tried powershell, pwsh)"
    return "no bash found"


def run_hook(script_bytes, cwd, cmd, kind):
    """Run the SELECTED guard with one probe. Same JSON stdin contract and the
    same exit-code meaning (0 = allow, non-zero = block) for both shapes."""
    runner = available_runner(kind)
    if runner is None:
        raise GuardRunnerMissing(runner_missing_reason(kind))
    payload = json.dumps({"cwd": cwd, "tool_name": "Bash", "tool_input": {"command": cmd}})
    with tempfile.TemporaryDirectory() as td:
        if kind == "windows":
            sp = os.path.join(td, "hook.ps1")
            with open(sp, "wb") as f:
                f.write(script_bytes)
            try:
                # ExecutionPolicy is a Windows-platform setting; skip it off
                # Windows so the same invocation works with pwsh on macOS.
                cmd = [runner, "-NoProfile"]
                if os.name == "nt":
                    cmd += ["-ExecutionPolicy", "Bypass"]
                cmd += ["-File", sp]
                r = subprocess.run(cmd, input=payload, capture_output=True, text=True)
            except FileNotFoundError:
                raise GuardRunnerMissing(runner_missing_reason(kind))
            return r.returncode
        sp = os.path.join(td, "hook.sh")
        with open(sp, "wb") as f:
            f.write(script_bytes)
        os.chmod(sp, 0o755)
        arg = sp.replace("\\", "/") if os.name == "nt" else sp
        try:
            r = subprocess.run([runner, arg], input=payload, capture_output=True, text=True)
        except FileNotFoundError:
            raise GuardRunnerMissing(runner_missing_reason(kind))
        return r.returncode


def run_probes(script_bytes, matrix, kind):
    return {label: ("allow" if run_hook(script_bytes, cwd, cmd, kind) == 0 else "block")
            for label, cwd, cmd in matrix}


def check_hook_invariants(script_bytes, kind):
    must_block, must_allow = hook_cases(kind)
    bad = []
    for label, cwd, cmd in must_block:
        if run_hook(script_bytes, cwd, cmd, kind) == 0:
            bad.append(f"must-block case allowed: {label}")
    for label, cwd, cmd in must_allow:
        if run_hook(script_bytes, cwd, cmd, kind) != 0:
            bad.append(f"must-allow case blocked: {label}")
    return bad


def status_of(line):
    cells = line.split("|")
    return cells[3].strip() if len(cells) > 4 else "?"


def _wer_text(raw):
    """WER writes Report.wer as UTF-16LE (sometimes with a BOM), not UTF-8."""
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16", "replace")
    if raw[:400].count(b"\x00") > 100:
        return raw.decode("utf-16-le", "replace")
    return raw.decode("utf-8", "replace")


def _wer_fields(raw):
    fields = {}
    for line in _wer_text(raw).splitlines():
        m = re.match(r"^([A-Za-z][A-Za-z0-9_]*)=(.*)$", line.strip())
        if m:
            fields[m.group(1)] = m.group(2).strip()
    return fields


def _wer_compare(added_bytes, wer_dir):
    """Compare an added WER artifact to the local WER report, by the report's OWN
    fields (AppName/AppPath + EventTime), never by filename.
    Returns 'verified' | 'mismatch' | 'unmatched' | 'unreadable'."""
    if not added_bytes:
        return "unmatched"
    try:
        entries = os.listdir(wer_dir)
    except OSError:
        return "unreadable"
    if "AppName=" in _wer_text(added_bytes)[:8192]:
        af = _wer_fields(added_bytes)
        a_app = (af.get("AppName") or "").lower()
        a_path = (af.get("AppPath") or "").lower()
        a_time = (af.get("EventTime") or "").strip()
        if not a_app:
            return "unmatched"
        for name in entries:
            src = os.path.join(wer_dir, name, "Report.wer")
            try:
                b = open(src, "rb").read()
            except OSError:
                continue
            f = _wer_fields(b)
            if (f.get("AppName") or "").lower() != a_app:
                continue
            if a_path and (f.get("AppPath") or "").lower() != a_path:
                continue
            if a_time and (f.get("EventTime") or "").strip() != a_time:
                continue
            return "verified" if b == added_bytes else "mismatch"
        return "unmatched"
    # A dump: match by content against any local WER dump.
    for name in entries:
        d = os.path.join(wer_dir, name)
        try:
            files = os.listdir(d)
        except OSError:
            continue
        for fn in files:
            if not fn.lower().endswith((".dmp", ".hdmp")):
                continue
            try:
                if open(os.path.join(d, fn), "rb").read() == added_bytes:
                    return "verified"
            except OSError:
                continue
    return "unmatched"


def main():
    argv = sys.argv[1:]
    # Pre-commit mode: the staged set, no PR/network. Kept first so the hook is
    # independent of gh and of any PR state.
    if "--staged" in argv:
        return staged_whitespace()
    build_flag = "--build" in argv
    no_build = "--no-build" in argv
    tier_mode = "--tier" in argv
    self_test = "--selftest" in argv
    if self_test:
        return selftest()
    if "--pincheck" in argv:
        ok, detail = check_contract_pin()
        print(f"contract-pin: {'ok' if ok else 'FAIL'} - {detail}")
        return 0 if ok else 1

    nums = [a for a in argv if a.isdigit()]
    if not nums:
        print(__doc__)
        return 2
    pr = nums[0]

    if tier_mode:
        files = pr_files(pr)
        print(tier_of(files))
        return 0

    r = sh([GH_BIN, "pr", "view", pr, "--repo", REPO,
            "--json", "number,state,baseRefName,baseRefOid,headRefOid,body,url,mergeCommit"])
    if r.returncode != 0:
        print(f"ENV: gh pr view {pr} failed: {r.stderr.strip()}")
        return 2
    meta = json.loads(r.stdout)
    state = meta["state"]
    head_sha = meta["headRefOid"]
    base_name = meta["baseRefName"]
    body = meta["body"] or ""
    changed = pr_files(pr) or []
    tier = tier_of(changed)

    if git("fetch", "-q", "origin", base_name).returncode != 0:
        print("ENV: git fetch of base branch failed")
        return 2
    head_ref = "HEAD"
    if git("rev-parse", "HEAD").stdout.strip() != head_sha:
        # --force: an earlier read may have created this ref, and a rebased or
        # amended head makes the update non-fast-forward (which used to fail
        # as a misleading "ENV: fetch of refs/pull/... failed").
        if git("fetch", "-q", "--force", "origin", f"pull/{pr}/head:refs/verify/pr-{pr}").returncode != 0:
            print(f"ENV: fetch of refs/pull/{pr}/head failed")
            return 2
        head_ref = f"refs/verify/pr-{pr}"
    # For a merged PR, diff against the merge commit's first parent; the head
    # is already an ancestor of the base branch, so `origin/<base>...head`
    # would be empty.
    if state == "MERGED" and (meta.get("mergeCommit") or {}).get("oid"):
        base_ref = f"{meta['mergeCommit']['oid']}^1"
    else:
        base_ref = f"origin/{base_name}"
    if git("rev-parse", "--verify", "-q", base_ref).returncode != 0:
        print(f"ENV: base ref {base_ref} does not resolve")
        return 2
    got = git("rev-parse", head_ref).stdout.strip()
    if got != head_sha:
        print(f"ENV: fetched head {got[:12]} != PR head {head_sha[:12]}")
        return 2

    report = []
    fails = []

    def add(name, ok, detail):
        report.append((name, "PASS" if ok else "FAIL", detail))
        if not ok:
            fails.append(name)

    # 1. whitespace — the shared rule (see whitespace_check; the pre-commit
    #    hook runs the same code on the staged set).
    ws_ok, ws_out = whitespace_check([f"{base_ref}...{head_ref}"])
    add("whitespace", ws_ok, ws_out[:400] or "clean")

    # 2. run records never edited, deleted or renamed in place
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    bad = []
    exempted = 0
    for line in r.stdout.splitlines():
        st, path = line.split("\t")[0], line.split("\t")[-1]
        bn = os.path.basename(path)
        is_addendum = (bn.endswith("-correction.md") or bn.endswith("-analysis.md")
                       or bn.endswith("-addendum.md"))
        if st.startswith("M"):
            if is_addendum or is_support_file(path):
                exempted += 1
            else:
                bad.append(f"modified in place: {path}")
        elif st.startswith(("D", "R")):
            bad.append(f"deleted/renamed: {path}")
    add("run-records", not bad,
        "; ".join(bad) if bad else
        (f"{exempted} modified addendum/support file(s) allowed" if exempted else "no modifications"))

    # 2b. added records need a companion raw artifact in the same PR (#21)
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    added_res = [l.split("\t")[-1] for l in r.stdout.splitlines() if l.startswith("A")]
    records = [p for p in added_res if is_run_record(p)]
    others = [os.path.basename(p) for p in added_res
              if p not in records and not p.endswith(".md")]
    missing = []
    for p in records:
        stem_ok = any(o.startswith(os.path.basename(p)[:-3]) for o in others)
        text = "" if stem_ok else git("show", f"{head_ref}:{p}").stdout
        if not record_accompanied(p, others, text):
            missing.append(p)
    add("record-provenance", not missing,
        "; ".join(f"no raw evidence in this PR (companion or cited artifact): {p}" for p in missing)
        if missing
        else (f"{len(records)} record(s) with evidence" if records
              else "no records added"))

    # 2c. contract pin: the fork's synced header must equal the recorded
    #     FORK-COORDINATION.md section 2 pin (parent commit + sha256).
    pin_ok, pin_detail = check_contract_pin()
    add("contract-pin", pin_ok, pin_detail)

    # 3. plan: syntax gated; status moves and new rows reported only
    r = git("diff", "--name-only", f"{base_ref}...{head_ref}", "--", "docs/recomp/PLAN.md")
    if r.stdout.strip():
        head_plan = git("show", f"{head_ref}:docs/recomp/PLAN.md").stdout
        base_plan = git("show", f"{base_ref}:docs/recomp/PLAN.md").stdout
        hrows, brows = {}, {}
        for line in head_plan.splitlines():
            m = re.match(r"\|\s*(\d+)\s*\|", line)
            if m:
                hrows[int(m.group(1))] = line
        for line in base_plan.splitlines():
            m = re.match(r"\|\s*(\d+)\s*\|", line)
            if m:
                brows[int(m.group(1))] = line
        probs = []
        nums_seen = [m.group(1) for m in re.finditer(r"^\|\s*(\d+)\s*\|", head_plan, re.M)]
        dups = sorted({n for n in nums_seen if nums_seen.count(n) > 1})
        if dups:
            probs.append("duplicate row numbers: " + ", ".join(dups))
        for n, line in sorted(hrows.items()):
            cells = line.split("|")
            if len(cells) < 6:
                probs.append(f"row {n}: malformed")
            elif cells[3].strip() not in STATUSES:
                probs.append(f"row {n}: bad status {cells[3].strip()!r}")
        moved = [f"#{n}: {status_of(brows[n])} -> {status_of(hrows[n])}"
                 for n in sorted(hrows) if n in brows and status_of(brows[n]) != status_of(hrows[n])]
        added = [f"#{n}" for n in sorted(hrows) if n not in brows]
        detail = "; ".join(probs) if probs else \
            (("moves (reported): " + ", ".join(moved)) if moved else "no status moves") + \
            (f"; new rows: {', '.join(added)}" if added else "")
        add("plan", not probs, detail)
    else:
        report.append(("plan", "INFO", "PLAN.md unchanged"))

    # 4. archival claims
    diff_files = git("diff", "--name-status", f"{base_ref}...{head_ref}").stdout
    added = [l.split("\t")[-1] for l in diff_files.splitlines() if l.startswith("A")]
    md_touched = [l.split("\t")[-1] for l in diff_files.splitlines()
                  if re.search(r"tests/results/.*\.md$", l.split("\t")[-1])]
    arch_problems, arch_verified, arch_local = [], 0, 0
    # The cited-sha256 half below ALWAYS runs. The crash-artifact comparison uses
    # the platform's own source: macOS ~/Library/Logs/DiagnosticReports by
    # basename; Windows WER ReportArchive, matched on the report's OWN
    # AppName/AppPath + EventTime (never a filename). If the source is absent or
    # unreadable the check says so LOUDLY - it never silently skips.
    if os.name == "nt":
        diag_dir = r"C:\ProgramData\Microsoft\Windows\WER\ReportArchive"
    else:
        diag_dir = os.path.expanduser("~/Library/Logs/DiagnosticReports")
    diag_available = os.path.isdir(diag_dir)
    diag_note = ""
    if diag_available:
        try:
            os.listdir(diag_dir)
        except OSError as e:
            diag_available = False
            diag_note = (f"crash-artifact comparison NOT PERFORMED: {diag_dir} is not readable "
                         f"({e.strerror}); added artifacts are verified by their cited sha256 only")
    if not diag_available and not diag_note:
        where = "admin-protected/absent" if os.name == "nt" else "macOS-only"
        diag_note = (f"crash-artifact comparison NOT PERFORMED: {diag_dir} not present ({where}); "
                     f"added artifacts are verified by their cited sha256 only")
    if diag_available:
        for path in added:
            bn = os.path.basename(path)
            if os.name == "nt":
                if not path.endswith((".wer", ".dmp", ".hdmp")):
                    continue
                status = _wer_compare(git_bytes("show", f"{head_ref}:{path}"), diag_dir)
                if status == "verified":
                    arch_local += 1
                elif status == "mismatch":
                    arch_problems.append(
                        f"{bn}: does not match the local WER report (same AppName/AppPath + EventTime)")
                elif status == "unreadable":
                    diag_note = f"crash-artifact comparison NOT PERFORMED: {diag_dir} became unreadable"
                else:
                    diag_note = (f"crash-artifact comparison NOT PERFORMED for {bn}: no local WER report "
                                 f"matches its AppName/AppPath + EventTime")
            else:
                if not path.endswith(".ips"):
                    continue
                src = os.path.join(diag_dir, bn)
                if os.path.exists(src):
                    arch_local += 1
                    if sha256_bytes(open(src, "rb").read()) != sha256_bytes(git_bytes("show", f"{head_ref}:{path}")):
                        arch_problems.append(f"{bn}: sha256 mismatch vs {src}")
    # Cited-sha256 resolution covers .ips/.wer/.dmp/.md/.log/.txt, so a record
    # citing a Windows crash artifact (.wer/.dmp) is verified, not silently ignored.
    for path in md_touched:
        text = git("show", f"{head_ref}:{path}").stdout
        for m in re.finditer(r"sha256\s*`([0-9a-f]{64})`", text):
            cited = m.group(1)
            ctx = text[max(0, m.start() - 300):m.start()]
            names = [a or b for a, b in re.findall(r"\(\./([^)]+)\)|`([^`]+\.(?:ips|wer|dmp|md|log|txt))`", ctx)]
            resolved = False
            for name in names[::-1]:
                p = f"tests/results/{name}"
                if git("cat-file", "-e", f"{head_ref}:{p}").returncode == 0:
                    if sha256_bytes(git_bytes("show", f"{head_ref}:{p}")) == cited:
                        arch_verified += 1
                        resolved = True
                    else:
                        arch_problems.append(f"{path}: cited sha256 does not match {p}")
                        resolved = True
                    break
            if not resolved:
                arch_problems.append(
                    f"{path}: cited sha256 {cited[:12]}… does not resolve to a file named next to it")
    arch_detail = "; ".join(filter(None, [
        f"{arch_verified} cited hash(es) verified" if arch_verified else "",
        f"{arch_local} crash artifact(s) compared against the local source" if arch_local else "",
        diag_note,
    ])) or "no archival claims"
    add("archives", not arch_problems,
        "; ".join(arch_problems) if arch_problems else arch_detail)

    # 5. hook invariants + matrix diff.
    #    The guard is selected by the LANE (the head tree and the PR diff), not
    #    by the host running the gate: this fork's .ps1 must be exercised by the
    #    macOS CI runner with Windows-shaped probes, and its .sh by any host
    #    with bash. The must-block / must-allow classes are the same in both
    #    shapes. A missing runner is N/A with the reason, never a false pass.
    changed_hook_paths = [l.split("\t")[-1] for l in diff_files.splitlines()]

    def head_has(path):
        return git("cat-file", "-e", f"{head_ref}:{path}").returncode == 0

    hook = select_guard(changed_hook_paths, head_has)
    kind = guard_kind(hook)
    if hook in changed_hook_paths:
        if available_runner(kind) is None:
            report.append(("hook", "N/A",
                           runner_missing_reason(kind) +
                           "; the guard hook cannot be exercised, so the check is not silently green"))
        else:
            head_hook = git_bytes("show", f"{head_ref}:{hook}")
            base_has = git("cat-file", "-e", f"{base_ref}:{hook}").returncode == 0
            base_hook = git_bytes("show", f"{base_ref}:{hook}") if base_has else None
            problems = []
            if kind == "unix":
                # The bash hook must be executable; .ps1 is not mode-gated.
                mode = git("ls-tree", head_ref, "--", hook).stdout.split()
                mode = mode[0] if mode else "?"
                if mode != "100755":
                    problems.append(f"mode {mode} (want 100755)")
            matrix = probe_matrix(kind)
            try:
                problems += check_hook_invariants(head_hook, kind)
                if base_hook is None:
                    diffs, probe_detail = [], f"{len(matrix)} cases; no base guard to compare"
                else:
                    rb = run_probes(base_hook, matrix, kind)
                    rh = run_probes(head_hook, matrix, kind)
                    diffs = [f"{label}: {rb[label]} -> {rh[label]}"
                             for label in rb if rb[label] != rh[label]]
                    probe_detail = (f"{len(matrix)} cases; diffs: " +
                                    ("; ".join(diffs) if diffs else "none"))
            except GuardRunnerMissing as e:
                report.append(("hook", "N/A",
                               f"{e}; the guard hook cannot be exercised, so the check is not silently green"))
            else:
                label = ("guard invariants hold" if kind == "windows"
                         else "executable (100755); guard invariants hold")
                add("hook", not problems, "; ".join(problems) if problems else label)
                report.append(("hook-probes", "INFO", probe_detail))
    else:
        report.append(("hook", "INFO", "unchanged"))

    # 6. header classification
    hdr = "engine/include/openscanner/obscan_api.h"
    if hdr in [l.split("\t")[-1] for l in diff_files.splitlines()]:
        def strip_cpp(t):
            t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
            t = re.sub(r"//[^\n]*", "", t)
            return re.sub(r"\s+", " ", t).strip()
        b = strip_cpp(git("show", f"{base_ref}:{hdr}").stdout)
        h = strip_cpp(git("show", f"{head_ref}:{hdr}").stdout)
        report.append(("header", "INFO",
                       "comment-only change" if b == h else "code text changed — semantic review needed"))
    else:
        report.append(("header", "INFO", "unchanged"))

    # 6b. UI JavaScript parse check — the #54 class: a duplicate binding in
    # ui/app.js killed the whole page, and nothing in this gate parsed the
    # page's scripts. Changed ui/*.js must parse (node; module-or-script per
    # the file's own import/export usage).
    js_files = [p for p in changed if re.match(r"^ui/.*\.js$", p)]
    if not js_files:
        report.append(("ui-js-syntax", "INFO", "no ui JS changed"))
    elif shutil.which("node") is None:
        report.append(("ui-js-syntax", "INFO", "node not found on PATH — skipped (CI covers)"))
    else:
        js_bad, js_checked = [], 0
        for p in js_files:
            if git("cat-file", "-e", f"{head_ref}:{p}").returncode != 0:
                continue  # deleted in this PR
            content = git_bytes("show", f"{head_ref}:{p}")
            esm = bool(re.search(rb"(?m)^\s*(import|export)\b", content))
            tmp = os.path.join(tempfile.gettempdir(),
                               f"verify-pr-{pr}-{os.path.basename(p)}{'.mjs' if esm else '.js'}")
            with open(tmp, "wb") as fh:
                fh.write(content)
            r = sh(["node", "--check", tmp])
            js_checked += 1
            if r.returncode != 0:
                first = (r.stderr.strip().splitlines() or ["parse failed"])[0]
                js_bad.append(f"{p}: {first[:160]}")
        add("ui-js-syntax", not js_bad,
            "; ".join(js_bad) if js_bad else f"{js_checked} file(s) parse")

    # 6b. fork-log: the agreed routine — a PR that moves the contract header carries its §5
    # line in the coordination file, in the same PR (the fork's --pincheck is the drift alarm).
    changed = [l.split("\t")[-1] for l in diff_files.splitlines()]
    fl = fork_log_problem(changed)
    add("fork-log", fl is None,
        fl if fl else ("header unchanged" if "engine/include/openscanner/obscan_api.h" not in changed
                       else "header + its fork-log line together"))

    # 7. inventory vs PR body
    body_l = body.lower()

    def mentioned(p):
        # Full path, basename, or the immediate parent directory name (so
        # "tests/hardware/<name>/main.cpp" counts when the body names <name>).
        # Deliberately no broader segment matching: it masked unclaimed files.
        if p in body or os.path.basename(p) in body:
            return True
        if p == "docs/recomp/PLAN.md":
            return True  # the board is routinely touched without being named
        parent = os.path.dirname(p).split("/")[-1]
        if len(parent) >= 5 and parent.lower() in body_l:
            return True
        return False

    unmentioned = [p for p in changed if not mentioned(p)]
    report.append(("inventory", "INFO",
                   f"{len(changed)} file(s); not mentioned in PR body: "
                   + (", ".join(unmentioned[:8]) + ("…" if len(unmentioned) > 8 else "")
                      if unmentioned else "none")))

    # 8. mode changes
    r = git("diff", "--summary", f"{base_ref}...{head_ref}")
    modes = [l for l in r.stdout.splitlines() if "mode change" in l]
    report.append(("modes", "INFO", "; ".join(modes) if modes else "no mode changes"))

    # 9. build + ctest
    at_head = git("rev-parse", "HEAD").stdout.strip() == head_sha
    if no_build:
        report.append(("build", "INFO", "skipped (--no-build)"))
    elif at_head or build_flag:
        cfg = sh(["cmake", "-S", ".", "-B", "build"])
        if cfg.returncode != 0:
            # A gate that fails without a diagnostic costs a cycle every time it
            # fires: print the tail so the CI log carries the actual error.
            print("--- cmake configure failed; last 40 lines of output ---")
            print("\n".join((cfg.stdout + cfg.stderr).strip().splitlines()[-40:]))
            add("build", False, "cmake configure failed (tail printed above)")
        else:
            bld = sh(["cmake", "--build", "build", "-j"])
            if bld.returncode != 0:
                print("--- cmake build failed; last 40 lines of output ---")
                print("\n".join((bld.stdout + bld.stderr).strip().splitlines()[-40:]))
                add("build", False, "cmake build failed (tail printed above)")
            else:
                ct = sh(["ctest", "--test-dir", "build", "-LE", "hardware", "--output-on-failure"])
                no_tests = "No tests were found" in (ct.stdout + ct.stderr)
                add("build", ct.returncode == 0 or no_tests,
                    "build ok; ctest: " + ("no tests selected" if no_tests else f"exit {ct.returncode}"))
    else:
        report.append(("build", "INFO", "skipped (working tree is not the PR head; runs in CI)"))

    # --- report
    short = head_sha[:12]
    print(f"# verify-pr — PR #{pr} (head `{short}`, base `{base_name}`, tier **{tier}**)")
    print()
    print("| check | result | detail |")
    print("|---|---|---|")
    for name, st, detail in report:
        print(f"| {name} | {st} | {detail.replace('|', '/')} |")
    print()
    checks = ["whitespace", "run-records", "record-provenance", "contract-pin", "plan",
              "archives", "hook", "ui-js-syntax", "fork-log", "build"]
    print("Gating checks: " + ", ".join(checks) + ".")
    if fails:
        print()
        print(f"**FAILED: {', '.join(fails)}**")
        return 1
    print()
    print("All gating checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
