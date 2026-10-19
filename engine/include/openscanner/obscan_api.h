#pragma once

// Clean-room C ABI declarations for Orbbec's "OBScan" scanning SDK
// (OBScan.SDK namespace), as used by CrealityScan 4.3.2.
//
// Provenance: every name, parameter order, type and struct field layout
// below was recovered independently by decompiling CrealityScan's IL2CPP
// metadata (global-metadata.dat + GameAssembly.{dll,dylib}) with
// Il2CppDumper, and cross-checked against the real exported symbol table
// of the shipped liborbbec_scan / lib_orbbec_scan binary (nm -gU). No
// vendor header, source file or binary is reproduced here — this file
// only restates the observed calling contract, the same way any SDK
// binding is written against a third-party ABI.
//
// Struct field offsets are inferred from IL2CPP's recorded managed-struct
// layout unless a comment marks them otherwise; treat them as a working
// hypothesis until a smoke test confirms round-tripping (see tools/smoke_test).
// Natively verified so far: ObScanDeviceMessage (16 bytes: int32 msg_id,
// int32 pad, void* data), the ObScanDeviceMessageID enum, and the whole
// ObScanStatus enum (checked against the vendor's own status-name table).

#include <stdbool.h>
#include <stddef.h>   /* offsetof, for the ABI guards below */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Status / error codes (OBScan.SDK.OBScanStatus)
//
// Complete and natively verified against the vendor's own status-name table:
// 42 named values plus 26 genuinely reserved slots (…010a-0f, …011a-20,
// …012a-2f, …0139-3f). Quirk worth knowing: every obscan_* wrapper returns
// OB_SCAN_ERROR_DEADLINE_EXCEEDED when its primary handle is NULL, so that
// code does not necessarily mean a timeout.
// ---------------------------------------------------------------------------
typedef enum ObScanStatus {
    OB_SCAN_SUCCESS = 0,
    OB_SCAN_ERROR_INVALID_ARGUMENT = 845283584,
    OB_SCAN_ERROR_FATAL = 845283585,
    OB_SCAN_ERROR_DEVICE_DISCONNECT = 845283586,
    OB_SCAN_ERROR_SESSION_PAUSED = 845283587,
    OB_SCAN_ERROR_SESSION_NOT_PAUSED = 845283588,
    OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION = 845283589,
    OB_SCAN_ERROR_CAMERA_PERMISSION_NOT_GRANTED = 845283590,
    OB_SCAN_ERROR_DEADLINE_EXCEEDED = 845283591,
    OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED = 845283592,
    OB_SCAN_ERROR_LISTENER_NOT_FOUND = 845283593,
    OB_SCAN_ERROR_MULTI_CALLED = 845283600,
    OB_SCAN_ERROR_CAMERA_NOT_REGISTER = 845283601,
    OB_SCAN_ERROR_OUT_MEMORY = 845283602,
    OB_SCAN_ERROR_FILE_NOT_EXIST = 845283603,
    OB_SCAN_ERROR_FILE_READ_FAILED = 845283604,
    OB_SCAN_ERROR_FILE_WRITE_FAILED = 845283605,
    OB_SCAN_ERROR_GRAPHICS_PROBLEM = 845283606,
    OB_SCAN_ERROR_HAVE_NOT_MARKER = 845283607,
    OB_SCAN_ERROR_IMPORT_EMPTY_PROJ = 845283608,
    OB_SCAN_ERROR_IMPORT_SELF_PROJ = 845283609,
    OB_SCAN_ERROR_NO_PLUG_IN_POWER = 845283617,
    OB_SCAN_ERROR_INVALID_SCAN = 845283618,
    OB_SCAN_ERROR_DEVICE_BUSY = 845283619,
    OB_SCAN_ERROR_CANCEL_RUN = 845283620,
    OB_SCAN_ERROR_CAMERA_PARAMS_INCONSISTENT = 845283621,
    OB_SCAN_ERROR_LASER_MARKER_FRAMEWORK_INVALID_CLOUD_SCAN = 845283622,
    OB_SCAN_ERROR_OUT_HARD_DISK = 845283623,
    OB_SCAN_ERROR_READ_DEVICE_PARAMS = 845283624,
    OB_SCAN_ERROR_IMPORT_NOT_SUPPORT_PROJ = 845283625,
    OB_SCAN_ERROR_IMPORT_HIGH_VERSION_PROJ = 845283632,
    OB_SCAN_ERROR_RESTORE_FACTORY_CRC_CHECK_FAILED = 845283633,
    OB_SCAN_ERROR_RESTORE_FACTORY_DATA_WRITE_FAILED = 845283634,
    OB_SCAN_ERROR_AI_HEAD_RESTORATION_NOT_DETECT_FACE = 845283635,
    OB_SCAN_ERROR_MESH_NOT_FOUND_HOLE = 845283636,
    OB_SCAN_ERROR_PROJ_NOT_WRITE_PERMISSION = 845283637,
    OB_SCAN_ERROR_SET_LED_LASER = 845283638,
    OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS = 845283639,
    OB_SCAN_ERROR_SET_IR_MODE = 845283640,
    OB_SCAN_ERROR_AI_REPLACE_RGB_FEATS_NOT_OBVIOUS = 845283648,
    OB_SCAN_ERROR_IMPORTED_MESH_SIZE_NOT_SUPPORT = 845283649,
    OB_SCAN_ERROR_MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION = 845283650,
    OB_SCAN_ERROR_ALLOCATE_GPU_RESOURCES_FAILED = 845283651,
} ObScanStatus;

// ---------------------------------------------------------------------------
// Mode-configuration enums (OBScan.SDK.*)
// ---------------------------------------------------------------------------
typedef enum ObScanProjection {
    OB_SCAN_INFRARED = 0,
    OB_SCAN_BLUE_LINE_LASER = 1,
} ObScanProjection;

typedef enum ObScanLightType {
    OB_SCAN_LIGHT_TYPE_PARALLEL = 0,
    OB_SCAN_LIGHT_TYPE_CROSS = 1,
    OB_SCAN_LIGHT_TYPE_SINGLE = 2,
} ObScanLightType;

typedef enum ObScanScanType {
    OB_SCAN_CLOUD_FUSED = 0,
    OB_SCAN_LASER_DETAILED = 1,
    OB_SCAN_MARKER_FRAMEWORK = 2,
} ObScanScanType;

typedef enum ObScanMode {
    OB_SCAN_MODE_FACE = 0,
    OB_SCAN_MODE_OBJECT_SMALL = 1,
    OB_SCAN_MODE_OBJECT_MEDIUM = 2,
    OB_SCAN_MODE_OBJECT_BIG = 3,
    OB_SCAN_MODE_HUMAN = 4,
} ObScanMode;

typedef enum ObScanFuseMode {
    OB_SCAN_FUSE_MODE_GEOMETRY = 0,
    OB_SCAN_FUSE_MODE_RGBD = 1,
    OB_SCAN_FUSE_MODE_MARKER = 2,
    OB_SCAN_FUSE_MODE_MARKERLESS = 3,
} ObScanFuseMode;

typedef enum ObScanAccuracy {
    OB_SCAN_ACCURACY_FAST = 0,
    OB_SCAN_ACCURACY_HIGH = 1,
} ObScanAccuracy;

typedef enum ObScanTexture {
    OB_SCAN_HAVE_TEXTURE = 0,
    OB_SCAN_NO_TEXTURE = 1,
} ObScanTexture;

typedef enum ObScanFov {
    OB_SCAN_FOV_NORMAL = 0,
    OB_SCAN_FOV_WIDE = 1,
} ObScanFov;

typedef enum ObScanFrameRate {
    OB_SCAN_FRAME_RATE_AUTO = 0,
    OB_SCAN_FRAME_RATE_STANDARD = 1,
    OB_SCAN_FRAME_RATE_BALANCED = 2,
    OB_SCAN_FRAME_RATE_FAST = 3,
} ObScanFrameRate;

