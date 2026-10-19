#!/usr/bin/env pwsh
# hw-preflight.ps1 - mechanical pre-run gate for the hw-test agent (Windows).
#
# Provenance: port of the parent's scripts/hw-preflight.sh (Open3DScanner,
# parent/main: scripts/hw-preflight.sh). Same checks, Windows mechanisms:
#   * worktree/branch confirmation via git;
#   * the one-SDK-user check uses Get-Process -Name, which is name-anchored
#     (there is no pgrep -x on Windows);
#   * binary selection: explicit path, then build/tools/<target>/<target>.exe,
#     then the first build/**/<target>.exe (there is no .app bundle on Windows);
#   * freshness compares the binary against the NEWEST source in the target's
#     directory (*.cpp, *.h, *.c, *.mm) - a header change counts.
#
# The INTERPOSER expectation has no Windows equivalent (no DYLD_INSERT_LIBRARIES
# equivalent for this launcher) and is reported N/A with that reason - not
# invented, and not faked.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/hw-preflight.ps1 <target> [expected-worktree] [binary-path]
#
# Exit 0 -> all checks pass; a pre-filled record header block is printed.
# Exit 1 -> a check failed; the reason is on stderr. Stop and report it.
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Target,
    [Parameter(Position = 1)][string]$ExpectedWt = $env:HW_EXPECTED_WT,
    [Parameter(Position = 2)][string]$ExplicitBin,
    [Alias('h')][switch]$Help
)
$ErrorActionPreference = 'Stop'

function Die([string]$Why) {
    [Console]::Error.WriteLine("hw-preflight: FAIL - $Why")
    exit 1
}

if ($Help) {
    Write-Output 'usage: hw-preflight.ps1 <target> [expected-worktree] [binary-path]'
    Write-Output '  <target> is the executable name (scan_server, raptor_client, ...).'
    exit 0
}
if ([string]::IsNullOrWhiteSpace($Target)) {
    Die 'usage: hw-preflight.ps1 <target> [expected-worktree] [binary-path]'
}

# git is resolved explicitly: it is not on this lane's PATH by default.
$gitBin = if ($env:GIT_BIN) { $env:GIT_BIN } else {
    'C:\Users\Bruce\AppData\Local\GitHubDesktop\app-3.6.7\resources\app\git\cmd\git.exe'
}
if (-not (Test-Path -LiteralPath $gitBin)) { $gitBin = 'git' }
function Invoke-Git([string[]]$GitArgs) {
    try { return ((& $script:gitBin @GitArgs 2>$null) -join "`n") } catch { return '' }
}

# 1. Worktree + branch. The hw worktree is the unique *-wt-hw worktree unless the
#    caller names one; no hard-coded user path.
if ([string]::IsNullOrWhiteSpace($ExpectedWt)) {
    foreach ($line in ((Invoke-Git @('worktree', 'list', '--porcelain')) -split "`n")) {
        if ($line -match '^worktree\s+(.+)$') {
            $candidate = $Matches[1].Trim()
            if ($candidate -match '-wt-hw[\\/]?$') { $ExpectedWt = $candidate; break }
        }
    }
}
if ([string]::IsNullOrWhiteSpace($ExpectedWt)) {
    Die 'cannot find an hw worktree (*-wt-hw); pass it as the second argument or set HW_EXPECTED_WT.'
}
if (-not (Test-Path -LiteralPath $ExpectedWt)) { Die "expected worktree '$ExpectedWt' does not exist." }
$wtReal = [IO.Path]::GetFullPath($ExpectedWt).TrimEnd('\', '/')
$pwdReal = [IO.Path]::GetFullPath((Get-Location).Path).TrimEnd('\', '/')
if ($pwdReal -ine $wtReal) { Die "pwd is '$pwdReal', expected the hw worktree '$wtReal'. cd there first." }
$branch = (Invoke-Git @('branch', '--show-current')).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) { $branch = '?' }

# 2. Locate the binary actually under test - fixed order, never find|head.
$buildCopy = Join-Path 'build' (Join-Path (Join-Path 'tools' $Target) "$Target.exe")
if (-not [string]::IsNullOrWhiteSpace($ExplicitBin)) {
    $bin = $ExplicitBin
    $why = 'explicit argument (the binary this run will execute)'
} elseif (Test-Path -LiteralPath $buildCopy) {
    $bin = $buildCopy
    $why = "build-tree copy (fixed order: build/tools/$Target/$Target.exe)"
} else {
    $found = Get-ChildItem -Path 'build' -Recurse -File -Filter "$Target.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName | Select-Object -First 1
    if ($null -eq $found) { Die "no built binary named '$Target.exe' under build/ - build it first." }
    $bin = $found.FullName
    $why = "build-tree copy (no build/tools/$Target/$Target.exe exists; first under build/)"
}
if (-not (Test-Path -LiteralPath $bin)) { Die "binary '$bin' does not exist." }

$alt = ''
if ((Test-Path -LiteralPath $buildCopy) -and
    ([IO.Path]::GetFullPath($bin) -ine [IO.Path]::GetFullPath($buildCopy))) {
    $alt = "other copy: $buildCopy ($((Get-Item -LiteralPath $buildCopy).LastWriteTime.ToString('HH:mm:ss')))"
}

# 3. Freshness against the NEWEST source in the target's directory.
$srcDir = $null
foreach ($root in @('tools', 'tests', 'engine')) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    $d = Get-ChildItem -Path $root -Recurse -Directory -Filter $Target -ErrorAction SilentlyContinue |
        Sort-Object FullName | Select-Object -First 1
    if ($null -ne $d) { $srcDir = $d.FullName; break }
}
$newestSrc = $null
$newestMt = [datetime]::MinValue
if ($null -ne $srcDir) {
    foreach ($f in Get-ChildItem -Path $srcDir -File -ErrorAction SilentlyContinue) {
        if ($f.Extension -notin @('.cpp', '.h', '.c', '.mm')) { continue }
        if ($f.LastWriteTimeUtc -gt $newestMt) { $newestMt = $f.LastWriteTimeUtc; $newestSrc = $f }
    }
}
if ($null -ne $newestSrc) {
    $binMt = (Get-Item -LiteralPath $bin).LastWriteTimeUtc
    if ($newestMt -gt $binMt) {
        Die "newest source '$($newestSrc.FullName)' is NEWER than binary '$bin' - rebuild (a header change counts)."
    }
    $fresh = "binary $($binMt.ToString('HH:mm:ss')) >= newest source $($newestSrc.Name) $($newestMt.ToString('HH:mm:ss'))"
} else {
    $fresh = 'source dir not auto-found; verify freshness by hand if in doubt'
}

