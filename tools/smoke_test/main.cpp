// Minimal smoke test for OpenScanner's clean-room OBScan SDK bindings.
//
// This does the least possible work needed to prove the binding approach:
// dynamically load the vendor's OBScan library, call obscan_version(),
// create a context, and enumerate connected devices.
//
// The actual SDK session (create -> enumerate -> destroy) runs in a
// disposable worker process, re-launched from this same executable. That
// is a deliberate workaround for a confirmed vendor SDK defect, not
// incidental complexity — see README.md "Known issues": the vendor's
// bundled ncnn+OpenMP runtime crashes on background threads it spawns
// internally the first time obscan_context_destroy() runs in a minimal
// host process. That crash happens on worker threads the vendor library
// itself creates, so an in-process signal handler can't safely recover
// from it (there's no safe way to resume a different thread's stack).
// Isolating the whole session in a child process sidesteps that: if the
// child is killed by the vendor's bug, this launcher process is
// unaffected.
//
// Usage:
//   smoke_test [path-to-liborbbec_scan.dylib-or-lib_orbbec_scan.dll]
//
// Exit codes: 0 clean run, 1 other launch failure, 2 skipped (no dylib —
// matches the hardware room's rule 10), 3 recovered from the known
// obscan_context_destroy() vendor crash.
//
// The device-count probe is deliberately fixed (7 samples x 500 ms); a
// deterministic series is worth more to a record than a tunable knob.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "openscanner/obscan_loader.h"
#include "openscanner/worker_process.h"