typedef enum ObScanObjectMaterial {
    OB_SCAN_OBJECT_MATERIAL_STANDARD = 0,
    OB_SCAN_OBJECT_MATERIAL_BLACK = 1,
    OB_SCAN_OBJECT_MATERIAL_WHITE = 2,
    OB_SCAN_OBJECT_MATERIAL_HIGHLY_REFLECTIVE = 3,
} ObScanObjectMaterial;

// ---------------------------------------------------------------------------
// Structs (field order/offsets recovered from IL2CPP struct layout)
// ---------------------------------------------------------------------------

// OBScan.SDK.OBScanModeConfig — 0x38 bytes
typedef struct ObScanModeConfig {
    ObScanTexture post_texture;          // 0x00
    ObScanFrameRate frame_rate;          // 0x04
    ObScanProjection projection;         // 0x08
    ObScanMode scan_mode;                // 0x0C
    ObScanAccuracy accuracy;             // 0x10
    ObScanFuseMode fuse_mode;            // 0x14
    ObScanFov scan_fov;                  // 0x18
    ObScanObjectMaterial object_material;// 0x1C
    double voxel_size;                   // 0x20
    ObScanLightType light_type;          // 0x28
    ObScanScanType scan_type;            // 0x2C
    double detail_voxel_size;            // 0x30
} ObScanModeConfig;

// OBScan.SDK.OBScanContextConfig — all fields are native string pointers.
// Marshaling (UTF-8 vs. platform ANSI) is unconfirmed; validate in the
// smoke test before relying on non-ASCII paths.
typedef struct ObScanContextConfig {
    const char* input_algo_dependent_files_dir;  // 0x00
    const char* output_ob_sdk_log_dir;            // 0x08
    const char* output_ob_scan_log_dir;           // 0x10
    const char* output_computer_performance_fps_dir; // 0x18
    const char* output_windows_gpu_info_dir;      // 0x20 (Windows-only; pass NULL on macOS)
} ObScanContextConfig;

// OBScan.SDK.OBScanDeviceInfo — 0x48 bytes
typedef struct ObScanDeviceInfo {
    int32_t camera_pid;                   // 0x00
    const char* camera_name;              // 0x08
    const char* camera_serial_number;     // 0x10
    const char* camera_connection_type;   // 0x18
    const char* camera_firmware_version;  // 0x20
    int32_t wifi_device_pid;              // 0x28
    const char* wifi_device_ssid;         // 0x30
    const char* wifi_device_firmware_version; // 0x38
    uint32_t device_calibration_timestamp;    // 0x40
} ObScanDeviceInfo;

// OBScan.SDK.OBScanSessionConfig — all three fields are native pointers/bool.
typedef struct ObScanSessionConfig {
    const char* output_dir;          // 0x00
    const char* name;                // 0x08
    bool is_import_mesh_file;        // 0x10
} ObScanSessionConfig;

// OBScan.SDK.OBScanConfig — wraps a mode config plus a display name for one
// scan within a session.
typedef struct ObScanConfig {
    ObScanModeConfig mode_config;    // 0x00
    const char* name;                // 0x38
} ObScanConfig;

// OBScan.SDK.OBScanPoint / OBScanNormal / OBScanColor — per-vertex data used
// by the point-cloud and mesh accessors below.
typedef struct ObScanPoint {
    float x, y, z, intensity;
} ObScanPoint;

typedef struct ObScanNormal {
    float nx, ny, nz, nw;
} ObScanNormal;

typedef struct ObScanColor {
    uint8_t r, g, b, a;
} ObScanColor;

// OBScan.SDK.OBScanPointClouds — the raw live/accumulated point cloud for a
// scan. `points` and `normals` are vendor-owned buffers of `num_points`
// entries each, returned as borrowed pointers (no copy, no refcount bump;
// obscan_scan_release_raw_cloud is a no-op) — copy out immediately; the next
// cloud publish/clear can move or empty them. See
// docs/recon/findings/scan-live-data.md §2.
typedef struct ObScanPointClouds {
    uint32_t num_points;    // 0x00
    ObScanPoint* points;    // 0x08
    ObScanNormal* normals;  // 0x10
} ObScanPointClouds;

// OBScan.SDK.OBScanPointColorClouds — point cloud with per-point color.
typedef struct ObScanPointColorClouds {
    uint32_t num_points;    // 0x00
    ObScanPoint* points;    // 0x08
    ObScanNormal* normals;  // 0x10
    ObScanColor* colors;    // 0x18
} ObScanPointColorClouds;

// OBScan.SDK.OBScanCloudBbox — axis-aligned bounds of the published model
// cloud, filled by obscan_scan_get_model_point_color_clouds. 48 bytes:
// min/max/center float triples with a 4-byte pad word after each; center is
// (min+max)/2. [inferred: obscan_common::compute_cloud_bbox @ 0x6ad464 /
// init_obscan_bbox @ 0x6ad540 — .recon-cache/listings/scan-live-data.plan24.asm]
typedef struct ObScanCloudBbox {
    float min[3];           // 0x00
    uint32_t pad0;          // 0x0c
    float max[3];           // 0x10
    uint32_t pad1;          // 0x1c
    float center[3];        // 0x20
    uint32_t pad2;          // 0x2c
} ObScanCloudBbox;

// OBScan.SDK.OBScanTriangle — a face as three vertex indices into a mesh's
// vertex array.
typedef struct ObScanTriangle {
    uint32_t v0, v1, v2;
} ObScanTriangle;

// OBScan.SDK.OBScanTriangleMesh — the live/preview reconstructed mesh for a
// scan (see obscan_scan_get_realtime_model_mesh). The getter returns pointers
// into the SDK's mesh cache on the scan object (borrowed, no copy); the next
// getter call re-fills it and obscan_scan_realtime_model_mesh_release frees it
// — copy out immediately. See docs/recon/findings/scan-live-data.md §14.
typedef struct ObScanTriangleMesh {
    uint32_t num_vertices;      // 0x00
    ObScanPoint* vertices;      // 0x08
    ObScanNormal* normals;      // 0x10
    uint32_t num_triangles;     // 0x18
    ObScanTriangle* triangles;  // 0x20
} ObScanTriangleMesh;

// ---------------------------------------------------------------------------
// Opaque handles
// ---------------------------------------------------------------------------
typedef void* ObScanContextHandle;
typedef void* ObScanDeviceManagementHandle;
typedef void* ObScanSessionHandle;
typedef void* ObScanScanHandle;

// ---------------------------------------------------------------------------
// Callback delegate shapes (OBScan.SDK.*CallbackDelegate). IL2CPP records
// ProgressCallbackDelegate/StatusCallbackDelegate as plain C-ABI function
// pointers (StatusCallbackDelegate is explicitly marked Cdecl in the
// metadata; the others follow the same convention). Whether NULL is
// accepted for an "optional-looking" callback parameter is not confirmed —
// that's inferred from the parameter being omittable on the C# call site,
// not verified against the native implementation. Treat as unverified
// until a smoke test confirms it for a given call.
// ---------------------------------------------------------------------------
typedef void (*ObScanProgressCallback)(float progress);
typedef void (*ObScanStatusCallback)(ObScanStatus status);

