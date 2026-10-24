# win: USBPcap trace of a working vendor UVC scan session (row #TBD, win lane 100+)

*Status `RECON`. **Wire observation of the vendor app**, not decompilation —
USBPcap + tshark only; no `obscan_*` DLL is read or run, no SDK of ours touches
the device during the capture (the vendor app is the sole USB client). The raw
`.pcapng` is kept locally only (gitignored) and never committed. This is the
Phase-D part-2 byte-for-byte validation reference and the empirical answer to
parent findings #201 (control-path-not-HID) and #202 (IR 4× / light-state).*

> **STATUS: first (idle) capture done 2026-10-24; live scan-session capture still
> pending.** The harness in [`tools/win_wire_capture/`](../../../tools/win_wire_capture/)
> is built and validated. A 30 s USBPcap capture was taken with the Raptor connected
> but **CrealityScan not streaming**, so it carries the device's **descriptors only**
> (via USBPcapCMD `--inject-descriptors`), not live probe/commit or streaming. The
> descriptor-derived facts below are confirmed; the dynamic control-plane sections
> (committed probe/commit, XU laser/exposure timing, IR light-state, handshake order)
> are marked _PENDING LIVE SCAN_. **Run-record discipline: this file is write-once;
> corrections go in as dated addenda, not edits.**

## Capture metadata

| field | value |
|--|--|
| capture date | 2026-10-24 05:22 (first idle capture) |
| local `.pcapng` path | `captures/win-wire/raptor-first-20261024-052237.pcapng` (merged) + per-hub `-USBPcap2.pcapng` (Raptor) (gitignored) |
| capture engine | USBPcapCMD multi-hub, `-A --inject-descriptors` (dumpcap has no USBPcap extcap on this box) |
| capture window | 30 s, device **idle** (no scan) — descriptors only |
| vendor app | CrealityScan 4.3.2 (Windows), not streaming during this capture |
| commit SHA | _TBD_ |

## Device identity

_From the capture's injected descriptors (USBPcap2 hub)._

| field | wire | note |
|--|--|--|
| VID:PID | **2BC5:0C03** ("Creality Raptor Pro") | Orbbec VID; matches the parent's device |
| bus_id / device_address | 2 / 2 (on `\\.\USBPcap2`) | NB: merged-file addresses collide across hubs — filter per-hub |
| interface count | **7** | matches parent's `--probe` (seven) |
| enumerated functions | Depth Camera, IR Camera, RGB Camera, HID "bar code badge reader", composite | three UVC cameras + one input-only HID |

## Descriptor-derived facts (first idle capture — CONFIRMED)

Decoded from the injected config descriptor (`tshark -V`, Raptor on `\\.\USBPcap2`).

**Interface map (7 interfaces = 3x [VideoControl + VideoStreaming] + 1 HID):**

| if | class / subclass | role | stream EP | transfer type | wMaxPacketSize | notes |
|--|--|--|--|--|--|--|
| 0 | Video / 0x01 (VC) | Camera 1 control | — | — | — | **Extension Unit Entity 4, bNumControls 17** |
| 1 | Video / 0x02 (VS) | Camera 1 = **Depth** | **0x81 IN** | **BULK (0x02)** | **1024** | Format 1 = **Y14**, frame 7 = **480x300** |
| 2 | Video / 0x01 (VC) | Camera 2 control | — | — | — | **Extension Unit Entity 8, bNumControls 3** |
| 3 | Video / 0x02 (VS) | Camera 2 | **0x82 IN** | **BULK (0x02)** | 1024 | (IR or RGB — map at live scan) |
| 4 | Video / 0x01 (VC) | Camera 3 control | — | — | — | **Extension Unit Entity 12, bNumControls 255** |
| 5 | Video / 0x02 (VS) | Camera 3 | **0x83 IN** | **BULK (0x02)** | 1024 | (IR or RGB — map at live scan) |
| 6 | HID (0x03) / 0x00 | control/status HID | **0x85 IN** | **INTERRUPT (0x03)** | **248** | **input-only, no OUT endpoint** |

Key confirmations:
- **bcdUVC 0x0110** on every VideoControl header — matches parent.
- **Streaming is BULK, not isochronous** — all three VS endpoints (0x81/0x82/0x83) are
  `bmAttributes 0x02`, 1024-byte max packet, single alt-setting (no iso ladder). The
  parent's `raw_capture` should do **bulk IN** on these, not iso alt-setting switching.
- **Depth = Y14 (14-bit), Format 1 / Frame 7 / 480x300** — matches parent `descriptor_table.h` depth (fmt 1, frame 7) and the WO's depth geometry.
- Camera 1 (Depth, interface 1) also advertises Format 3 = Y8 and Format 4 = MJPEG with
  frames up to 1920x1200; the full format/frame menu is in the raw capture.
