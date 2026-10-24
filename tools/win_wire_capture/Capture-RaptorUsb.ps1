#requires -Version 5.1
<#
.SYNOPSIS
  Run ONE continuous USB capture spanning the full vendor-app scan lifecycle
  (enumerate -> launch -> Start scan -> steady stream -> Stop -> close).

.DESCRIPTION
  Two capture engines:

    dumpcap  (default; needs a working Wireshark CLI + Npcap)
        Captures ALL \\.\USBPcap* interfaces into one .pcapng, so the Raptor's
        enumeration is caught whichever root hub it attaches to. Preferred:
        one file, nanosecond timestamps, decodes directly in tshark.

    usbpcapcmd  (fallback; works even when the Wireshark CLI is broken)
        Captures one chosen root hub with -A. Pick the hub with -UsbPcapDevice;
        run USBPcapCMD.exe with no args once to see the hub menu + device tree.

  CLEAN-ROOM: this observes the vendor app on the wire. Do NOT run any of our
  own tools (raw_capture, listener tests) against the device during the capture
  - the vendor app must be the only USB client (one SDK user at a time).

.PARAMETER DurationSeconds
  Auto-stop after N seconds (recommended: start this, then drive the lifecycle).
  Omit / 0 = capture until you press Enter.
.PARAMETER Engine
  'dumpcap' (default) or 'usbpcapcmd'. 'auto' picks usbpcapcmd if dumpcap won't load.
.PARAMETER UsbPcapDevice
  usbpcapcmd engine only, e.g. '\\.\USBPcap2'.
.NOTES
  Must run ELEVATED. For USB 3.0, run 'USBPcapCMD.exe -I' once as admin first.
#>
[CmdletBinding()]
param(
    [int]$DurationSeconds = 0,
    [ValidateSet('auto','dumpcap','usbpcapcmd')][string]$Engine = 'auto',
    [string]$UsbPcapDevice,
    [string]$WiresharkDir = 'C:\Program Files\Wireshark',
    [string]$UsbPcapDir   = 'C:\Program Files\USBPcap',
    [string]$OutFile
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\RaptorRef.ps1"

# Elevation + output path ------------------------------------------------------
$elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $elevated) { throw "Run this from an ELEVATED PowerShell (USBPcap needs admin)." }

New-Item -ItemType Directory -Force -Path $CaptureDir | Out-Null
if (-not $OutFile) {
    $OutFile = Join-Path $CaptureDir ("raptor-uvc-{0}.pcapng" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
}

function Test-DumpcapLoads {
    $p = Join-Path $WiresharkDir 'dumpcap.exe'
    if (-not (Test-Path $p)) { return $false }
    $out = & $p --version 2>&1; return ($LASTEXITCODE -eq 0 -and $out -match 'Dumpcap|Wireshark')
}

if ($Engine -eq 'auto') { if (Test-DumpcapLoads) { $Engine = 'dumpcap' } else { $Engine = 'usbpcapcmd' } }

# Lifecycle script the operator follows while the capture runs -----------------
function Show-Lifecycle {
    Write-Host "`n--- Drive this lifecycle now (one continuous capture) ---" -ForegroundColor Cyan
    Write-Host "  1. (if not already) plug in + power the Raptor      <- enumeration"
    Write-Host "  2. launch CrealityScan"
    Write-Host "  3. press Start scan"
    Write-Host "  4. hold a steady scan for ~15-30 s"
    Write-Host "  5. press Stop"
    Write-Host "  6. close CrealityScan / disconnect"
    Write-Host "  If the app crashes mid-capture: STOP here, keep the file, don't retry blind.`n" -ForegroundColor DarkYellow
}

Write-Host "Capture engine : $Engine"
Write-Host "Output file    : $OutFile"

if ($Engine -eq 'dumpcap') {
    $dumpcap = Join-Path $WiresharkDir 'dumpcap.exe'
    if (-not (Test-DumpcapLoads)) {
        throw "dumpcap does not load (Wireshark runtime incomplete). Use -Engine usbpcapcmd, or repair Wireshark (see Check-Toolchain.ps1)."
    }
    $ifaces = (& $dumpcap -D 2>&1) | Select-String 'USBPcap' | ForEach-Object {
        if ($_ -match '(\\\\\.\\USBPcap\d+)') { $matches[1] }
    }
    if (-not $ifaces) { throw "dumpcap -D listed no USBPcap interfaces (is USBPcap installed and the service up?)." }
    Write-Host ("Capturing on   : {0}" -f ($ifaces -join ', '))
    $argList = @(); foreach ($i in $ifaces) { $argList += @('-i', $i) }
    $argList += @('-w', $OutFile, '-n')
    if ($DurationSeconds -gt 0) { $argList += @('-a', "duration:$DurationSeconds") }
    Show-Lifecycle
    if ($DurationSeconds -gt 0) {
        Write-Host "Running for $DurationSeconds s..." -ForegroundColor Green
        & $dumpcap @argList
    } else {
        $proc = Start-Process -FilePath $dumpcap -ArgumentList $argList -NoNewWindow -PassThru
        Read-Host "Capturing... press ENTER to stop"
        if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null; Start-Sleep 1 }
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    }
}
else {  # usbpcapcmd fallback
    $usbpcap = Join-Path $UsbPcapDir 'USBPcapCMD.exe'
    if (-not (Test-Path $usbpcap)) { throw "USBPcapCMD not found at $usbpcap" }
    if (-not $UsbPcapDevice) {
        Write-Host "`nNo -UsbPcapDevice given. Run this to see the hubs + device tree, then pass e.g. -UsbPcapDevice \\.\USBPcap2:" -ForegroundColor Yellow
        Write-Host "    & '$usbpcap'`n"
        throw "Specify -UsbPcapDevice (the root hub the Raptor is on)."
    }
    # -A = all devices on the hub; keeps enumeration regardless of address.
    $argList = @('-d', $UsbPcapDevice, '-A', '-o', $OutFile)
    Write-Host ("Capturing on   : {0} (all devices)" -f $UsbPcapDevice)
    Show-Lifecycle
    if ($DurationSeconds -gt 0) {
        $proc = Start-Process -FilePath $usbpcap -ArgumentList $argList -NoNewWindow -PassThru
        Write-Host "Running for $DurationSeconds s..." -ForegroundColor Green
        Start-Sleep -Seconds $DurationSeconds
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    } else {
        $proc = Start-Process -FilePath $usbpcap -ArgumentList $argList -NoNewWindow -PassThru
        Read-Host "Capturing... press ENTER to stop"
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    }
}

if (Test-Path $OutFile) {
    $sz = [math]::Round((Get-Item $OutFile).Length / 1MB, 1)
    Write-Host "`nSaved $sz MB -> $OutFile" -ForegroundColor Green
    Write-Host "Next: Decode-Capture.ps1 -PcapFile `"$OutFile`"" -ForegroundColor Cyan
} else {
    Write-Host "`nNo output file was produced." -ForegroundColor Red
}