// ---------------------------------------------------------------------------
// OBScan.SDK.OBScanDeviceMessage / OBScanDeviceMessageDelegate /
// OBScanDeviceMessageListener (IL2CPP TypeDefIndex 12707-12709).
//
// Device detection on macOS — recovered model; full spec in
// docs/recon/findings/plan7-detection-path.md. The watchers start inside
// obscan_context_create(), before any listener exists (hardware run f4c5f86:
// MacPal.cpp:159 `Create PollingDeviceWatcher!` and the first GVCP hit land
// on/before the context-create moment). Both watchers live in the Orbbec SDK
// dylib this library links against (libOrbbecSDK.1.7.1.dylib, loaded via its
// @loader_path dependency libOrbbecSDK.1.7.dylib): libusb USB hotplug
// (watched vendor IDs 0x2BC5 = Orbbec and 0x1B20) plus a GVCP network
// watcher (~1 s cycles). liborbbec_scan itself imports no IOKit or CFRunLoop
// symbols; the process's IOServiceAddMatchingNotification calls are made by
// the SDK's own embedded libusb darwin backend when the USB transport
// context is constructed (USB device materialization / firmware-upgrade
// path), NOT at listener registration.
//
// Registering this listener only installs the message callback and then
// immediately re-evaluates the current device list via the device object's
// connect callback — which is why a DISCONNECT arrives synchronously inside
// the add call: that is the "no device yet" report (the data=NULL callback
// earlier code took for a registration acknowledgment), not watcher
// activity. Once a device is physically connected, the SDK delivers a
// message to onMessage (confirmed: msg_id=0 with a non-NULL data pointer
// corresponds to a real connect event, and
// obscan_device_management_get_device_num() called synchronously inside that
// callback correctly returns count=1).
//
// Registration guards, recovered natively: a NULL listener, or a second
// registration while one is already installed and never removed, both fail
// with OB_SCAN_ERROR_INVALID_ARGUMENT. The slot is single. Remove clears the
// slot and a re-add then returns SUCCESS [hw-observed: the
// 2026-10-17-plan8-device-message-listener run exercised exactly that pair]
// — but the cycle re-enters the vendor's device-creation machinery on the
// caller thread (Pioneer created!/destroyed!/created! ~200 ms apart) while
// the vendor NetDeviceWatcher (GVCP) thread later logs "Trying to create a
// device that you've already created!" and faults (NULL in
// get_active_device()).
// ABI-legal, correctness-unproven — findings §2/§8/§9, board #11.
// Messages emitted while no listener is installed are dropped, not queued.
// Because the add-time DISCONNECT is synchronous inside the add call, adding
// (or remove+add) from inside a callback re-enters that callback nested on the
// same thread; the watcher and scan paths deliver concurrently instead. See
// findings §4 (OQ-4).
// The listener object is referenced (a pointer to it is captured), never
// copied, so the caller must keep it alive until it is removed — and past
// obscan_context_destroy(): the SDK's background threads can outlive
// destroy while still holding the captured pointer, so freeing it together
// with the context is not safe; keep it valid for the life of the process
// (§8). Delivery happens on the emitting (vendor) thread
// [hw-confirmed: 2026-10-17-plan8 — DEVICE_CONNECT was invoked on the
// thread that registered the listener], so a listener must be thread-safe.
// See docs/recon/findings/device-message-listener.md.
//
// Message IDs, confirmed against the vendor's own native log, which writes
// "send device msg id : <NAME>" at the moment each one is delivered:
// 308412672 is OB_SCAN_MESSAGE_ID_DEVICE_DISCONNECT, and 0 is
// OB_SCAN_MESSAGE_ID_DEVICE_CONNECT. The SDK sends a DISCONNECT
// synchronously inside add_device_msg_listener() to report the initial
// "no device yet" state (this is the data=NULL callback earlier code took
// for a registration acknowledgment), then a CONNECT once the device is
// opened — delivered, per the recorded run, on the registration thread
// itself (findings §4). A DISCONNECT after a CONNECT is a real unplug.
//
// Known hazard — rewritten from the PLAN #8 crash analysis
// (tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md;
// main-thread re-read:
// tests/results/2026-10-17-crash-reports-main-thread-teardown-correction.md).
// Observed [hw-confirmed: 2026-10-17 — five crash reports + their raw logs]:
// (1) The fault site is inside the vendor SDK, strictly upstream of
// send_device_msg — update_camera_status -> update_device_info ->
// get_active_device() (device::OrbbecDevice) — on the vendor's
// NetDeviceWatcher (GVCP) thread: crash.ips frame #12 is
// libobsensor::pal::NetDeviceWatcher::start(...)::$_1, and the only images
// on the stack are liborbbec_scan / libOrbbecSDK / libsystem_pthread.
// (2) The conditions it was observed under — and it reproduces under them:
// an add-after-remove (re-add) of this listener, a DEVICE_CONNECT delivery
// to the caller's callback completing on the worker thread, add returning
// SUCCESS, then ~60 ms later that NetDeviceWatcher thread logging
// ObException.cpp:5 "Trying to create a device that you've already created!"
// and the process SIGSEGV'ing — a separate NetDeviceWatcher-initiated
// device-changed pass on a different thread, not a post-return continuation
// of the registration call. Five crash reports on this binary and worktree
// carry that sequence — plan8 16:54; the 19:21 test run; run2 20:01 ("the
// second reproduction of the identical crash signature today"); run3 22:41
// ("for a third time (plan8 run, run2, run3)"); run4 22:45 — so treat it as
// deterministic-ish under the OQ-3 remove/re-add sequence, not as general
// connect-callback fragility. The f4c5f86 plan1 run (2026-10-16; "a single
// registration, never a remove or a second add") neither supports nor
// contradicts this: it never exercised the path, and predates all five.
// (3) Main-thread state at the fault [hw-confirmed: the correction record —
// identical in all three archived reports]: the main thread was inside
// obscan_context_destroy() -> OBDeviceManagement::release() ->
// ~OrbbecDevice -> ~OrbbecInternalDevice -> OrbbecInternalDevice::stop(),
// blocked on a recursive_mutex. Teardown was racing the fault, not absent —
// the "never reached destroy" reading is dropped. No run in this series
// shows obscan_context_destroy() itself crashing: the f4c5f86 run's destroy
// returned SUCCESS in 807 ms with the worker exiting 0, so the
// "destroy/libomp crash" claim is unobserved in this series (the historical
// crash is separately documented and lldb-confirmed — README "Known issues").
// Not established: what leaves device bookkeeping in that NULL state — a
// pre-existing inconsistency that remove->add merely exposes, or re-entry
// while the device object re-creates (board #11); and whether stop()'s
// mutex is the one get_active_device()/update_device_info() take (open in
// the correction record). Until #11 rules, run anything that registers this
// listener inside a disposable worker process (see worker_process.h) — for
// the unproven NetDeviceWatcher threading around remove->add, not for
// callback-run fragility.
// ---------------------------------------------------------------------------
// OBScan.SDK.OBScanDeviceMessageID — the complete device family, recovered by
// decoding the vendor's own OBScanDeviceMessageID_name() lookup table. The
// connect-with-problem variants are directly actionable: they explain a device
// that enumerates (see get_device_num) but cannot be opened for scanning.
typedef enum ObScanDeviceMessageID {
    OB_SCAN_MESSAGE_ID_DEVICE_CONNECT = 0,
    OB_SCAN_MESSAGE_ID_DEVICE_DISCONNECT = 308412672,
    OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_SUCCESS = 308412688,
    OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED = 308412689,
    OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_REBOOT_FAILED = 308412690,
    OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED_IMAGE_PROBLEM = 308412691,
    OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_NEED_CALIBRATION = 308412692,
    OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_REBOOT_SUCCESS = 308412693,
    OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_WITH_PROBLEM = 308412694,
    OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_READ_PARAMS_FAILED = 308412695,
    OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_CRC_CHECK_FAILED = 308412696,
    OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED_LOW_VERSION = 308412697,
} ObScanDeviceMessageID;

// Field offsets below are natively verified (the message is passed by value in
// two registers and materialised with one 16-byte store).
typedef struct ObScanDeviceMessage {
    int32_t msg_id;  // 0x00 — ObScanDeviceMessageID.
    int32_t _pad;    // 0x04 — alignment padding to 8-byte boundary for `data`.
    void* data;      // 0x08 — payload; per-emitter, and NOT a device list
                     //        (findings §12): CONNECT (id 0) carries the address
                     //        of the manager's 1-byte "is support rescan" flag;
                     //        DISCONNECT and every other emitter on file pass NULL.
} ObScanDeviceMessage;

typedef void (*ObScanDeviceMessageDelegate)(ObScanDeviceMessage msg);

// Shape natively verified (see OQ-2 resolution, findings §6.1):
// docs/recon/findings/device-message-listener.md
typedef struct ObScanDeviceMessageListener {
    ObScanDeviceMessageDelegate onMessage;  // 0x00
} ObScanDeviceMessageListener;

