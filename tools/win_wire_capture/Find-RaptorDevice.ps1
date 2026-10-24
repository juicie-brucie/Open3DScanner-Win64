#requires -Version 5.1
<#
.SYNOPSIS
  Identify the CR-Scan Raptor on the USB bus and show its VID/PID + interfaces
  so the trace is provably comparable to the parent's device (WO step 2).

.DESCRIPTION
  Lists present USB devices that look like the scanner: a UVC camera / imaging
  device, an Orbbec-vendor device, or a multi-interface composite. For each it
  prints VID, PID, class, instance id and the child interface list (the MI_xx
  functions), then prints the parent reference for the operator to confirm the
  match before capturing.

  The per-packet USB *device address* (needed to filter the capture) is a
  capture-time property, not a PnP one - Decode-Capture.ps1 reads it from the
  enumeration in the .pcapng. This script establishes identity only.

.PARAMETER Snapshot
  Write the current USB device set to a file (run before plugging the Raptor
  in). Re-run with -Diff to show exactly what the plug-in added.
.PARAMETER Diff
  Compare against the -Snapshot file and highlight newly-appeared devices.
.NOTES
  Clean-room: PnP enumeration only. No vendor binary is read; no SDK is run.
#>
[CmdletBinding()]
param(
    [switch]$Snapshot,
    [switch]$Diff,
    [string]$SnapshotPath = "$env:TEMP\raptor-usb-snapshot.txt"
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\RaptorRef.ps1"

# All present USB function/device nodes, with VID/PID pulled from the instance id.
function Get-UsbNodes {
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -like 'USB\VID_*' } |
        ForEach-Object {
            $vid = $devpid = $null
            if ($_.InstanceId -match 'VID_([0-9A-Fa-f]{4}).*PID_([0-9A-Fa-f]{4})') {
                $vid = $matches[1].ToUpper(); $devpid = $matches[2].ToUpper()
            }
            [PSCustomObject]@{
                VID = $vid; PID = $devpid; Class = $_.Class
                Name = $_.FriendlyName; InstanceId = $_.InstanceId
            }
        }
}

if ($Snapshot) {
    Get-UsbNodes | ForEach-Object { $_.InstanceId } | Sort-Object |
        Set-Content -Path $SnapshotPath
    Write-Host "Snapshot of $((Get-Content $SnapshotPath).Count) USB nodes -> $SnapshotPath"
    Write-Host "Now plug in / power the Raptor, then re-run with -Diff."
    return
}

$nodes = Get-UsbNodes

if ($Diff) {
    if (-not (Test-Path $SnapshotPath)) { throw "No snapshot at $SnapshotPath; run -Snapshot first." }
    $before = Get-Content $SnapshotPath
    $new = $nodes | Where-Object { $_.InstanceId -notin $before }
    Write-Host "`n== Devices that appeared since the snapshot ==" -ForegroundColor Cyan
    if (-not $new) { Write-Host "  (none - did the Raptor enumerate?)" -ForegroundColor Yellow }
    else { $new | Format-Table VID,PID,Class,Name -AutoSize }
    $candidates = $new
} else {
    # Heuristic match: UVC camera / imaging / media, Orbbec vendor (2BC5),
    # or a composite device (the Raptor presents as one).
    $candidates = $nodes | Where-Object {
        $_.Class -in 'Camera','Image','Media' -or
        $_.VID -eq '2BC5' -or
        ($_.Class -eq 'USB' -and $_.Name -match 'Composite')
    }
}

Write-Host "`n== Candidate scanner devices ==" -ForegroundColor Cyan
if (-not $candidates) {
    Write-Host "  No camera/composite/Orbbec device present. Is the Raptor plugged in and powered?" -ForegroundColor Yellow
} else {
    foreach ($c in ($candidates | Sort-Object VID,PID -Unique)) {
        Write-Host ("`n  {0}:{1}  [{2}]  {3}" -f $c.VID,$c.PID,$c.Class,$c.Name) -ForegroundColor White
        Write-Host ("    instance: {0}" -f $c.InstanceId) -ForegroundColor DarkGray
        # Child interface functions (MI_xx) of this composite, with their class.
        $kids = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -like "*$($c.InstanceId.Split('\')[-1])*MI_*" -or
                           $_.Parent -eq $c.InstanceId }
        if ($kids) {
            foreach ($k in $kids) {
                if ($k.InstanceId -match 'MI_([0-9A-Fa-f]{2})') {
                    Write-Host ("      interface {0}  [{1}]  {2}" -f $matches[1],$k.Class,$k.FriendlyName) -ForegroundColor DarkCyan
                }
            }
        }
    }
}

# Parent reference, for the operator to eyeball the match ----------------------
Write-Host "`n== Parent descriptor_table.h reference (confirm the trace matches) ==" -ForegroundColor Cyan
Write-Host ("    Depth  bFormatIndex={0} bFrameIndex={1}" -f $RaptorParentRef.Depth.FormatIndex, $RaptorParentRef.Depth.FrameIndex)
Write-Host ("    IR     bFormatIndex={0} bFrameIndex={1}" -f $RaptorParentRef.IR.FormatIndex,    $RaptorParentRef.IR.FrameIndex)
Write-Host ("    RGB    bFormatIndex={0} bFrameIndex={1}" -f $RaptorParentRef.RGB.FormatIndex,   $RaptorParentRef.RGB.FrameIndex)
Write-Host ("    dwFrameInterval={0}  bcdUVC=0x{1:X4}  probe/commit len={2}" -f `
    $RaptorParentRef.dwFrameInterval, $RaptorParentRef.bcdUVC, $RaptorParentRef.ProbeCommitLength)
Write-Host "    (parent's --probe saw SEVEN interfaces on the composite.)`n" -ForegroundColor DarkGray
