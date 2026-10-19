// Purpose-built hardware test for PLAN #8: exercises the PLAN #1
// device-message-listener paths that the existing tools never touch:
//   - NULL handle -> OB_SCAN_ERROR_DEADLINE_EXCEEDED (845283591)
//   - NULL listener -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)
//   - double add without remove -> OB_SCAN_ERROR_INVALID_ARGUMENT (845283584)
//   - remove then add (OQ-3) and the status codes it returns
//   - delivery thread identity (OQ-4)
//
// Usage:
//   device_message_listener_test [path-to-liborbbec_scan.dylib-or-lib_orbbec_scan.dll]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#include <dlfcn.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <setupapi.h>
#include <tlhelp32.h>
#include <cctype>
#endif

// (No CoreFoundation here: it existed only for the macOS interposer path. This
// port has no Windows use for it, so it is removed rather than left as dead
// macOS-only noise.)

#include "openscanner/obscan_api.h"
#include "openscanner/obscan_loader.h"
#include "openscanner/worker_process.h"

namespace {

constexpr const char* kWorkerFlag = "--device-msg-listener-worker";

std::atomic<int> g_msg_count{0};
int g_failures = 0;
std::atomic<std::thread::id> g_callback_thread{};
std::thread::id g_registration_thread{};
std::mutex g_cb_mutex;
std::vector<std::thread::id> g_cb_threads;

const char* DeviceMessageName(int32_t msg_id);

void OnDeviceMessage(ObScanDeviceMessage msg) {
    std::thread::id tid = std::this_thread::get_id();
    g_callback_thread.store(tid);
    {
        std::lock_guard<std::mutex> lk(g_cb_mutex);
        g_cb_threads.push_back(tid);
    }
    g_msg_count++;
    std::printf("[worker][cb] device message: %s (msg_id=%d) data=%p (thread=%lu)\n",
                DeviceMessageName(msg.msg_id), msg.msg_id, msg.data,
                static_cast<unsigned long>(std::hash<std::thread::id>{}(tid)));
    std::fflush(stdout);
}

const char* DeviceMessageName(int32_t msg_id) {
    switch (msg_id) {
        case OB_SCAN_MESSAGE_ID_DEVICE_CONNECT: return "DEVICE_CONNECT";
        case OB_SCAN_MESSAGE_ID_DEVICE_DISCONNECT: return "DEVICE_DISCONNECT";
        default: return "unknown";
    }
}

ObScanDeviceMessageListener g_listener_a{OnDeviceMessage};
ObScanDeviceMessageListener g_listener_b{OnDeviceMessage};

const char* StatusName(ObScanStatus status) {
    switch (status) {
        case OB_SCAN_SUCCESS: return "OB_SCAN_SUCCESS";
        case OB_SCAN_ERROR_INVALID_ARGUMENT: return "OB_SCAN_ERROR_INVALID_ARGUMENT";
        case OB_SCAN_ERROR_DEADLINE_EXCEEDED: return "OB_SCAN_ERROR_DEADLINE_EXCEEDED";
        default: return "OB_SCAN_ERROR_(see obscan_api.h)";
    }
}

void Check(const char* label, ObScanStatus actual, ObScanStatus expected) {
    if (actual == expected) {
        std::printf("[worker][PASS] %s\n", label);
    } else {
        g_failures++;
        std::printf("[worker][FAIL] %s (got %d, expected %d)\n", label, (int)actual, (int)expected);
    }
    std::fflush(stdout);
}

// Log directories: macOS writes under /tmp; Windows has no /tmp, so use %TEMP%.
std::string TempLogDir(const char* name) {
#if defined(_WIN32)
    const char* base = std::getenv("TEMP");
    std::string root = (base != nullptr && base[0] != '\0') ? std::string(base) : std::string(".");
    return root + "\\" + name;
#else
    return std::string("/tmp/") + name;
#endif
}

// Local typedef for the not-yet-header-declared remove entry point.
// The findings (device-message-listener.md §1) record this symbol as
// exported: obscan_device_management_remove_device_msg_listener.
typedef ObScanStatus (*fn_remove)(ObScanDeviceManagementHandle, ObScanDeviceMessageListener*);


// g_cb_threads is appended by the callback on the emitting (vendor) thread, so
// every read of its size must take the same g_cb_mutex. std::mutex is not
// recursive: ThreadHashList() takes the lock too, so call CbCount() from
// outside a locked region.
size_t CbCount() {
    std::lock_guard<std::mutex> lk(g_cb_mutex);
    return g_cb_threads.size();
}

std::string ThreadHashList(size_t from, size_t to) {
    std::lock_guard<std::mutex> lk(g_cb_mutex);
    std::string out;
    for (size_t i = from; i < to && i < g_cb_threads.size(); ++i) {
        if (!out.empty()) out += ",";
        out += std::to_string(static_cast<unsigned long>(std::hash<std::thread::id>{}(g_cb_threads[i])));
    }
    if (out.empty()) out = "(none)";
    return out;
}

#if defined(_WIN32)
// Preconditions the runbook names before a hardware run: the CrealityScan RPC
// daemon host is running, and a VID_2BC5 interface is present. A missing
// precondition is a SKIP (exit 2); a failure after both are true is a genuine
// init failure (exit 1). "Unknown" means the probe itself failed - never
// reported as "absent".
enum class Presence { Present, Absent, Unknown };

Presence QueryProcess(const wchar_t* image_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return Presence::Unknown;
    Presence result = Presence::Absent;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, image_name) == 0) {
                result = Presence::Present;
                break;
            }
        } while (Process32NextW(snap, &entry));
    } else {
        result = Presence::Unknown;
    }
    CloseHandle(snap);
    return result;
}