// OBScan.SDK.OBScanDataMessage* / OBScanProcessMessage* — frames and process
// events arrive through these listeners rather than a polling API. Spec:
// docs/recon/findings/data-process-message-listeners.md (entry points §1,
// registration §2, preview guard §3, delivery §4, id tables §5). Listener
// object shape verified in
// docs/recon/findings/device-message-listener.md §6.1. Caller-
// facing rules recovered natively from the shipped dylib (all four exports
// verified; the process id values below are read from the vendor's own
// *_name table, not inferred):
// - single-slot registration: a second add without a remove, or a NULL
//   listener, fails with OB_SCAN_ERROR_INVALID_ARGUMENT; remove on an empty
//   slot returns OB_SCAN_ERROR_LISTENER_NOT_FOUND, after which a fresh add
//   succeeds;
// - BOTH listeners must be registered before obscan_scan_preview(): its
//   guard checks the data and process slots and otherwise reports
//   OB_SCAN_ERROR_LISTENER_NOT_FOUND;
// - data messages dispatch synchronously on the emitting thread and are
//   dropped, not queued, when the slot is empty; process messages are
//   enqueued for asynchronous dispatch (delivery thread / drop semantics —
//   findings OQ-P1), with a logged enqueue failure if the thread pool is
//   down.
typedef enum ObScanDataMessageID {
    OB_SCAN_MESSAGE_ID_NEW_RGB_IMAGE = 0,
    OB_SCAN_MESSAGE_ID_NEW_DEPTH_IMAGE = 325189888,
    OB_SCAN_MESSAGE_ID_NEW_IR_IMAGE = 325189889,
    OB_SCAN_MESSAGE_ID_FRAME_READY = 325189890,
} ObScanDataMessageID;

// Materialised with one 16-byte store at the send site, same rule as the
// device family above (send_data_msg copies the message by value, then
// invokes the function — findings §6).
typedef struct ObScanDataMessage {
    int32_t msg_id;  // 0x00 — ObScanDataMessageID.
    int32_t _pad;    // 0x04
    void* data;      // 0x08
} ObScanDataMessage;

typedef void (*ObScanDataMessageDelegate)(ObScanDataMessage msg);

typedef struct ObScanDataMessageListener {
    ObScanDataMessageDelegate onMessage;  // 0x00
} ObScanDataMessageListener;

// OBScan.SDK.OBScanProcessMessageID — completed by decoding the vendor's own
// native OBScanProcessMessageID_name() jump table (six entries,
// index = msg_id − 0x14620100; findings §5). All six values are read from
// the decode; 341967109 was also confirmed in hardware (the vendor log
// emits its name at delivery; a marker-layout warning, not a failure).
typedef enum ObScanProcessMessageID {
    OB_SCAN_MESSAGE_ID_FRAME_BUFFER_FULL = 341967104,
    OB_SCAN_MESSAGE_ID_MAX_FRAMES_REACHED = 341967105,
    OB_SCAN_MESSAGE_ID_DETECT_FACE = 341967106,
    OB_SCAN_MESSAGE_ID_SCANNING_FAILED = 341967107,
    OB_SCAN_MESSAGE_ID_OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE = 341967108,
    OB_SCAN_MESSAGE_ID_PASTED_MARKERS_TOO_REGULAR = 341967109,
} ObScanProcessMessageID;

// OBScan.SDK.OBScanDeviceTipsMessageID — decoded from the vendor's own
// OBScanDeviceTipsMessageID_name() pointer array (findings §11).
typedef enum ObScanDeviceTipsMessageID {
    OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INCREASE_EXPOSURE_TIME = 308412928,
    OB_SCAN_MESSAGE_ID_DEVICE_TIPS_DECREASE_EXPOSURE_TIME = 308412929,
    OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INFRARED_MODE_WITH_FILTER_OUTDOORS = 308412930,
    OB_SCAN_MESSAGE_ID_DEVICE_TIPS_BLUE_LINE_LASER_MODE_WITHOUT_FILTER = 308412931,
    OB_SCAN_MESSAGE_ID_DEVICE_TIPS_INFRARED_MARKER_MODE_WITHOUT_FILTER = 308412932,
} ObScanDeviceTipsMessageID;

// OBScan.SDK.OBScanControlMessageID — decoded from the vendor's own
// OBScanControlMessageID_name() byte-indexed jump table (findings §11). Ids
// in the gaps are reserved and name as "UNKNOWN".
typedef enum ObScanControlMessageID {
    OB_SCAN_MESSAGE_ID_RAPTOR_MIDDLE_BUTTON_SHORT_PRESS = 0,
    OB_SCAN_MESSAGE_ID_RAPTOR_MIDDLE_BUTTON_LONG_PRESS = 342884864,
    OB_SCAN_MESSAGE_ID_RAPTOR_UP_BUTTON_SHORT_PRESS = 342884865,
    OB_SCAN_MESSAGE_ID_RAPTOR_UP_BUTTON_LONG_PRESS = 342884866,
    OB_SCAN_MESSAGE_ID_RAPTOR_DOWN_BUTTON_SHORT_PRESS = 342884867,
    OB_SCAN_MESSAGE_ID_RAPTOR_DOWN_BUTTON_LONG_PRESS = 342884868,
    OB_SCAN_MESSAGE_ID_RAPTOR_LEFT_BUTTON_SHORT_PRESS = 342884869,
    OB_SCAN_MESSAGE_ID_RAPTOR_LEFT_BUTTON_LONG_PRESS = 342884870,
    OB_SCAN_MESSAGE_ID_RAPTOR_RIGHT_BUTTON_SHORT_PRESS = 342884871,
    OB_SCAN_MESSAGE_ID_RAPTOR_RIGHT_BUTTON_LONG_PRESS = 342884872,
    OB_SCAN_MESSAGE_ID_OTTER_ON_OFF_BUTTON_SHORT_PRESS = 342884873,
    OB_SCAN_MESSAGE_ID_OTTER_ON_OFF_BUTTON_LONG_PRESS = 342884880,
    OB_SCAN_MESSAGE_ID_OTTER_EXPOSURE_PLUS_BUTTON_SHORT_PRESS = 342884881,
    OB_SCAN_MESSAGE_ID_OTTER_EXPOSURE_PLUS_BUTTON_LONG_PRESS = 342884882,
    OB_SCAN_MESSAGE_ID_OTTER_EXPOSURE_SUBTRACT_BUTTON_SHORT_PRESS = 342884883,
    OB_SCAN_MESSAGE_ID_OTTER_EXPOSURE_SUBTRACT_BUTTON_LONG_PRESS = 342884884,
    OB_SCAN_MESSAGE_ID_WIFI_CONTROLLER_REMAINING_BATTERY = 342884885,
    OB_SCAN_MESSAGE_ID_WIFI_CONTROLLER_DOUBLE_CLICK = 342884886,
    OB_SCAN_MESSAGE_ID_RAPTOR_MIDDLE_BUTTON_DOUBLE_CLICK = 342884887,
    OB_SCAN_MESSAGE_ID_RAPTOR_UP_BUTTON_DOUBLE_CLICK = 342884888,
    OB_SCAN_MESSAGE_ID_RAPTOR_DOWN_BUTTON_DOUBLE_CLICK = 342884889,
    OB_SCAN_MESSAGE_ID_RAPTOR_LEFT_BUTTON_DOUBLE_CLICK = 342884896,
    OB_SCAN_MESSAGE_ID_RAPTOR_RIGHT_BUTTON_DOUBLE_CLICK = 342884897,
    OB_SCAN_MESSAGE_ID_FPGA_UPGRADE_PROGRESS_STATE = 342884898,
} ObScanControlMessageID;

