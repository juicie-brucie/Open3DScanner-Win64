# Windows enum parity — CrealityScan 4.3.2 (row #57 follow-up)

*Recon result: read-only byte-level scan of the installed Windows DLLs. No
decompiler, no Ghidra project import — the 84.6 MB lib_orbbec_scan.dll would
take hours to import; the values below were located by raw binary search of
little-endian encodings instead.*

## Artifact pins (Task A)

Computed from the installed copy under `C:\Program Files\CrealityScan\...`:

| Artifact | Size (bytes) | SHA-256 | FileVersion | ProductVersion | ProductName |
|---|---|---|---|---|---|
| `lib_orbbec_scan.dll` | 88,709,120 | `9E7B677191A1460E620F2089D972CC231BCCF27666565A1DBF1FFA1566E48787` | `3.5.3.260921` | `3.5.3.260921` | `OBScan` |
| `OrbbecSDK.dll` | 9,263,616 | `D36C0E8EA1E2123C5BC8288D8676608DA255BFFD8063E2B051DCF39C3FA79231` | *(none)* | *(none)* | *(none)* |
| `OrbbecGPL.dll` | 3,570,176 | `2CB4622CFDAB5C5F4DAF9BE7992EF0D7012730940BA0F3C56D3D4DAFF67091DE` | *(none)* | *(none)* | *(none)* |