- **Every camera exposes an Extension Unit** (entities 4 / 8 / 12). The HID (interface 6)
  is **interrupt-IN only, 248-byte reports, no OUT endpoint** — so laser/exposure cannot
  be driven over HID. This is empirical support for **#201 (control-path-not-HID)**: the
  control plane is the UVC XU and/or the vendor RPC property channel, not HID output.

## Probe / Commit (VS_PROBE_CONTROL / VS_COMMIT_CONTROL)

_Every `SET_CUR`/`GET_CUR` on selectors 1/2, per stream. Committed 34-byte
struct verbatim (hex), with `bmHint`, `bFormatIndex`, `bFrameIndex`,
`dwFrameInterval`, `dwMaxVideoFrameSize`, `dwMaxPayloadTransferSize`._

<!-- paste the "Probe / Commit" + "Committed structs" tables from Decode-Capture.ps1 here -->
_TBD_

## Compare to parent (`descriptor_table.h`) — flag any mismatch LOUDLY

Two rows of evidence: the **descriptor menu** (what the device advertises, from this
idle capture) and the **committed** probe/commit (what CrealityScan selects, _PENDING
LIVE SCAN_).

| stream | parent (fmt, frame) | descriptor advertises | committed (live) | match? |
|--|--|--|--|--|
| Depth | (1, 7) | Format 1 (Y14), frame 7 = 480x300 **present** | _PENDING LIVE SCAN_ | descriptor ✓ |
| IR    | (2, 1) | _which VS interface is IR TBD at live scan_ | _PENDING LIVE SCAN_ | _pending_ |
| RGB   | (1, 2) | _which VS interface is RGB TBD at live scan_ | _PENDING LIVE SCAN_ | _pending_ |

- `bcdUVC` **0x0110** — **confirmed** (matches parent).
- interface count **7** — **confirmed** (matches parent).
- probe/commit length 34 and `dwFrameInterval` 333333 — _PENDING LIVE SCAN_ (no probe/commit in an idle capture).

## Interface setup

_`SET_INTERFACE` alt-setting per VideoStreaming interface; any
`CLEAR_FEATURE(ENDPOINT_HALT)`._

<!-- paste "Interface setup" tables here -->
_TBD_

## Streaming endpoint

_Endpoint address, **transfer type (bulk vs isochronous)**, payload/packet
sizes, observed frame interval vs committed `dwFrameInterval`._

From descriptors (**confirmed**): three VideoStreaming endpoints **0x81 / 0x82 / 0x83**,
all **BULK** (`bmAttributes 0x02`), `wMaxPacketSize` **1024**, single alt-setting (no iso
ladder). So capture is bulk IN per camera.

Observed payload sizes and frame interval under load: _PENDING LIVE SCAN_ (no streaming
traffic in the idle capture).

## Control plane — laser / exposure (#201, #202)

_Any Extension-Unit (XU) `GET_CUR`/`SET_CUR`: unit ID + selector + payload. Any
vendor/class request toggling laser or exposure, **with timing relative to
frames**. If nothing appears on USB → RPC-only (confirms #201 "not HID")._

From descriptors (**confirmed**): each camera's VideoControl interface carries an
**Extension Unit** — Entity **4** (17 controls), Entity **8** (3 controls), Entity **12**
(255 controls). The HID (interface 6) is **interrupt-IN only, 248-byte reports, no OUT
endpoint**, so it cannot carry control writes. => Laser/exposure is driven by the UVC XU
and/or the vendor RPC property channel, **not HID** — empirical support for **#201**.

Which XU selector(s) + payload(s) actually toggle laser/exposure, and their timing vs
frames: _PENDING LIVE SCAN_.

## Pre-first-frame handshake order

_The exact sequence of control transfers issued before the first streamed frame._

<!-- paste "Pre-first-frame handshake order" table here -->
_TBD_

---

## 5-line summary (for the channel report)

1. **Indices/interval match `descriptor_table.h`?** Descriptors match: 7 interfaces, bcdUVC 0x0110, Depth = Format 1 / Frame 7 / 480x300 (Y14). Committed indices + dwFrameInterval 333333 _PENDING LIVE SCAN_.
2. **Bulk or isoch; payload/frame sizes?** **BULK** — three VS endpoints 0x81/0x82/0x83, bmAttributes 0x02, 1024-byte max packet, single alt-setting. Under-load payload/frame sizes _PENDING LIVE SCAN_.
3. **What drives laser/exposure?** Not HID (interface 6 is interrupt-IN only, 248 B, no OUT). Each camera has a UVC Extension Unit (entities 4/8/12) → XU and/or RPC. Exact selector+payload _PENDING LIVE SCAN_.
4. **IR 4× / light-state toggle (#202)?** _PENDING LIVE SCAN_ (needs streaming).
5. **Pre-first-frame handshake order?** _PENDING LIVE SCAN_ (needs a scan start).

## Addenda

_(dated corrections only — never edit above this line)_