// OBScan.SDK.OBScanCalibMessageID — decoded from the vendor's own
// OBScanCalibMessageID_name() comparison tree (findings §11).
typedef enum ObScanCalibMessageID {
    OB_SCAN_MESSAGE_ID_CALIB_SUCCESS = 0,
    OB_SCAN_MESSAGE_ID_CALIB_FAILED = 342884608,
    OB_SCAN_MESSAGE_ID_CAPTURE_QR_CODE_SUCCESS = 342884609,
    OB_SCAN_MESSAGE_ID_CAPTURE_QR_CODE_FAILED = 342884610,
    OB_SCAN_MESSAGE_ID_CALIB_DEVICE_REBOOT_FAILED = 342884611,
    OB_SCAN_MESSAGE_ID_CALIB_DEVICE_REBOOT_SUCCESS = 342884612,
    OB_SCAN_MESSAGE_ID_CAPTURE_QR_CODE_UNMATCH_CALIB_BOARD = 342884613,
    OB_SCAN_MESSAGE_ID_CALIB_UNMATCH_CALIB_BOARD = 342884614,
    OB_SCAN_MESSAGE_ID_CALIB_MV_OPTICAL_FILTER = 342884615,
} ObScanCalibMessageID;

// The 16-byte message materialization is natively verified for the data
// family only (send_data_msg — findings §6); this struct's runtime size
// stays [inferred] from the C# declaration until one is sized at a send
// site. docs/recon/findings/data-process-message-listeners.md §6.
typedef struct ObScanProcessMessage {
    int32_t msg_id;  // 0x00 — ObScanProcessMessageID
    int32_t _pad;    // 0x04
    void* data;      // 0x08
} ObScanProcessMessage;

typedef void (*ObScanProcessMessageDelegate)(ObScanProcessMessage msg);

typedef struct ObScanProcessMessageListener {
    ObScanProcessMessageDelegate onMessage;  // 0x00
} ObScanProcessMessageListener;

// ---------------------------------------------------------------------------
// Function pointer types for the subset of the obscan_* C ABI needed by the
// v1 smoke test (bootstrap + device enumeration). Resolved at runtime via
// ObscanLibrary (see obscan_loader.h) — this project never links the vendor
// library at build time.
// ---------------------------------------------------------------------------
typedef ObScanStatus (*fn_obscan_version)(const char** out_version);
typedef ObScanStatus (*fn_obscan_context_create)(ObScanContextHandle* out_context, ObScanContextConfig* config);
typedef ObScanStatus (*fn_obscan_context_destroy)(ObScanContextHandle* context);
typedef ObScanStatus (*fn_obscan_context_get_device_management)(ObScanContextHandle context, ObScanDeviceManagementHandle* out_device_handle);

// Registering BOTH of these on the context is required before
// obscan_scan_preview() will succeed — its guard inspects both listener
// slots and otherwise returns OB_SCAN_ERROR_LISTENER_NOT_FOUND (recovered
// natively in OBScanParasContext::judge_is_exist_listener; findings §3).
// Single-slot add/remove discipline, error codes and delivery semantics:
// see the message-family block above and findings §2/§4. The two remove_*
// entry points exist as exports and take a const listener (mangled K
// qualifier, findings §1).
typedef ObScanStatus (*fn_obscan_context_add_data_msg_listener)(ObScanContextHandle context, ObScanDataMessageListener* listener);
typedef ObScanStatus (*fn_obscan_context_add_process_msg_listener)(ObScanContextHandle context, ObScanProcessMessageListener* listener);
typedef ObScanStatus (*fn_obscan_context_remove_data_msg_listener)(ObScanContextHandle context, ObScanDataMessageListener const* listener);
typedef ObScanStatus (*fn_obscan_context_remove_process_msg_listener)(ObScanContextHandle context, ObScanProcessMessageListener const* listener);

typedef ObScanStatus (*fn_obscan_device_management_get_device_num)(ObScanDeviceManagementHandle device_handle, int32_t* out_device_num);
typedef ObScanStatus (*fn_obscan_device_management_get_device_info)(ObScanDeviceManagementHandle device_handle, int32_t idx, ObScanDeviceInfo* out_device_info);
typedef ObScanStatus (*fn_obscan_device_management_get_active_idx)(ObScanDeviceManagementHandle device_handle, int32_t* out_active_idx);
typedef ObScanStatus (*fn_obscan_device_management_select_active_device)(ObScanDeviceManagementHandle device_handle, int32_t active_idx);

// Device-detection results arrive through this listener; the detection
// watchers themselves start at obscan_context_create, not here (see the
// device-message block above and docs/recon/findings/plan7-detection-path.md
// §1–§5).
// Single-slot: registering a second listener without removing the first, or
// passing NULL, returns OB_SCAN_ERROR_INVALID_ARGUMENT. `listener` must stay
// alive for as long as the SDK might still invoke it (it is referenced, not
// copied) — including after obscan_context_destroy(); see the lifetime rule
// in the device-message block above. This project keeps its listeners in
// static storage for the whole process lifetime.
typedef ObScanStatus (*fn_obscan_device_management_add_device_msg_listener)(ObScanDeviceManagementHandle device_handle, ObScanDeviceMessageListener* listener);

// Session / scan lifecycle — the live-capture path this project actually
// cares about: context -> session -> scan -> start -> poll raw cloud ->
// stop -> destroy. Recovered the same way as the bootstrap subset above
// (IL2CPP decompilation, cross-checked symbol names against the real
// exported binary); struct field offsets are not yet independently
// confirmed via disassembly for this subset the way the bootstrap structs
// were — validate against real capture data before trusting field order
// blindly in a release build.
typedef ObScanStatus (*fn_obscan_context_create_session)(ObScanContextHandle context, ObScanSessionConfig* config, ObScanSessionHandle* out_session);
typedef ObScanStatus (*fn_obscan_session_destroy)(ObScanSessionHandle* session);
typedef ObScanStatus (*fn_obscan_session_create_scan)(ObScanSessionHandle session, ObScanConfig* config, ObScanScanHandle* out_scan);
// The real CrealityScan app always calls this before obscan_scan_start —
// confirmed by capturing its own obscan_* call log during a live scan
// (see live_capture_test.cpp for the full story). obscan_scan_start on a
// scan that was never previewed fails with OB_SCAN_ERROR_INVALID_ARGUMENT
// and the native log reason "cannot called start !": the laser/camera
// must already be actively streaming (a live preview) before the scan
// can transition into the recording state.
//
// IL2CPP's managed wrapper only exposes `Preview(Action<OBScanStatus>
// statusCallback)`, which looks like a single-callback signature — but
// OBScanTarget (the class that wraps this native call) separately defines
// static trampolines for BOTH a ProgressCallbackDelegate and a
// StatusCallbackDelegate ([MonoPInvokeCallback(typeof(...))]). Passing
// only one callback pointer here silently misaligns the real two-
// callback native signature: the garbage "status" values we saw (e.g.
// 10281832) were actually stale integer-register contents from calling
// our function as if it were the *progress* callback (a float, passed
// in an FP register under AAPCS64, read back as a bogus int), and
// passing NULL crashed when the SDK tried to invoke the still-missing
// second (real status) callback.
typedef ObScanStatus (*fn_obscan_scan_preview)(ObScanScanHandle scan, ObScanProgressCallback progress_callback,
                                                ObScanStatusCallback status_callback);