bool ContainsCaseInsensitive(const std::string& haystack, const char* needle) {
    std::string h = haystack;
    std::string n = needle;
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

Presence QueryVid2bc5Device() {
    HDEVINFO devices = SetupDiGetClassDevsA(nullptr, "USB", nullptr,
                                            DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devices == INVALID_HANDLE_VALUE) return Presence::Unknown;
    Presence result = Presence::Absent;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0;; ++i) {
        if (!SetupDiEnumDeviceInfo(devices, i, &info)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) result = Presence::Unknown;
            break;
        }
        DWORD type = 0;
        DWORD needed = 0;
        SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_HARDWAREID, &type,
                                          nullptr, 0, &needed);
        if (needed == 0) continue;
        std::vector<char> ids(needed + 2, 0);
        if (!SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_HARDWAREID, &type,
                                               reinterpret_cast<PBYTE>(ids.data()),
                                               needed, &needed)) {
            continue;
        }
        for (size_t off = 0; off < needed && ids[off] != '\0';) {
            std::string id(&ids[off]);
            if (ContainsCaseInsensitive(id, "VID_2BC5")) {
                result = Presence::Present;
                break;
            }
            off += id.size() + 1;
        }
        if (result == Presence::Present) break;
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}
#endif

int RunTests(const std::string& library_path) {
    std::printf("[worker] Loading OBScan SDK from: %s\n", library_path.c_str());
    openscanner::ObscanLibrary* libptr = nullptr;
    try {
        libptr = new openscanner::ObscanLibrary(library_path);
    } catch (const std::exception& e) {
        std::printf("[worker] FAILED to load SDK: %s\n", e.what());
#if defined(_WIN32)
        // Windows: the loader found the vendor DLL but could not load it (126 =
        // one of ITS dependencies is unresolved). That is an environment gap on
        // this platform, not a test failure - and the device path was never
        // reached. Return the documented SKIP code so ctest (SKIP_RETURN_CODE 2)
        // records "skipped", not "failed". macOS keeps 1: a present dylib that
        // fails dlopen is a real fault there.
        std::printf("[worker] SKIP: vendor SDK present but not loadable on Windows (dependency unresolved); device path NOT exercised.\n");
        std::fflush(stdout);
        return 2;
#else
        std::fflush(stdout);
        return 1;
#endif
    }
    openscanner::ObscanLibrary& lib = *libptr;
    std::printf("[worker] Resolved bootstrap symbols OK.\n");

#if defined(_WIN32)
    const Presence daemon = QueryProcess(L"CrealityScan.exe");
    const Presence device = QueryVid2bc5Device();
    std::printf("[worker] precondition: CrealityScan.exe (RPC daemon host): %s\n",
                daemon == Presence::Present ? "running" :
                daemon == Presence::Absent ? "NOT running" : "probe failed");
    std::printf("[worker] precondition: VID_2BC5 device: %s\n",
                device == Presence::Present ? "enumerated" :
                device == Presence::Absent ? "NOT enumerated" : "probe failed");
    std::fflush(stdout);
    if (daemon == Presence::Unknown || device == Presence::Unknown) {
        std::printf("[worker] SKIP: precondition probe failed (CrealityScan.exe: %s; VID_2BC5: %s); listener checks NOT exercised.\n",
                    daemon == Presence::Unknown ? "probe failed" : "ok",
                    device == Presence::Unknown ? "probe failed" : "ok");
        std::fflush(stdout);
        return 2;
    }
    if (daemon == Presence::Absent) {
        std::printf("[worker] SKIP: precondition not met: CrealityScan.exe (RPC daemon host) is not running; listener checks NOT exercised.\n");
        std::fflush(stdout);
        return 2;
    }
    if (device == Presence::Absent) {
        std::printf("[worker] SKIP: precondition not met: no VID_2BC5 device enumerated; listener checks NOT exercised.\n");
        std::fflush(stdout);
        return 2;
    }
#endif

    // Resolve the remove entry point opportunistically (not in the header).
    // macOS uses dlopen/dlsym. Windows has no dlsym, and the existing loader
    // (engine/src/obscan_loader.cpp) resolves only header-declared symbols - it
    // exposes no arbitrary-symbol resolver. So on Windows this symbol stays
    // unresolved and the OQ-3 remove/re-add checks are SKIPPED. That is a
    // platform gap, stated in the output, not silently equivalent.
    fn_remove remove_fn = nullptr;
#if !defined(_WIN32)
    void* native_handle = dlopen(library_path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (native_handle == nullptr) native_handle = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (native_handle != nullptr) remove_fn = reinterpret_cast<fn_remove>(dlsym(native_handle, "obscan_device_management_remove_device_msg_listener"));
#endif
#if defined(_WIN32)
    std::printf("[worker] remove entry point resolved: NO (Windows: the existing loader exposes no arbitrary-symbol resolver; OQ-3 remove/re-add checks SKIPPED - not equivalent to a Mac run)\n");
#else
    std::printf("[worker] remove entry point resolved: %s\n", remove_fn != nullptr ? "yes" : "NO (OQ-3 untestable)");
#endif

    std::string streaming_assets = openscanner::ObscanLibrary::FindStreamingAssetsDirForLibraryPath(library_path);
    std::string sdk_log_dir = TempLogDir("openscanner_ob_sdk_log");
    std::string scan_log_dir = TempLogDir("openscanner_ob_scan_log");
    std::string fps_log_dir = TempLogDir("openscanner_fps_log");
    ObScanContextConfig config = {};
    config.input_algo_dependent_files_dir = streaming_assets.c_str();
    config.output_ob_sdk_log_dir = sdk_log_dir.c_str();
    config.output_ob_scan_log_dir = scan_log_dir.c_str();
    config.output_computer_performance_fps_dir = fps_log_dir.c_str();
    config.output_windows_gpu_info_dir = "";

    ObScanContextHandle context = nullptr;
    ObScanStatus status = lib.obscan_context_create(&context, &config);
    std::printf("[worker] obscan_context_create() -> %s (%d)\n", StatusName(status), (int)status);
    if (status != OB_SCAN_SUCCESS) {
        std::printf("[worker] FAIL: preconditions met but obscan_context_create() failed; this is a genuine init failure.\n");
        std::fflush(stdout);
        return 1;
    }

    ObScanDeviceManagementHandle dm = nullptr;
    status = lib.obscan_context_get_device_management(context, &dm);
    std::printf("[worker] obscan_context_get_device_management() -> %s (%d)\n", StatusName(status), (int)status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // 1. NULL handle.
    status = lib.obscan_device_management_add_device_msg_listener(nullptr, &g_listener_a);
    std::printf("[worker] add(NULL handle, listener) -> %s (%d)\n", StatusName(status), (int)status);
    Check("add(NULL handle, listener) == DEADLINE_EXCEEDED", status, OB_SCAN_ERROR_DEADLINE_EXCEEDED);

    // 2. NULL listener.
    status = lib.obscan_device_management_add_device_msg_listener(dm, nullptr);
    std::printf("[worker] add(handle, NULL listener) -> %s (%d)\n", StatusName(status), (int)status);
    Check("add(handle, NULL listener) == INVALID_ARGUMENT", status, OB_SCAN_ERROR_INVALID_ARGUMENT);

    // 3. First valid registration.
    g_registration_thread = std::this_thread::get_id();
    g_msg_count = 0;
    g_callback_thread.store(std::thread::id{});
    status = lib.obscan_device_management_add_device_msg_listener(dm, &g_listener_a);
    std::printf("[worker] add(handle, listener_a) #1 -> %s (%d)\n", StatusName(status), (int)status);
    Check("add(handle, listener_a) #1 == SUCCESS", status, OB_SCAN_SUCCESS);
    size_t count_after_add1 = CbCount();
    std::printf("[worker]   messages delivered during add: %zu; callback thread id hashes=%s; registration thread id hash=%lu\n",
                count_after_add1,
                ThreadHashList(0, count_after_add1).c_str(),
                static_cast<unsigned long>(std::hash<std::thread::id>{}(g_registration_thread)));

    // 4. Double add without remove.
    status = lib.obscan_device_management_add_device_msg_listener(dm, &g_listener_b);
    std::printf("[worker] add(handle, listener_b) #2 (no remove) -> %s (%d)\n", StatusName(status), (int)status);
    Check("double add (no remove) == INVALID_ARGUMENT", status, OB_SCAN_ERROR_INVALID_ARGUMENT);

    std::printf("[worker] waiting 2s for any asynchronous device messages...\n");
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    size_t count_after_wait = CbCount();
    std::printf("[worker] total messages observed: %d\n", g_msg_count.load());
    std::printf("[worker]   messages after 2s wait: %zu; callback thread id hashes=%s\n",
                count_after_wait,
                ThreadHashList(0, count_after_wait).c_str());
    std::fflush(stdout);

    // 5. Remove then add (OQ-3).
    if (remove_fn != nullptr) {
        status = remove_fn(dm, &g_listener_a);
        std::printf("[worker] remove(handle, listener_a) -> %s (%d)\n", StatusName(status), (int)status);
        Check("remove(handle, listener_a) == SUCCESS", status, OB_SCAN_SUCCESS);

        // NULL handle for remove.
        status = remove_fn(nullptr, &g_listener_a);
        std::printf("[worker] remove(NULL handle, listener) -> %s (%d)\n", StatusName(status), (int)status);
        Check("remove(NULL handle, listener) == DEADLINE_EXCEEDED", status, OB_SCAN_ERROR_DEADLINE_EXCEEDED);

        size_t count_before_readd = CbCount();
        status = lib.obscan_device_management_add_device_msg_listener(dm, &g_listener_a);
        std::printf("[worker] add(handle, listener_a) after remove -> %s (%d)\n", StatusName(status), (int)status);
        Check("add after remove == SUCCESS (OQ-3 cleared slot)", status, OB_SCAN_SUCCESS);
        size_t count_after_readd = CbCount();
        std::printf("[worker]   messages delivered during re-add: %zu; callback thread id hashes=%s\n",
                    count_after_readd - count_before_readd,
                    ThreadHashList(count_before_readd, count_after_readd).c_str());

        // Clean up: remove again if registered.
        remove_fn(dm, &g_listener_a);
    } else {
        std::printf("[worker] skipping remove-then-add (remove symbol unavailable)\n");
    }

    // Teardown.
    status = lib.obscan_context_destroy(&context);
    std::printf("[worker] obscan_context_destroy() -> %s (%d)\n", StatusName(status), (int)status);

    std::printf("[worker] DONE (failures=%d)\n", g_failures);
    std::fflush(stdout);
    return g_failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    // Worker mode.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == kWorkerFlag) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing library path after %s\n", kWorkerFlag);
                return 2;
            }
            return RunTests(argv[i + 1]);
        }
    }

    // Launcher mode.
    std::string lib_path;
    if (argc >= 2) lib_path = argv[1];
    else lib_path = openscanner::ObscanLibrary::FindDefaultLibraryPath();
    if (lib_path.empty()) {
        std::fprintf(stderr, "No OBScan SDK library path given and no default found.\n");
        return 2;
    }