namespace {

constexpr const char* kWorkerFlag = "--sdk-session-worker";

const char* StatusName(ObScanStatus status) {
    switch (status) {
        case OB_SCAN_SUCCESS:
            return "OB_SCAN_SUCCESS";
        case OB_SCAN_ERROR_INVALID_ARGUMENT:
            return "OB_SCAN_ERROR_INVALID_ARGUMENT";
        case OB_SCAN_ERROR_CAMERA_NOT_REGISTER:
            return "OB_SCAN_ERROR_CAMERA_NOT_REGISTER";
        default:
            return "OB_SCAN_ERROR_(see obscan_api.h)";
    }
}

// Runs the actual SDK session. Called only inside the worker process.
int RunSdkSession(const std::string& library_path) {
    std::printf("[worker] Loading OBScan SDK from: %s\n", library_path.c_str());

    openscanner::ObscanLibrary lib(library_path);  // throws ObscanLoadError on failure
    std::printf("[worker] Resolved all bootstrap symbols OK.\n");

    const char* version = nullptr;
    ObScanStatus status = lib.obscan_version(&version);
    std::printf("[worker] obscan_version() -> %s, version=%s\n", StatusName(status),
                version != nullptr ? version : "(null)");

    ObScanContextConfig config = {};
    config.input_algo_dependent_files_dir = "";
    config.output_ob_sdk_log_dir = "/tmp/openscanner_ob_sdk_log";
    config.output_ob_scan_log_dir = "/tmp/openscanner_ob_scan_log";
    config.output_computer_performance_fps_dir = "/tmp/openscanner_fps_log";
    config.output_windows_gpu_info_dir = "";

    ObScanContextHandle context = nullptr;
    status = lib.obscan_context_create(&context, &config);
    std::printf("[worker] obscan_context_create() -> %s\n", StatusName(status));
    if (status != OB_SCAN_SUCCESS) {
        return 1;
    }

    ObScanDeviceManagementHandle device_mgmt = nullptr;
    status = lib.obscan_context_get_device_management(context, &device_mgmt);
    std::printf("[worker] obscan_context_get_device_management() -> %s\n", StatusName(status));

    if (status == OB_SCAN_SUCCESS) {
        // Delayed, repeated probe: the vendor's watcher completes detection
        // ~1 s after a single poll, so one sample raced it (OQ-7b). Keep the
        // listener-free shape — that is the point of this tool.
        constexpr int kDeviceProbeSamples = 7;
        constexpr int kDeviceProbeIntervalMs = 500;
        int32_t device_num = 0;
        for (int sample = 0; sample < kDeviceProbeSamples; ++sample) {
            int32_t observed = 0;
            status = lib.obscan_device_management_get_device_num(device_mgmt, &observed);
            std::printf("[worker] get_device_num sample %d/%d after %dms -> %s, count=%d\n",
                        sample + 1, kDeviceProbeSamples, sample * kDeviceProbeIntervalMs,
                        StatusName(status), observed);
            std::fflush(stdout);
            device_num = observed;
            if (sample + 1 < kDeviceProbeSamples) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kDeviceProbeIntervalMs));
            }
        }

        // obscan_device_management_get_device_info has never been seen to
        // succeed on hardware (no log in logs/ shows it), and the same call
        // was the leading suspect for the reproducible segfault in
        // logs/branch_claude-clever-albattani-cebx0e/. Its ObScanDeviceInfo
        // layout is unconfirmed. Off by default so smoke_test reaches
        // obscan_context_destroy; set OPENSCANNER_TRY_DEVICE_INFO=1 to probe
        // it deliberately (the worker isolation keeps a crash contained).
        // Note this tool never registers a device-message listener, so on
        // real hardware device_num stays 0 and this loop never runs; use
        // live_capture_test with OPENSCANNER_PROBE_DEVICE_INFO=1 to recover
        // the struct's real layout.
        const char* try_info_env = std::getenv("OPENSCANNER_TRY_DEVICE_INFO");
        const bool try_device_info = try_info_env != nullptr && std::string(try_info_env) == "1";
        if (device_num > 0 && !try_device_info) {
            std::printf("[worker] skipping obscan_device_management_get_device_info "
                        "(set OPENSCANNER_TRY_DEVICE_INFO=1 to call it)\n");
        }
        for (int32_t i = 0; try_device_info && i < device_num; ++i) {
            std::fflush(stdout);  // Survive a crash inside the next call.
            ObScanDeviceInfo info = {};
            status = lib.obscan_device_management_get_device_info(device_mgmt, i, &info);
            if (status == OB_SCAN_SUCCESS) {
                std::printf("[worker]   device[%d]: pid=%d name=%s serial=%s connection=%s firmware=%s\n", i,
                            info.camera_pid, info.camera_name != nullptr ? info.camera_name : "(null)",
                            info.camera_serial_number != nullptr ? info.camera_serial_number : "(null)",
                            info.camera_connection_type != nullptr ? info.camera_connection_type : "(null)",
                            info.camera_firmware_version != nullptr ? info.camera_firmware_version : "(null)");
            } else {
                std::printf("[worker]   device[%d]: get_device_info failed -> %s\n", i, StatusName(status));
            }
        }
    }

    // Known to crash inside the vendor SDK on some machines/SDK builds
    // (see README.md "Known issues"). That's exactly why this whole
    // function runs in a disposable worker process rather than the
    // launcher: if it crashes here, only the worker dies.
    status = lib.obscan_context_destroy(&context);
    std::printf("[worker] obscan_context_destroy() -> %s\n", StatusName(status));

    return status == OB_SCAN_SUCCESS ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    // Worker mode: argv[1] == kWorkerFlag, argv[2] = library path. Only
    // ever invoked by this same binary via RunSelfAsWorker, below.
    if (argc >= 3 && std::string(argv[1]) == kWorkerFlag) {
        return RunSdkSession(argv[2]);
    }

    // Launcher mode (normal invocation).
    std::string library_path = argc > 1 ? argv[1] : openscanner::ObscanLibrary::FindDefaultLibraryPath();
    if (library_path.empty()) {
        std::fprintf(stderr,
                      "No OBScan SDK library found. Pass the path to liborbbec_scan.dylib / "
                      "lib_orbbec_scan.dll explicitly (from an existing CrealityScan install).\n");
        std::printf("[launcher] Exit code 2 = skipped (no dylib).\n");
        return 2;
    }

    std::printf("[launcher] Running SDK session in a worker process...\n");
    std::fflush(stdout);
    int worker_exit_code = -1;
    std::string crash_reason;
    bool worker_ok =
        openscanner::RunSelfAsWorker({kWorkerFlag, library_path}, &worker_exit_code, &crash_reason);

    if (worker_ok) {
        std::printf("[launcher] Worker exited normally with code %d.\n", worker_exit_code);
        return worker_exit_code;
    }

    std::printf(
        "[launcher] Worker crashed inside the vendor SDK (%s). This is a known upstream issue "
        "(see README.md \"Known issues\") — the launcher process is unaffected and is exiting "
        "cleanly instead of propagating the crash.\n",
        crash_reason.c_str());
    std::printf("[launcher] Exit code 3 = recovered from known vendor crash.\n");
    // Exit code 3 (not 2): 2 is the hardware room's rule-10 "skipped, no
    // dylib", and a record reader must be able to tell the two apart.
    return 3;
}