typedef ObScanStatus (*fn_obscan_scan_start)(ObScanScanHandle scan);
// OBScanTarget.PauseScan() takes NO parameters beyond the implicit
// handle — our earlier `is_stop_stream` bool was invented, not
// recovered from the real ABI. Likely harmless under AAPCS64 (an unused
// extra argument just sits in a register the real function never
// reads), but it was never real and is removed for accuracy.
typedef ObScanStatus (*fn_obscan_scan_pause)(ObScanScanHandle scan);
// CORRECTED (again): this is the real native shape after all —
// (scan, progress_cb, status_cb, is_block), the same two-callback
// pairing as obscan_scan_preview. OBScanTarget.StopScan's public C#
// signature (bool isBlock, Action<OBScanStatus> statusCallback) is NOT
// the P/Invoke signature — like Preview, this call has its own
// class-level progress trampoline that isn't exposed in the managed
// method's parameter list, and a *different* Claude session caught
// that this file's previous version (the 2-param shape below, briefly
// on main) was a regression: it returns OB_SCAN_SUCCESS immediately
// but the native log shows nothing after "obscan_scan_stop" — no stop
// progress, no pipeline teardown, callback never invoked — and both
// subsequent destroys are refused with OB_SCAN_ERROR_MULTI_CALLED
// ("must first wait other process finshed: destroy session !"),
// leaving the device unreleased. Verified directly against this
// project's own committed logs: with THIS (4-arg) shape, the native
// log shows real "stop progress 0.01 ... 1.0" / "change to stop
// success !" lines and the progress callback receives exactly those
// values (logs/native_scan_log/scan_log_20261015-215147.txt); with the
// old 2-param shape, "obscan_scan_stop" is immediately followed by
// "obscan_session_destroy" with nothing in between
// (logs/native_scan_log/scan_log_20261016-020420.txt). An
// OB_SCAN_ERROR_INVALID_SCAN from the status callback after a real
// "change to stop success !" is a verdict on the scan's content (e.g.
// a near-empty model), not a failed stop call. NULL callbacks are
// rejected, same as preview.
//
// `is_block=true` runs the SDK's whole stop tail synchronously on the
// calling thread — after the pipeline stop/destroy it applies the model's
// rotate pose, writes <output_dir>/result/model.ply (28 B/vertex) and
// serializes the out-of-core fusion cache (temp/*.obmc) — so the call blocks
// for all of it, and its duration scales with the session's data. A large
// session's teardown tail can outrun a fixed watchdog budget (row #37's run:
// ~14 s left of 420 s, cut off mid-tail); see
// docs/recon/findings/scan-live-data.md §16.
// typedef ObScanStatus (*fn_obscan_scan_stop)(ObScanScanHandle scan, bool is_block, ObScanStatusCallback status_callback);  // WRONG — kept here as a record of the dead end, do not resurrect.
typedef ObScanStatus (*fn_obscan_scan_stop)(ObScanScanHandle scan, ObScanProgressCallback progress_callback,
                                             ObScanStatusCallback status_callback, bool is_block);
// Returns the last *published* model cloud. The getter itself does not
// trigger the SDK's temp->show publish, so during an active scan it stays
// empty unless something else (the color-cloud getter) has published. See
// docs/recon/findings/scan-live-data.md §3.
typedef ObScanStatus (*fn_obscan_scan_get_raw_cloud)(ObScanScanHandle scan, ObScanPointClouds* out_cloud);
// The call that publishes the model cloud. The fusion pipeline writes into a
// temp cloud and sets a pending flag; this getter is the only caller of the
// temp->show swap, so calling it is what makes the published cloud (and
// therefore get_raw_cloud) advance — and it returns that cloud with
// per-point colors plus its bbox. Requires the scan step to be 1..9 (1 ready,
// 2 scanning, 3 paused, 4 stopped, 5/6 post-process, 9 mesh-adjacent; 0 and
// 10+ rejected) — otherwise OB_SCAN_ERROR_MULTI_CALLED. out_clouds/out_bbox
// are validated by the wrapper, out_count is not (pass a valid pointer).
// Buffers are borrowed pointers, same lifetime rules as get_raw_cloud; the
// release below is a no-op. See docs/recon/findings/scan-live-data.md §3-§5.
typedef ObScanStatus (*fn_obscan_scan_get_model_point_color_clouds)(ObScanScanHandle scan,
                                                                    ObScanPointColorClouds* out_clouds,
                                                                    ObScanCloudBbox* out_bbox,
                                                                    uint32_t* out_count);
// Kept for API symmetry: the native release slot is `ret` (nothing to free),
// so this returns 0 for any valid handle (845283591 for a NULL one — the
// shared precondition code). The cloud buffers are borrowed; there is no
// ownership to drop.
typedef ObScanStatus (*fn_obscan_scan_model_clouds_release)(ObScanScanHandle scan);
// Only valid while the scan is paused (step == 3) — during an active scan it
// fails with OB_SCAN_ERROR_MULTI_CALLED and the native log line "get model
// mesh must first pause scan !"; after stop it fails the same way. The call
// BUILDS the mesh into the SDK's cache on the scan object and returns borrowed
// pointers into it: the next call (which rebuilds/re-fills) or the release
// below (which frees the cache) invalidates them — copy out immediately. See
// docs/recon/findings/scan-live-data.md §14.
typedef ObScanStatus (*fn_obscan_scan_get_realtime_model_mesh)(ObScanScanHandle scan, ObScanTriangleMesh* out_mesh);
// Frees the cached mesh that the getter above built (native:
// OrbbecScanLaser::model_mesh_release — OBScanMeshDatas destructor + delete;
// clears the scan's cache pointer). Returns 0 for a valid handle, 845283591
// for a NULL one. Call once per consumed mesh (or before the next getter
// call); not a no-op, unlike the cloud release.
typedef ObScanStatus (*fn_obscan_scan_realtime_model_mesh_release)(ObScanScanHandle scan);
typedef ObScanStatus (*fn_obscan_scan_get_mode_config)(ObScanScanHandle scan, ObScanModeConfig* out_config);

// ---------------------------------------------------------------------------
// The config apply surface (row #55): post-creation setters. All take the scan
// handle and reject a NULL one with OB_SCAN_ERROR_DEADLINE_EXCEEDED
// (845283591); non-zero returns are refusals (findings §18). Exposure changes
// go through to the device live; the mode reconfig stops and re-starts the
// scan pipeline, so prefer it for FOV/light-type changes only.
// `obscan_scan_reconfig_scan_mode_config` is the read-modify-write pair to
// `obscan_scan_get_mode_config` (there is no dedicated set_fov).
typedef ObScanStatus (*fn_obscan_scan_config_ir_exposure_time)(ObScanScanHandle scan, int32_t value);
typedef ObScanStatus (*fn_obscan_scan_config_color_exposure_time)(ObScanScanHandle scan, int32_t value);
typedef ObScanStatus (*fn_obscan_scan_config_laser_current)(ObScanScanHandle scan, int32_t value);
typedef ObScanStatus (*fn_obscan_scan_config_depth_range)(ObScanScanHandle scan, int32_t min_value, int32_t max_value);
typedef ObScanStatus (*fn_obscan_scan_config_rgb_fill_light)(ObScanScanHandle scan, bool enabled);
typedef ObScanStatus (*fn_obscan_scan_config_show_data_rotate)(ObScanScanHandle scan, bool enabled);
typedef ObScanStatus (*fn_obscan_scan_config_light_type)(ObScanScanHandle scan, ObScanLightType light_type);
typedef ObScanStatus (*fn_obscan_scan_reconfig_scan_mode_config)(ObScanScanHandle scan, const ObScanModeConfig* config);
// ---------------------------------------------------------------------------
// The plane-removal surface (rows #58/#59; findings plane-removal.md). The
// three toggles are live-callable at any step (the wrappers check only the
// scan handle); their state lives on the data-management subobject at
// scan+0xf0 (enabled@+0x4f, the plane@+0x50). `obscan_scan_get_manaul_remove_plane`
// takes a different 0x18-byte struct — now declared below as
// ObScanManualRPPoints (row #70; findings §4).
typedef struct ObScanPlane {
    float a;            // 0x00
    float b;            // 0x04
    float c;            // 0x08
    float d;            // 0x0c — the plane equation a·x + b·y + c·z + d = 0
} ObScanPlane;          // 16 bytes

typedef struct ObScanManualRPConfig {
    int32_t state;      // 0x00 — read by the native (== 2 on one path)
    bool enabled;       // 0x04
    /* 0x05..0x07 pad */
    ObScanPlane plane;  // 0x08
} ObScanManualRPConfig; // 0x18 bytes

typedef ObScanStatus (*fn_obscan_scan_plane_remove)(ObScanScanHandle scan, bool enabled);
typedef ObScanStatus (*fn_obscan_scan_turntable_plane_remove)(ObScanScanHandle scan, bool enabled);
typedef ObScanStatus (*fn_obscan_scan_config_manaul_remove_plane)(ObScanScanHandle scan, const ObScanManualRPConfig* config);

