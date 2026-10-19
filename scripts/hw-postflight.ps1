#!/usr/bin/env pwsh
# hw-postflight.ps1 - deterministic crash-report sweep for the hw-test agent (Windows).
#
# Provenance: port of the parent's scripts/hw-postflight.sh (Open3DScanner,
# parent/main: scripts/hw-postflight.sh). Same contract: match a report on ITS OWN
# FIELDS (app name/path + report time), never on a filename; choose the earliest;
# archive it and print a reference line with sha256.
#
# PLATFORM DECISION (macOS reads .ips; Windows has none - stated, not faked):
# this sweep reads TWO Windows sources:
#   (1) Windows Error Reporting: C:\ProgramData\Microsoft\Windows\WER\ReportArchive
#       - each report directory's Report.wer, matched on its AppName / AppPath
#         fields, with the report's own EventTime (falling back to the report
#         directory's time). The .wer is archived, and the .hdmp if present.
#   (2) the Application event log, EventID 1000 ("Application Error"), matched on
#       the event's faulting-application field and its event time.
# It never prints a line implying the macOS DiagnosticReports sweep ran.
#
# If NEITHER source yields a report, the script prints EXACTLY:
#   no Windows crash-report source available - sweep not performed
# and the record must quote that sentence. It must never say "no crash".
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/hw-postflight.ps1 <record-slug> 'YYYY-MM-DD HH:MM:SS' <target>
#
# Exit 0 always (a missing report is a valid outcome); exit 2 on bad usage.
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Slug,
    [Parameter(Position = 1)][string]$Start,
    [Parameter(Position = 2)][string]$Target,
    [Alias('h')][switch]$Help
)
$ErrorActionPreference = 'Stop'

function Usage {
    [Console]::Error.WriteLine("usage: hw-postflight.ps1 <record-slug> 'YYYY-MM-DD HH:MM:SS' <target>")
    exit 2
}
if ($Help) {
    Write-Output "usage: hw-postflight.ps1 <record-slug> 'YYYY-MM-DD HH:MM:SS' <target>"
    Write-Output '  Windows sweep: WER ReportArchive + the Application event log (no .ips on this platform).'
    exit 0
}
if ([string]::IsNullOrWhiteSpace($Slug) -or [string]::IsNullOrWhiteSpace($Start) -or
    [string]::IsNullOrWhiteSpace($Target)) { Usage }
if (-not (Test-Path -LiteralPath 'tests/results')) {
    [Console]::Error.WriteLine('hw-postflight: no tests/results/ here - run from the worktree root.')
    exit 2
}
$startTime = $null
try {
    $startTime = [datetime]::ParseExact($Start, 'yyyy-MM-dd HH:mm:ss', [Globalization.CultureInfo]::InvariantCulture)
} catch { $startTime = $null }
if ($null -eq $startTime) {
    [Console]::Error.WriteLine("hw-postflight: cannot parse run start '$Start' (want 'YYYY-MM-DD HH:MM:SS').")
    exit 2
}

# Reports can take a few seconds to land; give them a moment.
Start-Sleep -Seconds 3

$targetName = $Target
$targetExe = if ($Target -match '\.exe$') { $Target } else { "$Target.exe" }
$reports = @()

function Test-TargetApp([string]$AppName, [string]$AppPath) {
    if ($AppName -and ($AppName -ieq $targetName -or $AppName -ieq $targetExe)) { return $true }
    if ($AppPath) {
        $leaf = Split-Path -Leaf $AppPath
        if ($leaf -ieq $targetName -or $leaf -ieq $targetExe) { return $true }
    }
    return $false
}

function Convert-WerTime([string]$Value) {
    # WER EventTime is often a FILETIME (100 ns since 1601); accept a date string too.
    if ([string]::IsNullOrWhiteSpace($Value)) { return $null }
    $n = [long]0
    if ([long]::TryParse($Value.Trim(), [ref]$n) -and $n -gt 116444736000000000) {
        try { return [datetime]::FromFileTimeUtc($n).ToLocalTime() } catch { return $null }
    }
    $d = [datetime]::MinValue
    if ([datetime]::TryParse($Value, [ref]$d)) { return $d }
    return $null
}