**Version parity:** `lib_orbbec_scan.dll` ships `FileVersion`/`ProductVersion`
`3.5.3.260921` — the same build the macOS lane recovered from
`liborbbec_scan.3.5.3.dylib` ([win64-surface.md](#)). `OrbbecSDK.dll` carries
no readable `VS_VERSION_INFO` under light tooling ([win64-surface.md](#));
its version is unconfirmed on Windows (macOS ships `1.7.1`). `OrbbecGPL.dll`
also carries no readable version resource.

---

## Method

1. **String-table presence:** read all ASCII `OB_SCAN_*` identifiers from each
   DLL. `lib_orbbec_scan.dll` contains **218** `OB_SCAN_*` ASCII identifier
   strings (status codes, message IDs, mode/config enums, etc.).
   `OrbbecSDK.dll` and `OrbbecGPL.dll` contain zero `OB_SCAN_*` strings — the
   enum name tables live in the scan library, not the SDK DLL. No UTF-16LE
   `OB_SCAN_*` strings found in any DLL.

2. **Numeric value search:** converted `lib_orbbec_scan.dll` to a contiguous
   hex string and searched for each macOS-recovered enum value as a little-endian
   4-byte integer, then as an 8-byte integer for anything not found at 4 bytes.
   Every match is a data-section integer — the value is present as a standalone
   LE encoding in the binary. Absence of a match means the value does not appear
   as a data-section integer at that width; it may still exist as an instruction
   immediate or jump-table entry in code.

All searches run with `JAVA_HOME`, `GHIDRA_INSTALL_DIR`,
`REA_ANALYSIS_PROVIDER=ghidra` set in the same shell invocation as every REA
call (each shell call is a fresh process).

---

## Confirmed values (90 of 103 key values — all identical to macOS)

Every value below was found as a standalone little-endian integer in
`lib_orbbec_scan.dll` and matches the macOS-recovered header value exactly.

### OBScanStatus — 40 of 43 named values found

| Symbol | macOS value | Windows value | Match? |
|---|---|---|---|
| `OB_SCAN_SUCCESS` | 0 | 0 | identical |
| `OB_SCAN_ERROR_INVALID_ARGUMENT` | 845283584 (`0x32620100`) | same | identical |
| `OB_SCAN_ERROR_FATAL` | 845283585 | same | identical |
| `OB_SCAN_ERROR_DEVICE_DISCONNECT` | 845283586 | same | identical |
| `OB_SCAN_ERROR_SESSION_PAUSED` | 845283587 | same | identical |
| `OB_SCAN_ERROR_SESSION_NOT_PAUSED` | 845283588 | same | identical |
| `OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION` | 845283589 | same | identical |
| `OB_SCAN_ERROR_CAMERA_PERMISSION_NOT_GRANTED` | 845283590 | same | identical |
| `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | 845283591 | same | identical |
| `OB_SCAN_ERROR_LISTENER_NOT_FOUND` | 845283593 | same | identical |
| `OB_SCAN_ERROR_MULTI_CALLED` | 845283600 | same | identical |
| `OB_SCAN_ERROR_CAMERA_NOT_REGISTER` | 845283601 | same | identical |
| `OB_SCAN_ERROR_OUT_MEMORY` | 845283602 | same | identical |
| `OB_SCAN_ERROR_FILE_NOT_EXIST` | 845283603 | same | identical |
| `OB_SCAN_ERROR_FILE_READ_FAILED` | 845283604 | same | identical |
| `OB_SCAN_ERROR_FILE_WRITE_FAILED` | 845283605 | same | identical |
| `OB_SCAN_ERROR_GRAPHICS_PROBLEM` | 845283606 | same | identical |
| `OB_SCAN_ERROR_IMPORT_EMPTY_PROJ` | 845283608 | same | identical |
| `OB_SCAN_ERROR_IMPORT_SELF_PROJ` | 845283609 | same | identical |
| `OB_SCAN_ERROR_NO_PLUG_IN_POWER` | 845283617 | same | identical |
| `OB_SCAN_ERROR_INVALID_SCAN` | 845283618 | same | identical |
| `OB_SCAN_ERROR_DEVICE_BUSY` | 845283619 | same | identical |
| `OB_SCAN_ERROR_CANCEL_RUN` | 845283620 | same | identical |
| `OB_SCAN_ERROR_CAMERA_PARAMS_INCONSISTENT` | 845283621 | same | identical |
| `OB_SCAN_ERROR_LASER_MARKER_FRAMEWORK_INVALID_CLOUD_SCAN` | 845283622 | same | identical |
| `OB_SCAN_ERROR_OUT_HARD_DISK` | 845283623 | same | identical |
| `OB_SCAN_ERROR_READ_DEVICE_PARAMS` | 845283624 | same | identical |
| `OB_SCAN_ERROR_IMPORT_NOT_SUPPORT_PROJ` | 845283625 | same | identical |
| `OB_SCAN_ERROR_IMPORT_HIGH_VERSION_PROJ` | 845283632 | same | identical |
| `OB_SCAN_ERROR_RESTORE_FACTORY_CRC_CHECK_FAILED` | 845283633 | same | identical |
| `OB_SCAN_ERROR_RESTORE_FACTORY_DATA_WRITE_FAILED` | 845283634 | same | identical |
| `OB_SCAN_ERROR_AI_HEAD_RESTORATION_NOT_DETECT_FACE` | 845283635 | same | identical |
| `OB_SCAN_ERROR_MESH_NOT_FOUND_HOLE` | 845283636 | same | identical |
| `OB_SCAN_ERROR_PROJ_NOT_WRITE_PERMISSION` | 845283637 | same | identical |
| `OB_SCAN_ERROR_SET_LED_LASER` | 845283638 | same | identical |
| `OB_SCAN_ERROR_SET_IR_MODE` | 845283640 | same | identical |
| `OB_SCAN_ERROR_AI_REPLACE_RGB_FEATS_NOT_OBVIOUS` | 845283648 | same | identical |
| `OB_SCAN_ERROR_IMPORTED_MESH_SIZE_NOT_SUPPORT` | 845283649 | same | identical |
| `OB_SCAN_ERROR_MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION` | 845283650 | same | identical |
| `OB_SCAN_ERROR_ALLOCATE_GPU_RESOURCES_FAILED` | 845283651 | same | identical |

Not found as data-section integers: `OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED`
(845283592), `OB_SCAN_ERROR_HAVE_NOT_MARKER` (845283607),
`OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS` (845283639) — see
**[Not found as standalone integers](#not-found-as-standalone-integers)** below.

**Gap-side split (all 13 are Windows-side):** every one of the 13 not-found
values exists in the macOS header (recovered from the macOS binary's `*_name()`
tables — see `engine/include/openscanner/obscan_api.h`), so the macOS side has
direct evidence for all 13. The gap is in our Windows read: the values do not
appear as standalone data-section integers in `lib_orbbec_scan.dll`. There are
**zero macOS-side residuals** in this table — no row is attributed to the parent
artifact.

### Device message IDs — 11 of 12 found

| Symbol | macOS value | Windows value | Match? |
|---|---|---|---|
| `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT` | 0 | 0 | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_DISCONNECT` | 308412672 (`0x12620100`) | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_SUCCESS` | 308412688 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED` | 308412689 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_REBOOT_FAILED` | 308412690 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED_IMAGE_PROBLEM` | 308412691 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_NEED_CALIBRATION` | 308412692 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_REBOOT_SUCCESS` | 308412693 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_READ_PARAMS_FAILED` | 308412695 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_CRC_CHECK_FAILED` | 308412696 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED_LOW_VERSION` | 308412697 | same | identical |

Not found: `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_WITH_PROBLEM` (308412694) — see below.

### Tips message IDs — 3 of 5 found

| Symbol | macOS value | Windows value | Match? |
|---|---|---|---|
| `OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INCREASE_EXPOSURE_TIME` | 308412928 (`0x12620200`) | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_TIPS_DECREASE_EXPOSURE_TIME` | 308412929 | same | identical |
| `OB_SCAN_MESSAGE_ID_DEVICE_TIPS_BLUE_LINE_LASER_MODE_WITHOUT_FILTER` | 308412931 | same | identical |

Not found: `OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INFRARED_MODE_WITH_FILTER_OUTDOORS`
(308412930), `OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INFRARED_MARKER_MODE_WITHOUT_FILTER`
(308412932) — see below.

### Control message IDs — 20 of 24 found

All found values match macOS exactly, including:
`RAPTOR_MIDDLE_BUTTON_SHORT_PRESS` = 0, `RAPTOR_MIDDLE_BUTTON_LONG_PRESS` =
342884864 (`0x14700200`), `RAPTOR_UP_BUTTON_SHORT_PRESS` = 342884865,
`RAPTOR_UP_BUTTON_LONG_PRESS` = 342884866, `RAPTOR_DOWN_BUTTON_SHORT_PRESS` =
342884867, `RAPTOR_LEFT_BUTTON_SHORT_PRESS` = 342884869,
`RAPTOR_LEFT_BUTTON_LONG_PRESS` = not found, `RAPTOR_RIGHT_BUTTON_SHORT_PRESS` =
342884871, `RAPTOR_RIGHT_BUTTON_LONG_PRESS` = not found,
`OTTER_ON_OFF_BUTTON_SHORT_PRESS` = 342884873, `OTTER_ON_OFF_BUTTON_LONG_PRESS` =
342884880, `OTTER_EXPOSURE_PLUS_BUTTON_SHORT_PRESS` = 342884881,
`OTTER_EXPOSURE_PLUS_BUTTON_LONG_PRESS` = not found,
`OTTER_EXPOSURE_SUBTRACT_BUTTON_SHORT_PRESS` = 342884883,
`OTTER_EXPOSURE_SUBTRACT_BUTTON_LONG_PRESS` = 342884884,
`WIFI_CONTROLLER_REMAINING_BATTERY` = 342884885,
`WIFI_CONTROLLER_DOUBLE_CLICK` = 342884886,
`RAPTOR_MIDDLE_BUTTON_DOUBLE_CLICK` = 342884887,
`RAPTOR_UP_BUTTON_DOUBLE_CLICK` = 342884888,
`RAPTOR_DOWN_BUTTON_DOUBLE_CLICK` = 342884889,
`RAPTOR_LEFT_BUTTON_DOUBLE_CLICK` = 342884896,
`RAPTOR_RIGHT_BUTTON_DOUBLE_CLICK` = 342884897,
`FPGA_UPGRADE_PROGRESS_STATE` = 342884898.

Not found: `RAPTOR_DOWN_BUTTON_LONG_PRESS` (342884868),
`RAPTOR_LEFT_BUTTON_LONG_PRESS` (342884870),
`RAPTOR_RIGHT_BUTTON_LONG_PRESS` (342884872),
`OTTER_EXPOSURE_PLUS_BUTTON_LONG_PRESS` (342884882) — see below.

### Calib message IDs — 8 of 9 found

All found values match macOS exactly: `CALIB_SUCCESS` = 0,
`CALIB_FAILED` = 342884608 (`0x14700100`), `CAPTURE_QR_CODE_SUCCESS` =
342884609, `CAPTURE_QR_CODE_FAILED` = 342884610,
`CALIB_DEVICE_REBOOT_FAILED` = 342884611, `CALIB_DEVICE_REBOOT_SUCCESS` =
342884612, `CALIB_UNMATCH_CALIB_BOARD` = 342884614,
`CALIB_MV_OPTICAL_FILTER` = 342884615.

Not found: `CAPTURE_QR_CODE_UNMATCH_CALIB_BOARD` (342884613) — see below.

### Data message IDs — 4 of 4 found

All match macOS: `NEW_RGB_IMAGE` = 0, `NEW_DEPTH_IMAGE` = 325189888
(`0x13620100`), `NEW_IR_IMAGE` = 325189889, `FRAME_READY` = 325189890.

### Process message IDs — 4 of 6 found

All found match macOS: `MAX_FRAMES_REACHED` = 341967105 (`0x14620101`),
`SCANNING_FAILED` = 341967107,
`OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE` = 341967108,
`PASTED_MARKERS_TOO_REGULAR` = 341967109.

Not found: `FRAME_BUFFER_FULL` (341967104), `DETECT_FACE` (341967106) — see below.

---

## Not found as standalone integers — 13 values

Each of these was searched as both a 4-byte and an 8-byte little-endian
integer and found **zero** times in `lib_orbbec_scan.dll`. They are likely
instruction immediates or jump-table entries in code rather than data-section
integers, which is why a raw LE scan misses them. **[inferred]** — each sits in
a consecutive sequence bounded by confirmed values on both sides, so they are
very likely identical on Windows, but this is an inference, not a direct
observation. Do not treat them as confirmed parity.

| Symbol | macOS value | gap side | Surrounding confirmed values | Reasoning |
|---|---|---|---|---|
| `OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED` | 845283592 (`0x32620108`) | Windows | `..._DEADLINE_EXCEEDED` = `0x32620107` confirmed; `..._LISTENER_NOT_FOUND` = `0x32620109` confirmed | Consecutive run: 07 → [missing] → 09 |
| `OB_SCAN_ERROR_HAVE_NOT_MARKER` | 845283607 (`0x32620117`) | Windows | `..._GRAPHICS_PROBLEM` = `0x32620116` confirmed; `..._IMPORT_EMPTY_PROJ` = `0x32620118` confirmed | Consecutive run: 16 → [missing] → 18 |
| `OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS` | 845283639 (`0x32620137`) | Windows | `..._SET_LED_LASER` = `0x32620136` confirmed; `..._SET_IR_MODE` = `0x32620138` confirmed | Consecutive run: 36 → [missing] → 38 |
| `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_WITH_PROBLEM` | 308412694 (`0x12620116`) | Windows | `..._REBOOT_SUCCESS` = `0x12620115` confirmed; `..._READ_PARAMS_FAILED` = `0x12620117` confirmed | Consecutive run: 15 → [missing] → 17 |
| `OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INFRARED_MODE_WITH_FILTER_OUTDOORS` | 308412930 (`0x12620202`) | Windows | `..._DECREASE_EXPOSURE_TIME` = `0x12620201` confirmed; `..._BLUE_LINE_LASER_MODE_WITHOUT_FILTER` = `0x12620203` confirmed | Consecutive run: 01 → [missing] → 03 |
| `OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INFRARED_MARKER_MODE_WITHOUT_FILTER` | 308412932 (`0x12620204`) | Windows | `..._BLUE_LINE_LASER_MODE_WITHOUT_FILTER` = `0x12620203` confirmed; nothing confirmed after | Consecutive run: 03 → [missing] → end of tips block |
| `OB_SCAN_MESSAGE_ID_RAPTOR_DOWN_BUTTON_LONG_PRESS` | 342884868 (`0x14700204`) | Windows | `..._DOWN_SHORT_PRESS` = `0x14700203` confirmed; `..._LEFT_SHORT_PRESS` = `0x14700205` confirmed | Consecutive run: 03 → [missing] → 05 |
| `OB_SCAN_MESSAGE_ID_RAPTOR_LEFT_BUTTON_LONG_PRESS` | 342884870 (`0x14700206`) | Windows | `..._LEFT_SHORT_PRESS` = `0x14700205` confirmed; `..._RIGHT_SHORT_PRESS` = `0x14700207` confirmed | Consecutive run: 05 → [missing] → 07 |
| `OB_SCAN_MESSAGE_ID_RAPTOR_RIGHT_BUTTON_LONG_PRESS` | 342884872 (`0x14700208`) | Windows | `..._RIGHT_SHORT_PRESS` = `0x14700207` confirmed; `..._OTTER_ON_OFF_SHORT_PRESS` = `0x14700209` confirmed | Consecutive run: 07 → [missing] → 09 |
| `OB_SCAN_MESSAGE_ID_OTTER_EXPOSURE_PLUS_BUTTON_LONG_PRESS` | 342884882 (`0x14700212`) | Windows | `..._PLUS_SHORT_PRESS` = `0x14700211` confirmed; `..._SUBTRACT_SHORT_PRESS` = `0x14700213` confirmed | Consecutive run: 11 → [missing] → 13 |
| `OB_SCAN_MESSAGE_ID_CAPTURE_QR_CODE_UNMATCH_CALIB_BOARD` | 342884613 (`0x14700105`) | Windows | `..._REBOOT_SUCCESS` = `0x14700104` confirmed; `..._UNMATCH_CALIB_BOARD` = `0x14700106` confirmed | Consecutive run: 04 → [missing] → 06 |
| `OB_SCAN_MESSAGE_ID_FRAME_BUFFER_FULL` | 341967104 (`0x14620100`) | Windows | nothing confirmed before in process block; `..._MAX_FRAMES_REACHED` = `0x14620101` confirmed | First entry of process block; next confirmed at 01 |
| `OB_SCAN_MESSAGE_ID_DETECT_FACE` | 341967106 (`0x14620102`) | Windows | `..._MAX_FRAMES_REACHED` = `0x14620101` confirmed; `..._SCANNING_FAILED` = `0x14620103` confirmed | Consecutive run: 01 → [missing] → 03 |

---

## String-table anomalies

The vendor binary itself contains both `OB_SCAN_MESSAGE_ID_SCANING_FAILED`
(typo, missing 'N') and `OB_SCAN_MESSAGE_ID_SCANNING_FAILED` strings. The
header uses the corrected `SCANNING_FAILED` spelling (ID `341967107`). It is
unclear which string the `*_name()` function returns for that ID. **[inferred]**

---

## OPEN QUESTIONS

1. **Settle the 13 missing values.** A Ghidra import of
   `lib_orbbec_scan.dll` (estimated hours for an 84.6 MB binary — notepad.exe
   at 600 KB took ~179 s) or targeted disassembly of the `*_name()` functions
   would confirm whether each missing ID exists and with what value. Until then,
   the 13 remain **[inferred]** — bounded by confirmed neighbors, not observed.
   **Gap-side attribution: all 13 are Windows-side.** Every one exists in the
   macOS header (recovered from the macOS binary's `*_name()` tables), so the
   macOS side has direct evidence; the gap is that our Windows read did not find
   them as standalone data-section integers. There are **zero macOS-side
   residuals** — no row is a parent-side gap, and OPEN QUESTIONS does not
   attribute any of the 13 to the parent artifact.
2. **Name-table structure.** Only the raw integer values and the identifier
   strings were located. The association between specific values and specific
   names (the `*_name()` lookup tables) was not reverse-engineered; value-to-name
   mapping is inferred from sequence ordering, not from observed name-table
   entries.
3. **OrbbecSDK.dll version.** No readable `VS_VERSION_INFO` under light tooling.
   The macOS side ships `libOrbbecSDK.1.7.1.dylib`; the Windows `OrbbecSDK.dll`
   version is unconfirmed.

---

## Evidence (paths)

- Installed DLLs (read-only, not copied):
  `C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64/lib_orbbec_scan.dll`
  `C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64/OrbbecSDK.dll`
  `C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64/OrbbecGPL.dll`
- Related: `docs/recon/findings/win64-surface.md` (export counts, version parity,
  ABI caveats) — this document complements it.
- Related: `engine/include/openscanner/obscan_api.h` (the recovered C ABI
  contract; `OBScanStatus` enum at lines 39–83).
