#!/usr/bin/env python3
"""Property tests for verify-pr.py — the "never a false success" gate.

The verdict-gate fails CLOSED: any rejection path prints "ignoring" and exits 0
WITHOUT posting a status, so the required `verdict` context never appears and the
merge blocks. A false SUCCESS is the only outcome that must never happen. Two
places can produce one:

  1. the verdict-comment parser in .github/workflows/verdict-gate.yml;
  2. --tier in scripts/verify-pr.py — if an agent-tier path is classified
     "script", verdict-gate auto-posts success and the PR merges with no verdict.

This file asserts both. It reads the workflow text only as a drift guard (the
authoritative parser is the workflow); the decision logic below is a faithful
transcription of that workflow's rules.

Usage:
    python scripts/test_verify_pr.py      # exit 0 = all properties hold
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

results = []


def record(name, ok, detail=""):
    results.append((name, ok, detail))


def load_verify_pr():
    path = os.path.join(HERE, "verify-pr.py")
    spec = importlib.util.spec_from_file_location("verify_pr", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)  # module-level resolution runs; main() does not
    return module


# ---------------------------------------------------------------------------
# 1. verdict parser — must never yield a false success
# ---------------------------------------------------------------------------
# Transcribed from .github/workflows/verdict-gate.yml:
#   count  = grep -o -E 'VERDICT:[[:space:]]*(MERGEABLE|NEEDS CHANGE)[[:space:]]*@[[:space:]]*[0-9a-f]{40}'
#            over the comment with '*' markers stripped
#   line   = grep -m1 -E '^VERDICT: (MERGEABLE|NEEDS CHANGE) @ [0-9a-f]{40}[[:space:]]*$'
#            over the ORIGINAL body
#   stale  = the anchored line's sha != the current head -> ignoring
# Any rejection yields NO status (the required context stays missing).
VERDICT_TOKEN = re.compile(r"VERDICT:[ \t]*(MERGEABLE|NEEDS CHANGE)[ \t]*@[ \t]*[0-9a-f]{40}")
VERDICT_ANCHORED = re.compile(r"^VERDICT: (MERGEABLE|NEEDS CHANGE) @ ([0-9a-f]{40})[ \t]*$", re.M)

HEAD = "a" * 40
STALE = "b" * 40


def verdict_decision(body, head):
    """Return 'success' | 'failure' | 'no-status' (the last = fail closed)."""
    count = len(VERDICT_TOKEN.findall(body.replace("*", "")))
    if count != 1:
        return "no-status"
    m = VERDICT_ANCHORED.search(body)  # original body, not star-stripped
    if not m:
        return "no-status"
    verdict, sha = m.group(1), m.group(2)
    if sha != head:
        return "no-status"
    return "success" if verdict == "MERGEABLE" else "failure"


def test_verdict_parser():
    cases = [
        ("single anchored verdict naming the current head -> success",
         f"VERDICT: MERGEABLE @ {HEAD}", HEAD, "success"),
        ("single anchored NEEDS CHANGE -> failure (not a success)",
         f"reasoning\nVERDICT: NEEDS CHANGE @ {HEAD}\n", HEAD, "failure"),
        ("zero verdict tokens -> no status (blocks)",
         "just a normal review comment", HEAD, "no-status"),
        ("two verdict tokens in one comment -> no status (blocks)",
         f"VERDICT: MERGEABLE @ {HEAD}\nVERDICT: MERGEABLE @ {HEAD}\n", HEAD, "no-status"),
        ("unanchored / quoted verdict -> no status (blocks)",
         f'as discussed, "VERDICT: MERGEABLE @ {HEAD}" is the line', HEAD, "no-status"),
        ("bold-wrapped verdict (counts once, but not an anchored line) -> no status (blocks)",
         f"**VERDICT: MERGEABLE @ {HEAD}**", HEAD, "no-status"),
        ("anchored verdict naming a stale head -> no status (blocks)",
         f"VERDICT: MERGEABLE @ {STALE}", HEAD, "no-status"),
        ("recanted verdict plus a fresh one -> two tokens -> no status (blocks)",
         f"VERDICT: MERGEABLE @ {STALE}\nVERDICT: MERGEABLE @ {HEAD}", HEAD, "no-status"),
    ]
    for name, body, head, expect in cases:
        got = verdict_decision(body, head)
        record(f"verdict: {name}", got == expect, f"expected {expect}, got {got}")

    # Only the one explicit success case may ever return 'success'.
    successes = [n for n, b, h, e in [(c[0], c[1], c[2], c[3]) for c in cases]
                 if verdict_decision(b, h) == "success"]
    record("verdict: exactly one case yields success", successes == [cases[0][0]],
           f"successes={successes}")


def test_verdict_workflow_drift_guard():
    """The transcription above must still match the real workflow text."""
    wf_path = os.path.join(REPO_ROOT, ".github", "workflows", "verdict-gate.yml")
    if not os.path.isfile(wf_path):
        record("verdict: workflow present for drift guard", False, wf_path)
        return
    text = open(wf_path, encoding="utf-8").read()
    fragments = [
        "VERDICT:[[:space:]]*(MERGEABLE|NEEDS CHANGE)[[:space:]]*@[[:space:]]*[0-9a-f]{40}",
        "^VERDICT: (MERGEABLE|NEEDS CHANGE) @ [0-9a-f]{40}[[:space:]]*$",
        '"$n" -ne 1',
        '"$sha" != "$head"',
    ]
    missing = [f for f in fragments if f not in text]
    record("verdict: workflow still carries the fail-closed rules",
           not missing, "missing: " + "; ".join(missing) if missing else "all present")


# ---------------------------------------------------------------------------
# 2. tier computation — the false-success vector that lives in the port
# ---------------------------------------------------------------------------
# Every path the parent's current AGENT_PATTERNS calls agent must NOT come back
# as "script" (which would let verdict-gate auto-post success).
def test_tier(vp):
    agent_paths = [
        "engine/src/obscan_loader.cpp",
        "tools/scan_server/main.cpp",
        "mesh/src/mesh_tools.cpp",
        "ui/app.js",
        "scripts/verify-pr.py",
        "tests/hardware/device_message_listener_test/main.cpp",
        "CMakeLists.txt",
        "engine/CMakeLists.txt",
        ".github/workflows/verify.yml",
        ".claude/hooks/guard-hw-run.sh",
        "docs/recon/findings/scan-live-data.md",
        "docs/recomp/WINDOWS-FORK.md",
        "docs/recomp/FORK-COORDINATION.md",
        # tests/results/** is agent tier in the parent's CURRENT list (#21):
        "tests/results/2026-10-18-run.md",
        "tests/results/2026-10-18-run.log",
        "tests/results/2026-10-18-run.crash.ips",
        "tests/results/2026-10-18-run-correction.md",
        "tests/results/2026-10-18-run-analysis.md",
    ]
    script_paths = [
        "docs/recomp/PLAN.md",
        "docs/recomp/ROADMAP.md",
        "README.md",
        "CLAUDE.md",
        "LICENSE",
    ]

    for p in agent_paths:
        got = vp.tier_of([p])
        record(f"tier: agent {p}", got == "agent", f"got {got}")

    for p in script_paths:
        got = vp.tier_of([p])
        record(f"tier: script {p}", got == "script", f"got {got}")

    # Any agent-tier path in a mixed list forces agent.
    record("tier: mixed list with an agent path -> agent",
           vp.tier_of(["README.md", "tests/results/x.md"]) == "agent",
           vp.tier_of(["README.md", "tests/results/x.md"]))
    # Unknown / empty -> agent (fail closed).
    record("tier: empty list -> agent (fail closed)", vp.tier_of([]) == "agent", vp.tier_of([]))
    record("tier: unreadable (None) -> agent (fail closed)",
           vp.tier_of(None) == "agent", vp.tier_of(None))

    # Explicit regression guard: the stale-copy bug — tests/results/** was
    # script tier before the parent's #21 change, which auto-merged unreviewed
    # records. It must be agent tier.
    record("tier: AGENT_PATTERNS pins tests/results/** as agent",
           r"^tests/results/" in vp.AGENT_PATTERNS,
           "pattern missing" if r"^tests/results/" not in vp.AGENT_PATTERNS else "present")
    record("tier: a tests/results/** record is not script",
           vp.tier_of(["tests/results/2026-10-18-anything.md"]) != "script",
           vp.tier_of(["tests/results/2026-10-18-anything.md"]))


def main():
    test_verdict_parser()
    test_verdict_workflow_drift_guard()
    vp = load_verify_pr()
    test_tier(vp)

    width = max(len(n) for n, _, _ in results)
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name.ljust(width)}  {detail}")
    failed = [n for n, ok, _ in results if not ok]
    print()
    if failed:
        print(f"**FAILED: {len(failed)} of {len(results)}**")
        for n in failed:
            print(f"  - {n}")
        return 1
    print(f"All {len(results)} properties hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