# 4. Name-anchored conflicting-process check. Get-Process -Name matches the image
#    name exactly (case-insensitive); it never matches a substring of a path.
$conflict = @()
foreach ($name in @('scan_server', 'raptor_client', 'device_message_listener_test',
                    'data_process_message_listener_test', 'live_capture_test',
                    'capture_pipeline_smoke_test')) {
    $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    if ($procs.Count -gt 0) {
        $conflict += ("{0}({1})" -f $name, (($procs | ForEach-Object { $_.Id }) -join ','))
    }
}
if ($conflict.Count -gt 0) {
    Die ('a conflicting SDK client is already running: ' + ($conflict -join ' ') + ' - one run at a time.')
}

# 5. Interposer N/A is not blank: assert what we CAN check on Windows - the
#    loader's vendor DLL path exists, the DLL actually loads with its adjacent
#    dependencies, and one bootstrap export resolves. A present-but-unloadable
#    SDK is a FAIL, not a test SKIP.
if (-not ('ScannerNative' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class ScannerNative {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    public static extern IntPtr LoadLibraryExA(string lpLibFileName, IntPtr hFile, uint dwFlags);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    public static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool FreeLibrary(IntPtr hModule);

    public const uint LOAD_WITH_ALTERED_SEARCH_PATH = 0x00000008;
}
'@
}
$dllCandidates = @(
    'C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll',
    'C:\Program Files (x86)\CrealityScan\CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll'
)
$dllPath = $dllCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($Target -eq 'raptor_client') {
    $interp = 'N/A for interposer (macOS-only); links no vendor SDK (socket/STL only) - no DLL-path check applies'
} elseif ($dllPath) {
    $handle = [ScannerNative]::LoadLibraryExA($dllPath, [IntPtr]::Zero,
                                              [ScannerNative]::LOAD_WITH_ALTERED_SEARCH_PATH)
    if ($handle -eq [IntPtr]::Zero) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Die "vendor SDK present but not loadable on Windows: LoadLibraryExA('$dllPath') failed (GetLastError=$code). Do not start the run."
    }
    $symbol = [IntPtr]::Zero
    $symbolError = 0
    try {
        $symbol = [ScannerNative]::GetProcAddress($handle, 'obscan_context_create')
        $symbolError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    } finally {
        [void][ScannerNative]::FreeLibrary($handle)
    }
    if ($symbol -eq [IntPtr]::Zero) {
        Die "vendor SDK loaded but obscan_context_create did not resolve: GetProcAddress('$dllPath', 'obscan_context_create') failed (GetLastError=$symbolError). Do not start the run."
    }
    $interp = "N/A for interposer (macOS-only); DLL path verified; load asserted (obscan_context_create resolved); path: $dllPath"
} else {
    $interp = 'N/A for interposer (macOS-only); DLL path NOT found at ' + ($dllCandidates -join ' OR ') +
        ' - the vendor SDK is absent; no load assertion was possible'
}

# --- all checks passed: emit the pre-filled record header ---
Write-Output 'hw-preflight: OK'
Write-Output ''
Write-Output '----8<---- paste into the record ----8<----'
Write-Output ("- Date: " + (Get-Date).ToString('yyyy-MM-dd'))
Write-Output ("- Worktree: " + $wtReal)
Write-Output ("- Branch: " + $branch)
Write-Output ("- Target: " + $bin + "  (" + $why + ")")
Write-Output ("- Other copy: " + $(if ($alt) { $alt } else { 'none' }))
Write-Output ("- Preflight: pwd OK; no conflicting process; freshness: " + $fresh)
Write-Output ("- Interposer: " + $interp)
Write-Output ("- Run start: " + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
Write-Output '----8<----------------------------------8<----'
exit 0