#if defined(__APPLE__) && defined(VULKAN_DISABLE_INTERPOSE_DYLIB)
    {
        size_t slash = lib_path.find_last_of('/');
        std::string moltenvk_path;
        if (slash != std::string::npos) moltenvk_path = lib_path.substr(0, slash + 1) + "libMoltenVK.dylib";
        if (!moltenvk_path.empty()) {
            std::string insert = moltenvk_path + ":" + VULKAN_DISABLE_INTERPOSE_DYLIB;
            setenv("DYLD_INSERT_LIBRARIES", insert.c_str(), 1);
            std::printf("[launcher] DYLD_INSERT_LIBRARIES=%s\n", insert.c_str());
            std::fflush(stdout); // banner must precede worker output when piped
        }
    }
#elif defined(_WIN32)
    // Say it out loud: Windows cannot reproduce the macOS interposer hand-off.
    // There is no DYLD_INSERT_LIBRARIES equivalent and no vulkan_disable_interpose
    // target on this platform. The run below is WITHOUT an interposer, so its
    // evidence is NOT equivalent to a Mac run on that axis.
    std::printf("[launcher] no interposer on Windows: DYLD_INSERT_LIBRARIES is macOS-only and this build has no vulkan_disable_interpose. The worker runs WITHOUT an interposer; do not record this as equivalent to a Mac run in that respect.\n");
    std::fflush(stdout);
#endif

    std::vector<std::string> args = {kWorkerFlag, lib_path};
    int exit_code = 0;
    std::string reason;
    bool ok = openscanner::RunSelfAsWorker(args, &exit_code, &reason, 180);
    if (!ok) {
        std::printf("worker did not exit cleanly: %s\n", reason.c_str());
        return 1;
    }
    std::printf("worker exit code: %d\n", exit_code);
    return exit_code;
}
