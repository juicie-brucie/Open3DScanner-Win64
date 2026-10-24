# win64 USB wire-capture harness

Observe the **vendor app** (CrealityScan) driving the CR-Scan Raptor over USB,
and decode the control plane into a findings spec the parent's `raw_capture`
builder can consume. This is **wire observation, not decompilation** — USBPcap +
tshark only, no vendor binary is read, no SDK of ours is run.

> **Clean-room / one-SDK-user rule:** during a capture the vendor app must be the
> **only** USB client touching the device. Do **not** run our `raw_capture`,
> listener tests, or any SDK tool against the Raptor while capturing. The capture
> is read-only. If CrealityScan crashes mid-capture, **stop and keep the file** —
> do not retry blind.

## Scripts

| script | what it does |
|--|--|
| `Check-Toolchain.ps1`   | Preflight: USBPcapCMD, Npcap, tshark/dumpcap-that-actually-loads, admin. Prints the exact fix if anything's missing. Run this first. |
| `Find-RaptorDevice.ps1` | Identify the Raptor (VID/PID, class, interfaces) and compare to the parent's `descriptor_table.h`. `-Snapshot` before plug-in, `-Diff` after, to see exactly what enumerated. |
| `Capture-RaptorUsb.ps1` | One continuous capture across the full lifecycle. `dumpcap` engine (all USBPcap hubs → one `.pcapng`) or `usbpcapcmd` fallback. |
| `Decode-Capture.ps1`    | tshark → a paste-ready Markdown block: identity, Probe/Commit (34-byte struct verbatim), interface setup, streaming endpoints, Extension-Unit/vendor control, handshake order — auto-compared to the parent. |
| `RaptorRef.ps1`         | Shared reference values (parent descriptor_table.h, UVC request codes). Dot-sourced by the others. |

## Prerequisites

- **CrealityScan** installed and known to stream. ✅ (v4.3.2 present)
- **USBPcap** (admin driver install). ✅
- **Wireshark / tshark + dumpcap that LOAD.** ⚠️ needs the **x64 VC++ runtime** in
  System32 — see *Known issue* below.
- Run everything from an **elevated** PowerShell. For USB 3.0, run once as admin:
  `& 'C:\Program Files\USBPcap\USBPcapCMD.exe' -I`

## Run order

```powershell
# from an ELEVATED PowerShell, in this folder
.\Check-Toolchain.ps1                       # 1. verify tooling
.\Find-RaptorDevice.ps1 -Snapshot           # 2a. BEFORE plugging in the Raptor
#   ... plug in + power the Raptor ...
.\Find-RaptorDevice.ps1 -Diff               # 2b. confirm identity vs parent
.\Capture-RaptorUsb.ps1 -DurationSeconds 60 # 3. drive: launch → Start → ~20s → Stop → close
.\Decode-Capture.ps1 -PcapFile <path printed above>   # 4. decode + compare
```

Captures land in `..\..\captures\win-wire\` (gitignored — never committed).
Decode writes `<capture>.decode.md`; paste its block into
`docs/recon/findings/win-uvc-wire-capture.md` and finalise the 5-line summary.

## Known issue — tshark/dumpcap won't start (`0xC0000135`)

On this box the **x64 Visual C++ Redistributable runtime is missing from
System32** (`vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` — only the
x86 copies are in SysWOW64). Every 64-bit Wireshark DLL depends on these, so the
CLI exits immediately with `STATUS_DLL_NOT_FOUND`. The registry may *list* the
x64 redist as installed; the files simply aren't deployed (corrupt install).

**Fix (elevated):**
```powershell
winget install --id Microsoft.VCRedist.2015+.x64 -e --force
# or: download https://aka.ms/vs/17/release/vc_redist.x64.exe ; vc_redist.x64.exe /repair
```

`USBPcapCMD.exe` does **not** need this, so you can still *capture* while the
runtime is being fixed — only *decode* is blocked.

### Manual find-the-missing-DLL probe (if the above doesn't resolve it)
```powershell
Add-Type -Namespace N -Name K -MemberDefinition '[DllImport("kernel32",SetLastError=true,CharSet=CharSet.Unicode)]public static extern System.IntPtr LoadLibraryEx(string f,System.IntPtr h,uint flags);'
'vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','ucrtbase.dll' | % {
  $h=[N.K]::LoadLibraryEx($_,[IntPtr]::Zero,0)
  '{0,-22} {1}' -f $_, $(if($h -eq [IntPtr]::Zero){"FAIL $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}else{'ok'})
}
```

## What the decode answers (the 5-line channel summary)

1. Do the committed indices/interval match `descriptor_table.h`?
   (Depth fmt1/frame7, IR 2/1, RGB 1/2, all `dwFrameInterval` 333333, bcdUVC 0x0110, len 34)
2. Streaming transfer type (bulk vs isochronous) + payload/frame sizes.
3. What drives laser/exposure — an Extension-Unit selector + payload, or nothing
   on USB (i.e. RPC-only)? (parent findings #201 "not HID", #202 IR light-state)
4. Does the IR stream show a 4× / light-state toggle (#202)?
5. The pre-first-frame control handshake order.