// The manual-plane GETTER (row #70; findings plane-removal.md §4): its argument
// is the SDK's own OBScanManualRPConfig_ — the same 0x18 bytes as
// ObScanManualRPConfig above but a DIFFERENT shape: a {pointer, count} view of
// the manual plane's defining points (16-byte {x,y,z,pad} records, scaled
// x1000 when read). The two exports' structs are not interchangeable despite
// the shared size. The native fills *out_plane (an ObScanPlane, 16 bytes)
// derived from those points.
typedef struct ObScanManualRPPoints {
    uint32_t _reserved0;  // 0x00 — unread by the getter's native
    uint32_t _reserved1;  // 0x04
    const void* points;   // 0x08 — 16-byte {x,y,z,pad} records
    uint32_t count;       // 0x10
    uint32_t _reserved2;  // 0x14
} ObScanManualRPPoints;   // 0x18 bytes
typedef ObScanStatus (*fn_obscan_scan_get_manaul_remove_plane)(ObScanScanHandle scan, const ObScanManualRPPoints* config, ObScanPlane* out_plane);

// ---------------------------------------------------------------------------
// Frame and image payloads — the read side of the listener live path.
// `ObScanDataMessage.data` for NEW_{IR,DEPTH,RGB}_IMAGE is an image object and
// for FRAME_READY a frame object (findings §12); these are the C entry points
// for consuming them. Both kinds are BORROWED and ONE-SHOT PER ACCESSOR: each
// accessor consumes itself (calling the same accessor twice returns
// OB_SCAN_ERROR_MULTI_CALLED; after the SDK's release every accessor returns
// OB_SCAN_ERROR_INVALID_ARGUMENT), so read once, copy out, then call the
// matching release. Per-accessor state bits: findings §15. See
// docs/recon/findings/data-process-message-listeners.md §12/§14/§15.
typedef void* ObScanFrameHandle;   // the FRAME_READY payload pointer
typedef void* ObScanImageHandle;   // the NEW_*_IMAGE payload pointer
// The scanner pose — the scan pose getters' out (row #40) and the frame
// payload's pose. A caller-allocated 128-byte value: sixteen doubles,
// row-major 4x4 (m[r][c] at (4r+c)*8; the translation column is
// m[0][3]/m[1][3]/m[2][3]; bottom row [0,0,0,1] for an isometry). The scan
// getters COPY it (repeatable, no release); the frame getter returns a
// borrowed pointer (one-shot). See docs/recon/findings/scan-live-data.md §17.
typedef struct ObScanPose {
    double m[4][4];
} ObScanPose;                       // 128 bytes
// Pointer-to-pointer out whose layout is not yet recovered (findings §14);
// callers hold the returned pointer opaquely until then.
typedef struct ObScanImageData ObScanImageData;

// The direct-out structs (row #36). These are CALLER-ALLOCATED: put one on
// your stack, pass &out, and the SDK fills it in. Layouts from the listing;
// see docs/recon/findings/data-process-message-listeners.md §15.
// Marker records: also filled by the scan family's obscan_scan_get_markers.
// The three arrays are parallel: `markers` = 16-byte {x,y,z,pad} positions,
// `aux_0` = the same shape (normals), `aux_1` = one f32 (radius) per marker.
// See docs/recon/findings/scan-live-data.md §17.3.
typedef struct ObScanMarkers {
    uint32_t num_markers;   // 0x00
    const void* markers;    // 0x08  begin of the 16-byte marker records
    const void* aux_0;      // 0x10  per-marker array (element type unmapped)
    const void* aux_1;      // 0x18  per-marker array (element type unmapped)
} ObScanMarkers;            // 32 bytes

// A {data, count} view over a 4-byte-element advice array; each of the two
// frame advice getters fills one of these.
typedef struct ObScanSimpDistAdviceArray {
    const void* data;       // 0x00  4-byte-element advice entries
    uint32_t count;         // 0x08
    uint32_t _reserved;     // 0x0c
} ObScanSimpDistAdviceArray; // 16 bytes

// The reference-rods view: a count plus six pointer-sized fields sourced from
// the frame's rods data (their meanings are unmapped, findings §15).
typedef struct ObScanReferenceRods {
    uint32_t count;         // 0x00
    uint32_t _reserved;     // 0x04
    const void* p1;         // 0x08
    const void* p2;         // 0x10
    const void* p3;         // 0x18
    const void* p4;         // 0x20
    const void* p5;         // 0x28
    const void* p6;         // 0x30
} ObScanReferenceRods;      // 56 bytes

// The depth image's distance advice: the SDK's (int, int) pair plus a 320-bin
// u8 histogram (findings §15). The color image's getter is a stub returning
// OB_SCAN_ERROR_INVALID_ARGUMENT; only the depth image carries advice.
typedef struct ObScanDistanceAdviceInfo {
    int32_t pair_first;     // 0x00  first member of the SDK's (int, int) pair
    int32_t pair_second;    // 0x04  second member
    uint8_t histogram[320]; // 0x08
} ObScanDistanceAdviceInfo; // 328 bytes

// Compile-time ABI guards (row #36 follow-up): the SDK fills these structs by
// copying fixed-size blocks, so a size or offset drift is an ABI break, not a
// style issue. Sizes/offsets are the listing's (findings §15).
#if defined(__cplusplus)
#define OBSCAN_ABI_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define OBSCAN_ABI_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define OBSCAN_ABI_ASSERT(cond, msg) /* pre-C11: no compile-time check available */
#endif
OBSCAN_ABI_ASSERT(sizeof(ObScanMarkers) == 32, "ObScanMarkers must be 32 bytes (findings §15)");
OBSCAN_ABI_ASSERT(offsetof(ObScanMarkers, num_markers) == 0x00 &&
                  offsetof(ObScanMarkers, markers) == 0x08 &&
                  offsetof(ObScanMarkers, aux_0) == 0x10 &&
                  offsetof(ObScanMarkers, aux_1) == 0x18,
                  "ObScanMarkers offsets (findings §15)");
OBSCAN_ABI_ASSERT(sizeof(ObScanSimpDistAdviceArray) == 16, "ObScanSimpDistAdviceArray must be 16 bytes (findings §15)");
OBSCAN_ABI_ASSERT(offsetof(ObScanSimpDistAdviceArray, data) == 0x00 &&
                  offsetof(ObScanSimpDistAdviceArray, count) == 0x08,
                  "ObScanSimpDistAdviceArray offsets (findings §15)");
OBSCAN_ABI_ASSERT(sizeof(ObScanReferenceRods) == 56, "ObScanReferenceRods must be 56 bytes (findings §15)");
OBSCAN_ABI_ASSERT(offsetof(ObScanReferenceRods, p1) == 0x08 &&
                  offsetof(ObScanReferenceRods, p6) == 0x30,
                  "ObScanReferenceRods offsets (findings §15)");
OBSCAN_ABI_ASSERT(sizeof(ObScanDistanceAdviceInfo) == 328, "ObScanDistanceAdviceInfo must be 328 bytes (findings §15)");
OBSCAN_ABI_ASSERT(offsetof(ObScanDistanceAdviceInfo, pair_second) == 0x04 &&
                  offsetof(ObScanDistanceAdviceInfo, histogram) == 0x08,
                  "ObScanDistanceAdviceInfo offsets (findings §15)");
OBSCAN_ABI_ASSERT(sizeof(ObScanPlane) == 16, "ObScanPlane must be 16 bytes (findings plane-removal.md §4)");
OBSCAN_ABI_ASSERT(sizeof(ObScanManualRPPoints) == 0x18, "ObScanManualRPPoints must be 0x18 bytes (findings plane-removal.md §4)");
OBSCAN_ABI_ASSERT(offsetof(ObScanManualRPPoints, points) == 0x08 && offsetof(ObScanManualRPPoints, count) == 0x10,
                  "ObScanManualRPPoints offsets (findings plane-removal.md §4)");
