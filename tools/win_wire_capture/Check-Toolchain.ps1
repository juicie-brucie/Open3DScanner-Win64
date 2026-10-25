#requires -Version 5.1
<#
.SYNOPSIS
  Preflight doctor for the win64 USB wire-capture harness.

.DESCRIPTION
  Verifies the four things a live capture+decode needs, and prints a clear
  verdict for each:
    1. USBPcapCMD.exe   (capture engine)
    2. Npcap service    (dumpcap's packet backend)
    3. tshark / dumpcap  (Wireshark CLI - capture + decode; must actually LOAD)
    4. Administrator     (USBPcap needs elevation; USB 3.0 needs -I once)

  Exits 0 only when capture AND decode are both possible.
.NOTES
  Clean-room: observation only. Touches no vendor binary, runs no SDK.
#>
[CmdletBinding()]
param(
    [string]$WiresharkDir = 'C:\Program Files\Wireshark',
    [string]$UsbPcapDir   = 'C:\Program Files\USBPcap'
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\RaptorRef.ps1"

$ok = @(); $bad = @()
function Pass($m){ $script:ok  += $m; Write-Host "  [ OK ] $m" -ForegroundColor Green }
function Fail($m){ $script:bad += $m; Write-Host "  [FAIL] $m" -ForegroundColor Red }
function Note($m){ Write-Host "  [info] $m" -ForegroundColor DarkGray }

Write-Host "`n== win64 wire-capture toolchain check ==`n"

# 1. USBPcapCMD ----------------------------------------------------------------
$usbpcap = Join-Path $UsbPcapDir 'USBPcapCMD.exe'
if (Test-Path $usbpcap) {
    $v = & $usbpcap -h 2>&1 | Select-Object -First 1
    if ($v -match 'Usage') { Pass "USBPcapCMD present: $usbpcap" }
    else { Fail "USBPcapCMD found but did not respond to -h" }
} else { Fail "USBPcapCMD not found at $usbpcap" }

# 2. Npcap ---------------------------------------------------------------------
$npcap = Get-Service npcap -ErrorAction SilentlyContinue
if ($npcap -and $npcap.Status -eq 'Running') { Pass "Npcap service running" }
elseif ($npcap) { Fail "Npcap service present but $($npcap.Status) (start it, or reinstall)" }
else { Fail "Npcap not installed (bundle it with the Wireshark installer)" }

# 3. tshark / dumpcap - must actually initialise, not just exist ---------------
foreach ($exe in 'tshark','dumpcap') {
    $p = Join-Path $WiresharkDir "$exe.exe"
    if (-not (Test-Path $p)) { Fail "$exe not found at $p"; continue }
    # A broken install exits with STATUS_DLL_NOT_FOUND (0xC0000135) and no output.
    $out = & $p --version 2>&1
    $code = $LASTEXITCODE
    if ($code -eq 0 -and ($out -match 'TShark|Dumpcap|Wireshark')) {
        Pass "$exe loads OK ($(( $out | Select-Object -First 1)))"
    } else {
        Fail "$exe present but FAILS TO LOAD (exit 0x$('{0:X8}' -f $code)) - Wireshark runtime is incomplete"
    }
}

# 4. Admin / USB3 init ---------------------------------------------------------
$elevated = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($elevated) { Pass "Running elevated (USBPcap capture + USB3 -I available)" }
else { Note "Not elevated - USBPcap capture needs an elevated shell; USB 3.0 capture needs 'USBPcapCMD -I' once as admin." }

# Verdict ----------------------------------------------------------------------
Write-Host ""
if ($bad.Count -eq 0) {
    Write-Host "All checks passed - ready to capture and decode." -ForegroundColor Green
    exit 0
}
Write-Host "BLOCKED: $($bad.Count) problem(s)." -ForegroundColor Yellow
if ($bad -match 'FAILS TO LOAD') {
    # Diagnose the usual cause: x64 VC++ runtime missing from the real System32.
    $sysnative = Join-Path $env:SystemRoot 'Sysnative'   # real 64-bit System32 even from a 32-bit host
    $sys = if (Test-Path $sysnative) { $sysnative } else { Join-Path $env:SystemRoot 'System32' }
    $miss = @('vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll') |
        Where-Object { -not (Test-Path (Join-Path $sys $_)) }
    if ($miss) {
@"

  Cause: the x64 Visual C++ runtime is MISSING from System32:
    $($miss -join ', ')
  Every 64-bit Wireshark DLL needs these, so tshark/dumpcap can't start.
  Fix (ELEVATED shell) - repair/force-reinstall the x64 VC++ redistributable:
    winget install --id Microsoft.VCRedist.2015+.x64 -e --force
  or: download https://aka.ms/vs/17/release/vc_redist.x64.exe and run
    vc_redist.x64.exe /repair
  Capture via USBPcapCMD.exe still works without this; only DECODE is blocked.
"@ | Write-Host -ForegroundColor DarkYellow
    } else {
        Write-Host "`n  tshark/dumpcap won't load but the x64 VC++ runtime is present - run the LoadLibrary probe in the README to find the missing DLL.`n" -ForegroundColor DarkYellow
    }
}
exit 1
