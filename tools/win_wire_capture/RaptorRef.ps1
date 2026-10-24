# RaptorRef.ps1 - shared reference values for the win64 USB wire-capture harness.
#
# Clean-room note: every value here comes from the WORK ORDER
# (win64-wire-capture-wo.md) and the parent's own `descriptor_table.h` summary
# quoted in it. Nothing here is read from a vendor binary. Dot-source this file
# from the other scripts:  . "$PSScriptRoot\RaptorRef.ps1"

# --- Parent descriptor_table.h reference (the "compare-to-parent" target) -----
# The committed Probe/Commit we expect to see on the wire for each stream.
$script:RaptorParentRef = [ordered]@{
    # stream  => @{ FormatIndex; FrameIndex }   (bFormatIndex, bFrameIndex)
    Depth = [ordered]@{ FormatIndex = 1; FrameIndex = 7 }   # (fmt 1, frame 7)
    IR    = [ordered]@{ FormatIndex = 2; FrameIndex = 1 }   # (2,1)
    RGB   = [ordered]@{ FormatIndex = 1; FrameIndex = 2 }   # (1,2)

    # Shared across all three streams, per the WO:
    dwFrameInterval   = 333333      # 100-ns units -> 30.0003 fps
    bcdUVC            = 0x0110      # UVC 1.1
    ProbeCommitLength = 34         # bytes
}

# UVC request codes (bRequest on the control endpoint) -------------------------
$script:UvcRequest = @{
    SET_CUR = 0x01; GET_CUR = 0x81; GET_MIN = 0x82; GET_MAX = 0x83
    GET_RES = 0x84; GET_LEN = 0x85; GET_INFO = 0x86; GET_DEF = 0x87
}
# VideoStreaming interface control selectors (wValue high byte) -----------------
$script:UvcVsSelector = @{ VS_PROBE_CONTROL = 0x01; VS_COMMIT_CONTROL = 0x02 }

# Standard USB requests we care about ------------------------------------------
$script:UsbRequest = @{ CLEAR_FEATURE = 0x01; SET_INTERFACE = 0x0B }
$script:UsbFeature = @{ ENDPOINT_HALT = 0x00 }

# USBPcap/pcap usb.transfer_type values ----------------------------------------
$script:UsbXferType = @{ 0 = 'isochronous'; 1 = 'interrupt'; 2 = 'control'; 3 = 'bulk' }

# Where raw captures live (gitignored; never committed - see .gitignore) --------
$script:CaptureDir = Join-Path (Resolve-Path "$PSScriptRoot\..\..").Path 'captures\win-wire'