# (1) Windows Error Reporting - match the report's OWN fields. ReportArchive is
#     admin-protected on some machines; an unreadable source is best-effort, not a
#     crash of the sweep.
$werRoot = 'C:\ProgramData\Microsoft\Windows\WER\ReportArchive'
$werDirs = @()
try {
    if (Test-Path -LiteralPath $werRoot) {
        $werDirs = @(Get-ChildItem -LiteralPath $werRoot -Directory -ErrorAction Stop)
    }
} catch { $werDirs = @() }
foreach ($dir in $werDirs) {
    $wer = Join-Path $dir.FullName 'Report.wer'
    try { $werExists = Test-Path -LiteralPath $wer } catch { $werExists = $false }
    if (-not $werExists) { continue }
    try { $raw = Get-Content -LiteralPath $wer -Raw -ErrorAction Stop } catch { continue }
    $fields = @{}
    foreach ($line in ($raw -split "`r?`n")) {
        if ($line -match '^([A-Za-z][A-Za-z0-9_]*)=(.*)$') { $fields[$Matches[1]] = $Matches[2].Trim() }
    }
    if (-not (Test-TargetApp $fields['AppName'] $fields['AppPath'])) { continue }
    $t = Convert-WerTime $fields['EventTime']
    if ($null -eq $t) { $t = $dir.LastWriteTime }
    if ($t -lt $startTime) { continue }
    $hdmpPath = $null
    try {
        $hdmp = Get-ChildItem -LiteralPath $dir.FullName -Filter '*.hdmp' -File -ErrorAction Stop |
            Select-Object -First 1
        if ($hdmp) { $hdmpPath = $hdmp.FullName }
    } catch { }
    $reports += [pscustomobject]@{
        Time = $t; Kind = 'wer'; Wer = $wer; Hdmp = $hdmpPath; Event = $null
    }
}

# (2) Application event log, EventID 1000 - faulting app is property 0.
try {
    $events = Get-WinEvent -FilterHashtable @{
        LogName = 'Application'; Id = 1000; StartTime = $startTime
    } -ErrorAction Stop
    foreach ($ev in $events) {
        $appName = $null
        try { $appName = [string]$ev.Properties[0].Value } catch { }
        if (-not (Test-TargetApp $appName $null)) { continue }
        $reports += [pscustomobject]@{
            Time = $ev.TimeCreated; Kind = 'event'; Wer = $null; Hdmp = $null; Event = $ev; EventId = $ev.Id
        }
    }
} catch { }

if ($reports.Count -eq 0) {
    # The exact sentence the parent's team agreed on. Never "no crash".
    Write-Output 'no Windows crash-report source available - sweep not performed'
    exit 0
}

$ordered = @($reports | Sort-Object Time, Wer)
$first = $ordered[0]
if ($first.Kind -eq 'wer') {
    $dest = Join-Path 'tests/results' ($Slug + '.wer')
    Copy-Item -LiteralPath $first.Wer -Destination $dest -Force
    if ($first.Hdmp) {
        Copy-Item -LiteralPath $first.Hdmp -Destination (Join-Path 'tests/results' ($Slug + '.hdmp')) -Force
    }
} else {
    $dest = Join-Path 'tests/results' ($Slug + '.event.log')
    (($first.Event | Format-List * | Out-String)) | Set-Content -LiteralPath $dest -Encoding UTF8
}
$sha = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLower()
$source = if ($first.Wer) { $first.Wer } else { "Application event log, event id $($first.EventId)" }

Write-Output 'hw-postflight: crash report matched by the report fields (AppName/AppPath) + time (Windows WER / event log).'
Write-Output ("  source: " + $source)
Write-Output ("  copied: " + $dest)
$hdmpNote = if ($first.Kind -eq 'wer' -and $first.Hdmp) { Join-Path 'tests/results' ($Slug + '.hdmp') } else { $null }
if ($hdmpNote) { Write-Output ("  copied (optional): " + $hdmpNote) }
Write-Output ("  sha256: " + $sha)
if ($ordered.Count -gt 1) {
    Write-Output ("  note: {0} report(s) matched; archived the EARLIEST. The others:" -f $ordered.Count)
    $ordered | Select-Object -Skip 1 | ForEach-Object {
        if ($_.Wer) { Write-Output ("    " + $_.Wer) }
        else { Write-Output ("    Application event id $($_.EventId) at $($_.Time)") }
    }
}
Write-Output ''
Write-Output '----8<---- reference this in the record ----8<----'
$leaf = Split-Path -Leaf $dest
$ref = '- Crash report: [`' + $leaf + '`](./' + $leaf + '), sha256 `' + $sha + '` (Windows WER/event-log sweep)'
Write-Output $ref
Write-Output '----8<------------------------------------------8<----'
exit 0
