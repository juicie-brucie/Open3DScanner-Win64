#!/usr/bin/env python3
"""Deterministic pre-verification checks for an OpenScanner PR.

Usage:
    scripts/verify-pr.py <pr-number> [--build] [--no-build]
    scripts/verify-pr.py --tier <pr-number>      # print: script | agent
    scripts/verify-pr.py --selftest              # assert the tier table (CI runs this)
    scripts/verify-pr.py --pincheck              # assert the recorded contract pin matches the header

Works on git refs (no checkout required): fetches `refs/pull/<n>/head` and the
PR's base branch, then checks the diff. Exit 0 = all gating checks passed;
1 = a gating check failed; 2 = environment problem.

Tiers (printed with the report; used by verdict-gate.yml to decide whether a PR
is script-gated or needs an agent verdict):
    agent tier   engine/**, tools/**, mesh/**, ui/**, scripts/**, tests/hardware/**,
                 any CMakeLists.txt, .github/**, .claude/**, docs/recon/**,
                 docs/recomp/{WINDOWS-FORK,FORK-COORDINATION}.md,
                 tests/results/** (records, logs, addenda — #21)
    script tier  everything else (PLAN.md, docs/recomp/** except the two fork
                 documents above, CLAUDE.md, README.md, ...)
    An empty or unreadable file list is treated as agent tier. The list is
    pinned by `--selftest`; edit the table and the patterns together.

Gating checks
  whitespace      `git diff --check`
  run-records     tests/results/: no existing record modified, deleted or renamed
                  in place (addenda *-correction.md / *-analysis.md / *-addendum.md excepted)
  record-provenance
                  an added tests/results/*.md record must be accompanied, in the
                  same PR, by an added raw artifact sharing its stem (typically
                  its .log, non-.md); addenda/corrections/analyses are exempt.
  contract-pin    the fork's engine/include/openscanner/obscan_api.h must equal the
                  parent commit + sha256 recorded in FORK-COORDINATION.md section 2;
                  a mismatch fails, naming what is recorded and what is present.
                  LIMIT: it compares the local copy to the RECORDED pin only - the
                  gate does no network fetch, so it cannot detect that the parent
                  has moved to a newer revision.
  plan            PLAN.md row syntax: unique numbers, valid statuses, well-formed.
                  Status moves and new rows are reported, not gated.
  archives        cited `sha256 <hex>` values in changed tests/results/*.md must
                  match a file named next to the citation in the PR; a citation
                  that cannot be resolved fails. This half ALWAYS runs. Added
                  *.ips files are compared against
                  ~/Library/Logs/DiagnosticReports/<same basename> when that
                  directory exists; when it does not (any non-macOS host, CI),
                  that half alone is reported N/A with the reason.
  hook            macOS: .claude/hooks/guard-hw-run.sh is executable, and the
                  head hook satisfies the guard invariants (must-block /
                  must-allow cases below); a base-vs-head probe matrix diff is
                  reported. Windows: that hook is bash/POSIX and the OpenCode
                  replacement (charter section 8 item 4) may not be in the tree,
                  so the check is reported N/A with its reason — never green.
                  windows_guard_status() is the seam that will run the
                  replacement's own invariants.
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

# The hardware guard the `hook` check exercises, and the Windows replacement the
# swap point targets (charter section 8 item 4). The Windows guard's matrix
# lives in tools/hw_guard_test; windows_guard_status() is where it gets wired.
GUARD_PATH_UNIX = ".claude/hooks/guard-hw-run.sh"
GUARD_PATH_WINDOWS = ".opencode/plugins/hw-guard/guard.mjs"

# Paths whose review needs judgment (semantics, prose claims, or the gate itself).
AGENT_PATTERNS = [
    r"^engine/", r"^tools/", r"^mesh/", r"^ui/", r"^scripts/",
    r"^tests/hardware/", r"CMakeLists\.txt$",
    r"^\.github/", r"^\.claude/",
    r"^docs/recon/", r"^docs/architecture/",
    # Contract-bearing docs under the otherwise script-tier docs/recomp/:
    # the Windows-fork charter and coordination file carry rules (the contract
    # pin, the two-writer rule) and deserve a verdict like docs/recon/**.
    r"^docs/recomp/(WINDOWS-FORK|FORK-COORDINATION)\.md$",
    # Every evidence artifact under tests/results/** — records, logs and addenda —
    # is agent-tier (#21): the directory is the loop's highest-stakes artifact
    # class, and an added record auto-merging unreviewed was the hole.
    r"^tests/results/",
]


def is_agent_tier(path):
    return any(re.search(p, path) for p in AGENT_PATTERNS)


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
    ("tests/results/2026-10-18-run.md", "agent"),
    ("tests/results/2026-10-18-run.log", "agent"),
    ("tests/results/2026-10-18-run.ips", "agent"),
    ("tests/results/2026-10-18-old-addendum.md", "agent"),
    (".github/workflows/verify.yml", "agent"),
    (".claude/hooks/guard-hw-run.sh", "agent"),
    ("CMakeLists.txt", "agent"),
    ("docs/recomp/PLAN.md", "script"),
    ("docs/recomp/ROADMAP.md", "script"),
    ("docs/recomp/log/2026-10-18-entry.md", "script"),
    ("CLAUDE.md", "script"),
    ("README.md", "script"),
    ("docs/research/plane-removal-reverse-engineering.md", "script"),
    ("docs/tooling/rea-ghidra-setup.md", "script"),
]


# The contract pin — FORK-COORDINATION.md section 2 records the parent commit and
# the sha256 of the header the fork consumes (a synced copy, single-sourced in
# the parent). This check asserts the fork's working-tree header still equals the
# recorded sha256.
#
# WHAT IT DOES NOT COVER: it reads the RECORDED pin and the local header only. It
# does not fetch the parent and cannot know whether a newer parent revision
# exists — the gate has no network access and should not have any. It catches a
# local drift between the recorded pin and the copy, not upstream movement.
PIN_DOC = "docs/recomp/FORK-COORDINATION.md"
PIN_HEADER = "engine/include/openscanner/obscan_api.h"
PIN_ROW = re.compile(
    r"^\|\s*`Open3DScanner-Win64`\s*\(fork\)\s*\|\s*`([0-9a-f]{40})`\s*\|\s*`([0-9a-f]{64})`\s*\|",
    re.M)


def check_contract_pin():
    """Return (ok, detail). Detail names BOTH values on a mismatch.

    'Loudly and specifically' matters: on failure a reader must be able to tell
    that the copy drifted, not that the check broke — so a parse/read problem
    reports 'check broken', and a real drift reports recorded vs present.
    """
    try:
        with open(PIN_DOC, encoding="utf-8") as fh:
            doc = fh.read()
    except OSError as e:
        return False, f"check broken: cannot read {PIN_DOC} ({e})"
    m = PIN_ROW.search(doc)
    if not m:
        return False, f"check broken: no fork pin row found in {PIN_DOC} section 2"
    parent_commit, recorded = m.group(1), m.group(2)
    try:
        with open(PIN_HEADER, "rb") as fh:
            raw = fh.read()
    except OSError as e:
        return False, f"check broken: cannot read {PIN_HEADER} ({e})"
    # The pin records a git blob sha256 (LF content). A Windows checkout has CRLF
    # in the working tree (core.autocrlf), so normalise CRLF -> LF before hashing;
    # on macOS/Linux the bytes are unchanged.
    present = hashlib.sha256(raw.replace(b"\r\n", b"\n")).hexdigest()
    if present != recorded:
        return False, (
            f"HEADER DRIFTED from the recorded pin: FORK-COORDINATION section 2 records sha256 "
            f"{recorded} (parent {parent_commit}), but {PIN_HEADER} has sha256 {present}. "
            f"The synced copy no longer matches the pin — re-sync the header and update the pin.")
    return True, f"recorded {recorded} == present {present}; parent {parent_commit}"


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
    # The contract pin is asserted here too, so `--selftest` covers it wherever
    # it runs (CI runs it on the base copy and the PR copy).
    pin_ok, pin_detail = check_contract_pin()
    for b in bad:
        print("SELFTEST FAIL:", b)
    if not pin_ok:
        print("SELFTEST FAIL:", pin_detail)
    print(f"tier selftest: {len(TIER_CASES) + 2} cases, {'FAIL' if bad else 'ok'}")
    print(f"contract-pin: {'ok' if pin_ok else 'FAIL'} - {pin_detail}")
    return 1 if (bad or not pin_ok) else 0


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def git(*args):
    return sh([GIT_BIN] + list(args))


def git_bytes(*args):
    return subprocess.run([GIT_BIN] + list(args), capture_output=True).stdout


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


def probe_matrix():
    cases = []
    for cwd_name, cwd in [("wt-hw", WT_HW), ("main", MAIN), ("wt-verify", WT_VERIFY), ("tmp", "/tmp")]:
        for cmd_name, cmd in [
            ("device-test", DEVICE_TEST),
            ("data-process-test", DATA_TEST),
            ("cd-hw+device", f"cd {WT_HW} && {DEVICE_TEST}"),
            ("dyld", "DYLD_INSERT_LIBRARIES=/x ./run"),
            ("recon-cache", "cat .recon-cache/listings/x.asm"),
            ("header", "cat engine/include/openscanner/obscan_api.h"),
            ("vendor-sdk", "strings libOrbbecSDK.1.7.1.dylib"),
            ("vendor-scan", "cat liborbbec_scan.3.5.3.dylib"),
            ("ls", "ls -la"),
        ]:
            cases.append((f"{cwd_name}/{cmd_name}", cwd, cmd))
    return cases


def run_hook(script_bytes, cwd, cmd):
    with tempfile.TemporaryDirectory() as td:
        sp = os.path.join(td, "hook.sh")
        with open(sp, "wb") as f:
            f.write(script_bytes)
        os.chmod(sp, 0o755)
        payload = json.dumps({"cwd": cwd, "tool_name": "Bash", "tool_input": {"command": cmd}})
        r = subprocess.run(["bash", sp], input=payload, capture_output=True, text=True)
        return r.returncode


def run_probes(script_bytes, matrix):
    return {label: ("allow" if run_hook(script_bytes, cwd, cmd) == 0 else "block")
            for label, cwd, cmd in matrix}


HOOK_MUST_BLOCK = [
    ("dyld/tmp", "/tmp", "DYLD_INSERT_LIBRARIES=/x ./run"),
    ("vendor-sdk/tmp", "/tmp", "strings libOrbbecSDK.1.7.1.dylib"),
    ("vendor-scan/tmp", "/tmp", "cat liborbbec_scan.3.5.3.dylib"),
    ("recon-cache/tmp", "/tmp", "cat .recon-cache/listings/x.asm"),
    ("header/tmp", "/tmp", "cat engine/include/openscanner/obscan_api.h"),
    ("device/main", MAIN, DEVICE_TEST),
    ("device/wt-verify", WT_VERIFY, DEVICE_TEST),
    ("device/tmp", "/tmp", DEVICE_TEST),
    ("data/main", MAIN, DATA_TEST),
    ("data/wt-verify", WT_VERIFY, DATA_TEST),
    ("data/tmp", "/tmp", DATA_TEST),
]
HOOK_MUST_ALLOW = [
    ("device/wt-hw", WT_HW, DEVICE_TEST),
    ("data/wt-hw", WT_HW, DATA_TEST),
    ("device/cd-wt-hw", "/tmp", f"cd {WT_HW} && {DEVICE_TEST}"),
    ("ls/tmp", "/tmp", "ls -la"),
]


def check_hook_invariants(script_bytes):
    bad = []
    for label, cwd, cmd in HOOK_MUST_BLOCK:
        if run_hook(script_bytes, cwd, cmd) == 0:
            bad.append(f"must-block case allowed: {label}")
    for label, cwd, cmd in HOOK_MUST_ALLOW:
        if run_hook(script_bytes, cwd, cmd) != 0:
            bad.append(f"must-allow case blocked: {label}")
    return bad


def windows_guard_status():
    """Report the Windows hardware guard (charter section 8 item 4).

    Returns (result, detail). Until the OpenCode replacement is in this tree the
    check is honestly N/A — never green — and the detail names the reason. When
    the replacement lands, this function must run ITS invariants (the matrix in
    tools/hw_guard_test/run.mjs) instead of returning N/A; that swap is the whole
    point of the seam, so it stays a small change here.
    """
    if os.path.exists(GUARD_PATH_WINDOWS):
        return ("N/A",
                f"the Windows guard ({GUARD_PATH_WINDOWS}) is present but its invariants are not wired "
                f"into this check yet — run tools/hw_guard_test/run.mjs, then wire it here")
    return ("N/A",
            f"no hardware guard to exercise: {GUARD_PATH_UNIX} is bash/POSIX and the OpenCode replacement "
            f"({GUARD_PATH_WINDOWS}) is not in this tree (charter section 8 item 4)")


def status_of(line):
    cells = line.split("|")
    return cells[3].strip() if len(cells) > 4 else "?"


def main():
    argv = sys.argv[1:]
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

    # 1. whitespace — verbatim run captures are exempt. A raw log is the device's
    #    own output (trailing spaces and all) and must never be edited to satisfy a
    #    linter; the records and everything authored stay checked.
    r = git("diff", "--check", f"{base_ref}...{head_ref}", "--", ".",
            ":(exclude)tests/results/*.log", ":(exclude)tests/results/*.ips")
    add("whitespace", not (r.stdout.strip() or r.stderr.strip()),
        (r.stdout + r.stderr).strip()[:400] or "clean")

    # 2. run records never edited, deleted or renamed in place
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    bad = []
    addenda = 0
    for line in r.stdout.splitlines():
        st, path = line.split("\t")[0], line.split("\t")[-1]
        bn = os.path.basename(path)
        is_addendum = (bn.endswith("-correction.md") or bn.endswith("-analysis.md")
                       or bn.endswith("-addendum.md"))
        if st.startswith("M"):
            if is_addendum:
                addenda += 1
            else:
                bad.append(f"modified in place: {path}")
        elif st.startswith(("D", "R")):
            bad.append(f"deleted/renamed: {path}")
    add("run-records", not bad,
        "; ".join(bad) if bad else
        (f"{addenda} modified addenda allowed" if addenda else "no modifications"))

    # 2b. added records need a companion raw artifact in the same PR (#21)
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    added_res = [l.split("\t")[-1] for l in r.stdout.splitlines() if l.startswith("A")]
    records = [p for p in added_res if p.endswith(".md")
               and not re.search(r"-(correction|analysis|addendum)\.md$", p)]
    others = [os.path.basename(p) for p in added_res
              if p not in records and not p.endswith(".md")]
    missing = [p for p in records
               if not any(o.startswith(os.path.basename(p)[:-3]) for o in others)]
    add("record-provenance", not missing,
        "; ".join(f"no companion raw artifact in this PR: {p}" for p in missing) if missing
        else (f"{len(records)} record(s) with companion(s)" if records
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
    # The cited-sha256 half below ALWAYS runs. The added-*.ips comparison reads
    # ~/Library/Logs/DiagnosticReports, which only exists on macOS; when it is
    # absent (Windows, CI) only that half is N/A, and the reason is printed.
    diag_dir = os.path.expanduser("~/Library/Logs/DiagnosticReports")
    diag_available = os.path.isdir(diag_dir)
    if diag_available:
        for path in added:
            if not path.endswith(".ips"):
                continue
            bn = os.path.basename(path)
            src = os.path.join(diag_dir, bn)
            if os.path.exists(src):
                arch_local += 1
                if sha256_bytes(open(src, "rb").read()) != sha256_bytes(git_bytes("show", f"{head_ref}:{path}")):
                    arch_problems.append(f"{bn}: sha256 mismatch vs {src}")
    for path in md_touched:
        text = git("show", f"{head_ref}:{path}").stdout
        for m in re.finditer(r"sha256\s*`([0-9a-f]{64})`", text):
            cited = m.group(1)
            ctx = text[max(0, m.start() - 300):m.start()]
            names = [a or b for a, b in re.findall(r"\(\./([^)]+)\)|`([^`]+\.(?:ips|md|log|txt))`", ctx)]
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
        f"{arch_local} .ips compared against DiagnosticReports (local)" if arch_local else "",
        "" if diag_available else f"DiagnosticReports comparison N/A: {diag_dir} not present (macOS-only)",
    ])) or "no archival claims"
    add("archives", not arch_problems,
        "; ".join(arch_problems) if arch_problems else arch_detail)

    # 5. hook executable + invariants + matrix diff.
    #    macOS: exercise the bash PreToolUse hook. Windows: that hook is
    #    bash/POSIX; report N/A with its reason (never green). The replacement's
    #    own invariants are wired in through windows_guard_status().
    if os.name == "nt":
        result, detail = windows_guard_status()
        report.append(("hook", result, detail))
    else:
        hook = GUARD_PATH_UNIX
        if hook in [l.split("\t")[-1] for l in diff_files.splitlines()]:
            mode = git("ls-tree", head_ref, "--", hook).stdout.split()[0]
            head_hook = git_bytes("show", f"{head_ref}:{hook}")
            base_hook = git_bytes("show", f"{base_ref}:{hook}")
            problems = [] if mode == "100755" else [f"mode {mode} (want 100755)"]
            problems += check_hook_invariants(head_hook)
            matrix = probe_matrix()
            rb, rh = run_probes(base_hook, matrix), run_probes(head_hook, matrix)
            diffs = [f"{label}: {rb[label]} -> {rh[label]}" for label in rb if rb[label] != rh[label]]
            add("hook", not problems, "; ".join(problems) if problems else
                "executable (100755); guard invariants hold")
            report.append(("hook-probes", "INFO",
                           f"{len(matrix)} cases; diffs: " + ("; ".join(diffs) if diffs else "none")))
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
        rc = sh(["cmake", "-S", ".", "-B", "build"]).returncode
        if rc == 0:
            rc = sh(["cmake", "--build", "build", "-j"]).returncode
        if rc != 0:
            add("build", False, "cmake configure/build failed (see CI log)")
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
    checks = ["whitespace", "run-records", "record-provenance", "contract-pin", "plan", "archives",
              "ui-js-syntax", "build"]
    if os.name != "nt":
        checks.insert(6, "hook")
    print("Gating checks: " + ", ".join(checks) + ".")
    if os.name == "nt":
        print(f"hook: N/A here — {windows_guard_status()[1]}")
    if fails:
        print()
        print(f"**FAILED: {', '.join(fails)}**")
        return 1
    print()
    print("All gating checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
