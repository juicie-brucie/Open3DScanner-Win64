#!/usr/bin/env pwsh
# PreToolUse guard for the Claude Code shell tool on the Windows fork.
#
# Provenance: port of the parent's .claude/hooks/guard-hw-run.sh
# (Open3DScanner, parent/main: .claude/hooks/guard-hw-run.sh). Same invariants;
# Windows mechanisms. The bash original is not kept: two implementations drift,
# and this lane is Windows-only.
#
# Claude Code passes the tool call as JSON on stdin, including the working
# directory:
#   {"session_id":"...","cwd":"C:\\path","tool_name":"Bash",
#    "tool_input":{"command":"..."}}
#
# Exit 0 -> allow the tool call.
# Exit 2 -> block it; stderr is shown to the agent as the reason.
$ErrorActionPreference = 'Stop'

$raw = [Console]::In.ReadToEnd()
$payload = $null
try { $payload = $raw | ConvertFrom-Json } catch { $payload = $null }
$cmd = if ($payload -and $payload.tool_input) { [string]$payload.tool_input.command } else { '' }
$cwd = if ($payload -and $payload.cwd) { [string]$payload.cwd } else { '' }

function Block([string]$Why) {
    [Console]::Error.WriteLine("BLOCKED by hw-test guard: $Why")
    exit 2
}

# 1. Manual library-path injection. The launcher owns module resolution; setting
#    it by hand doubles the insertion. DYLD_INSERT_LIBRARIES/LD_PRELOAD are the
#    macOS/Linux names; the Windows analogues are an explicit DLL directory or
#    hand-steered loader.
foreach ($tok in 'DYLD_INSERT_LIBRARIES', 'LD_PRELOAD', 'SetDllDirectory', 'AddDllDirectory',
                 'SetDefaultDllDirectories') {
    if ($cmd -like "*$tok*") {
        Block "never set $tok; the launcher owns module resolution"
    }
}

# 2. Clean-room wall: no vendor artefacts, no decompiled output, no recon cache.
foreach ($tok in '.recon-cache', 'libOrbbecSDK', 'liborbbec_scan', 'lib_orbbec_scan') {
    if ($cmd -like "*$tok*") {
        Block 'the hardware agent must not read vendor artefacts'
    }
}

# 3. The ABI header is recon-only.
if ($cmd -like '*obscan_api.h*') { Block 'obscan_api.h is recon-only' }

# 4. The hardware test must run against the hardware worktree. Accept either the
#    session's cwd or an explicit cd to that worktree in the command - a bare
#    relative path is only safe when the cwd is the worktree.
if ($cmd -like '*message_listener_test*') {
    $cwdInRoom = $cwd -match '-wt-hw[\\/]?$'
    $cmdNamesRoom = $cmd -match '-wt-hw'
    if (-not $cwdInRoom -and -not $cmdNamesRoom) {
        Block 'run the hardware test only from the *-wt-hw worktree (the hardware build)'
    }
}

exit 0