OBSCAN_ABI_ASSERT(offsetof(ObScanManualRPConfig, plane) == 0x08, "ObScanManualRPConfig offsets (findings plane-removal.md §4)");
OBSCAN_ABI_ASSERT(sizeof(ObScanManualRPConfig) == 0x18, "ObScanManualRPConfig must be 0x18 bytes (findings plane-removal.md §4)");
OBSCAN_ABI_ASSERT(sizeof(ObScanPose) == 128, "ObScanPose must be 128 bytes (findings §17)");
OBSCAN_ABI_ASSERT(offsetof(ObScanPose, m[0][3]) == 0x18 &&
                  offsetof(ObScanPose, m[1][3]) == 0x38 &&
                  offsetof(ObScanPose, m[2][3]) == 0x58,
                  "ObScanPose translation column offsets (findings §17)");
#undef OBSCAN_ABI_ASSERT

// ---------------------------------------------------------------------------
// The scanner-POV channel (row #40): the pose and marker getters.
// Both pose getters are CALLER-ALLOCATED 128-byte copies (repeatable, no
// release). The markers getter fills a BORROWED 32-byte view (layout §15 in
// data-process-message-listeners.md — read synchronously, do not retain).
// NULL scan -> OB_SCAN_ERROR_DEADLINE_EXCEEDED (845283591); NULL out ->
// OB_SCAN_ERROR_INVALID_ARGUMENT (845283584). The global pose updates only
// when a registration result is confirmed; the registration pose is the live
// solve. See docs/recon/findings/scan-live-data.md §17.
typedef ObScanStatus (*fn_obscan_scan_get_global_pose)(ObScanScanHandle scan, ObScanPose* out_pose);
typedef ObScanStatus (*fn_obscan_scan_get_registration_pose)(ObScanScanHandle scan, ObScanPose* out_pose);

// The registration family's pose path (row #60; findings §17.2). The flow:
// create a registration from the session, run it (the runners are not declared
// here), then confirm — which publishes the scan's registration pose as the
// global pose — and destroy it. All three reject a NULL handle with
// OB_SCAN_ERROR_DEADLINE_EXCEEDED (845283591). For a live per-frame pose, the
// registration getter above is the channel; the global getter changes only on
// a confirmed result.
typedef void* ObScanRegistrationHandle;   // opaque; wraps the SDK's object
typedef ObScanStatus (*fn_obscan_session_create_registration)(ObScanSessionHandle session, ObScanRegistrationHandle* out_registration);
typedef ObScanStatus (*fn_obscan_registration_confirm_or_cancel_result)(ObScanRegistrationHandle registration, bool confirm);
typedef ObScanStatus (*fn_obscan_registration_destroy)(ObScanRegistrationHandle registration);
typedef ObScanStatus (*fn_obscan_scan_get_markers)(ObScanScanHandle scan, ObScanMarkers* out_markers);

// Fills *out_cloud with a pointer to the SDK's frame cloud — the same
// {num_points, points, normals} shape as ObScanPointClouds.
typedef ObScanStatus (*fn_obscan_frame_get_frame_cloud)(ObScanFrameHandle frame, ObScanPointClouds** out_cloud);
typedef ObScanStatus (*fn_obscan_frame_get_frame_bbox)(ObScanFrameHandle frame, ObScanCloudBbox* out_bbox);
typedef ObScanStatus (*fn_obscan_frame_get_timestamp)(ObScanFrameHandle frame, int64_t* out_timestamp);
typedef ObScanStatus (*fn_obscan_frame_get_frame_id)(ObScanFrameHandle frame, int32_t* out_frame_id);
typedef ObScanStatus (*fn_obscan_frame_get_frame_rate)(ObScanFrameHandle frame, int32_t* out_frame_rate);
// OBScan.SDK.OBScanTrackingStatus — the value returned by the getter below
// (row #78; findings scan-quality-gate.md §2). The mapping is the vendor's own:
// OBScanTrackingStatus_name (0x170d64) bounds-checks <= 6 and loads a 7-entry
// table (0x45455b0; descs 0x45455e8); out of range is "UNKNOWN".
typedef enum ObScanTrackingStatus {
    OB_SCAN_TRACKING_SUCCESS = 0,                        // "tracking success"
    OB_SCAN_TRACKING_LOST = 1,                           // "tracking lost"
    OB_SCAN_TRACKING_AMBIGUOUS_PLANE = 2,                // "ambiguous plane"
    OB_SCAN_TRACKING_FAILURE_INSUFF_VALID_POINTS = 3,    // "insufficient valid points"
    OB_SCAN_TRACKING_FAILURE_TEXTURELESS = 4,            // "textureless"
    OB_SCAN_TRACKING_FAILURE_MARKERLESS = 5,             // "markerless failure"
    OB_SCAN_TRACKING_FAILURE_MOVING_TOO_FAST = 6,        // "moving too fast"
} ObScanTrackingStatus;

typedef ObScanStatus (*fn_obscan_frame_get_frame_tracking_status)(ObScanFrameHandle frame, int32_t* out_status);
typedef ObScanStatus (*fn_obscan_frame_get_update_model)(ObScanFrameHandle frame, bool* out_update_model);
typedef ObScanStatus (*fn_obscan_frame_get_frame_pose)(ObScanFrameHandle frame, ObScanPose** out_pose);
// Releases the frame object (native `release_data`: clears its state and drops
// the impl). Returns 0 for a valid handle.
typedef ObScanStatus (*fn_obscan_frame_release)(ObScanFrameHandle frame);

typedef ObScanStatus (*fn_obscan_image_get_width)(ObScanImageHandle image, int32_t* out_width);
typedef ObScanStatus (*fn_obscan_image_get_height)(ObScanImageHandle image, int32_t* out_height);
typedef ObScanStatus (*fn_obscan_image_get_timestamp)(ObScanImageHandle image, int64_t* out_timestamp);
typedef ObScanStatus (*fn_obscan_image_get_display_image)(ObScanImageHandle image, ObScanImageData** out_image_data);
typedef ObScanStatus (*fn_obscan_image_get_depth_display_image)(ObScanImageHandle image, int32_t index, ObScanImageData** out_image_data);
// Releases the image object (native `release_data`). Returns 0 for a valid handle.
typedef ObScanStatus (*fn_obscan_image_release)(ObScanImageHandle image);

// The five direct-out getters (row #36): each fills a CALLER-ALLOCATED struct
// (layouts above). NULL out or handle -> OB_SCAN_ERROR_INVALID_ARGUMENT; the
// marker/advice/rods four do not validate the frame's impl.
typedef ObScanStatus (*fn_obscan_frame_get_marker_datas)(ObScanFrameHandle frame, ObScanMarkers* out_markers);
typedef ObScanStatus (*fn_obscan_frame_get_marker_distance_advice)(ObScanFrameHandle frame, ObScanSimpDistAdviceArray* out_advice);
typedef ObScanStatus (*fn_obscan_frame_get_frame_cloud_distance_advice)(ObScanFrameHandle frame, ObScanSimpDistAdviceArray* out_advice);
typedef ObScanStatus (*fn_obscan_frame_get_reference_rods)(ObScanFrameHandle frame, ObScanReferenceRods* out_rods);
typedef ObScanStatus (*fn_obscan_image_get_distance_advice)(ObScanImageHandle image, ObScanDistanceAdviceInfo* out_advice);

// OBScanTarget.ConfigCombinedExposureAndLaserCurrent(int value) — a
// single combined IR-exposure/laser-current slider, range [1, 1000]
// (OBScanRealTimeConfig.combined_exposure_and_laser_current; ground
// truth for this device's LaserFrameRenderParam: default ~500, last
// used 540). Confirmed necessary: physical markers for fuse_mode=Marker
// went undetected in the real CrealityScan app until exposure was
// turned up well above default.
typedef ObScanStatus (*fn_obscan_scan_config_combined_exposure_and_laser_current)(ObScanScanHandle scan,
                                                                                   int32_t value);

#ifdef __cplusplus
}  // extern "C"
#endif
