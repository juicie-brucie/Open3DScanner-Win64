#include "openscanner/obscan_loader.h"

#include <array>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace openscanner {

namespace {

#if defined(_WIN32)
void* OpenLibrary(const std::string& path) {
    return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
}
void* ResolveSymbol(void* handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
}
std::string LastErrorMessage() {
    return "LoadLibrary/GetProcAddress failed (GetLastError=" + std::to_string(GetLastError()) + ")";
}
#else
void* OpenLibrary(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* ResolveSymbol(void* handle, const char* name) {
    return dlsym(handle, name);
}
std::string LastErrorMessage() {
    const char* err = dlerror();
    return err != nullptr ? std::string(err) : std::string("unknown dlopen/dlsym error");
}
#endif

bool PathExists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f != nullptr) {
        std::fclose(f);
        return true;
    }
    return false;
}

}  // namespace

ObscanLibrary::ObscanLibrary(const std::string& library_path) {
    handle_ = OpenLibrary(library_path);
    if (handle_ == nullptr) {
        throw ObscanLoadError("Failed to load OBScan SDK library at '" + library_path + "': " + LastErrorMessage());
    }

    auto resolve = [this](const char* name) -> void* {
        void* sym = ResolveSymbol(handle_, name);
        if (sym == nullptr) {
            // Not dlclose()'d here either: loading it already ran the
            // vendor's static initializers, which may have started threads.
            throw ObscanLoadError(std::string("OBScan SDK library is missing expected symbol '") + name +
                                   "' (SDK version mismatch?)");
        }
        return sym;
    };

    obscan_version = reinterpret_cast<fn_obscan_version>(resolve("obscan_version"));
    obscan_context_create = reinterpret_cast<fn_obscan_context_create>(resolve("obscan_context_create"));
    obscan_context_destroy = reinterpret_cast<fn_obscan_context_destroy>(resolve("obscan_context_destroy"));
    obscan_context_get_device_management =
        reinterpret_cast<fn_obscan_context_get_device_management>(resolve("obscan_context_get_device_management"));
    obscan_context_add_data_msg_listener =
        reinterpret_cast<fn_obscan_context_add_data_msg_listener>(resolve("obscan_context_add_data_msg_listener"));
    obscan_context_add_process_msg_listener = reinterpret_cast<fn_obscan_context_add_process_msg_listener>(
        resolve("obscan_context_add_process_msg_listener"));
    obscan_context_remove_data_msg_listener = reinterpret_cast<fn_obscan_context_remove_data_msg_listener>(
        resolve("obscan_context_remove_data_msg_listener"));
    obscan_context_remove_process_msg_listener = reinterpret_cast<fn_obscan_context_remove_process_msg_listener>(
        resolve("obscan_context_remove_process_msg_listener"));
    obscan_device_management_get_device_num = reinterpret_cast<fn_obscan_device_management_get_device_num>(
        resolve("obscan_device_management_get_device_num"));
    obscan_device_management_get_device_info = reinterpret_cast<fn_obscan_device_management_get_device_info>(
        resolve("obscan_device_management_get_device_info"));
    obscan_device_management_get_active_idx = reinterpret_cast<fn_obscan_device_management_get_active_idx>(
        resolve("obscan_device_management_get_active_idx"));
    obscan_device_management_select_active_device =
        reinterpret_cast<fn_obscan_device_management_select_active_device>(
            resolve("obscan_device_management_select_active_device"));
    obscan_device_management_add_device_msg_listener =
        reinterpret_cast<fn_obscan_device_management_add_device_msg_listener>(
            resolve("obscan_device_management_add_device_msg_listener"));

    obscan_context_create_session =
        reinterpret_cast<fn_obscan_context_create_session>(resolve("obscan_context_create_session"));
    obscan_session_destroy = reinterpret_cast<fn_obscan_session_destroy>(resolve("obscan_session_destroy"));
    obscan_session_create_scan =
        reinterpret_cast<fn_obscan_session_create_scan>(resolve("obscan_session_create_scan"));
    obscan_scan_preview = reinterpret_cast<fn_obscan_scan_preview>(resolve("obscan_scan_preview"));
    obscan_scan_start = reinterpret_cast<fn_obscan_scan_start>(resolve("obscan_scan_start"));
    obscan_scan_pause = reinterpret_cast<fn_obscan_scan_pause>(resolve("obscan_scan_pause"));
    obscan_scan_stop = reinterpret_cast<fn_obscan_scan_stop>(resolve("obscan_scan_stop"));
    obscan_scan_get_raw_cloud =
        reinterpret_cast<fn_obscan_scan_get_raw_cloud>(resolve("obscan_scan_get_raw_cloud"));
    obscan_frame_get_frame_tracking_status = reinterpret_cast<fn_obscan_frame_get_frame_tracking_status>(
        resolve("obscan_frame_get_frame_tracking_status"));
    obscan_frame_release =
        reinterpret_cast<fn_obscan_frame_release>(resolve("obscan_frame_release"));
    obscan_scan_plane_remove =
        reinterpret_cast<fn_obscan_scan_plane_remove>(resolve("obscan_scan_plane_remove"));
    obscan_scan_turntable_plane_remove =
        reinterpret_cast<fn_obscan_scan_turntable_plane_remove>(resolve("obscan_scan_turntable_plane_remove"));
    obscan_scan_config_manaul_remove_plane = reinterpret_cast<fn_obscan_scan_config_manaul_remove_plane>(
        resolve("obscan_scan_config_manaul_remove_plane"));
    obscan_scan_get_global_pose =
        reinterpret_cast<fn_obscan_scan_get_global_pose>(resolve("obscan_scan_get_global_pose"));
    obscan_scan_get_registration_pose =
        reinterpret_cast<fn_obscan_scan_get_registration_pose>(resolve("obscan_scan_get_registration_pose"));
    obscan_scan_get_model_point_color_clouds =
        reinterpret_cast<fn_obscan_scan_get_model_point_color_clouds>(
            resolve("obscan_scan_get_model_point_color_clouds"));
    obscan_scan_model_clouds_release =
        reinterpret_cast<fn_obscan_scan_model_clouds_release>(resolve("obscan_scan_model_clouds_release"));
    obscan_scan_get_realtime_model_mesh = reinterpret_cast<fn_obscan_scan_get_realtime_model_mesh>(
        resolve("obscan_scan_get_realtime_model_mesh"));
    obscan_scan_realtime_model_mesh_release = reinterpret_cast<fn_obscan_scan_realtime_model_mesh_release>(
        resolve("obscan_scan_realtime_model_mesh_release"));
    obscan_scan_get_mode_config =
        reinterpret_cast<fn_obscan_scan_get_mode_config>(resolve("obscan_scan_get_mode_config"));
    obscan_scan_config_combined_exposure_and_laser_current =
        reinterpret_cast<fn_obscan_scan_config_combined_exposure_and_laser_current>(
            resolve("obscan_scan_config_combined_exposure_and_laser_current"));
}

// Deliberately never unloads the vendor library. It starts its own
// background threads (libusb hotplug + GVCP network discovery watchers,
// plus RPC clients) inside obscan_context_create per PLAN #7, and keeps
// the listener pointers it's given; some of those outlive
// obscan_context_destroy() and all of them are still running if a run
// bails out before teardown. Unloading the image under them means
// executing unmapped code — a crash at exit on top of whatever went wrong.
// The process is about to exit anyway; the OS reclaims the mapping then.
ObscanLibrary::~ObscanLibrary() = default;

std::string ObscanLibrary::FindDefaultLibraryPath() {
#if defined(_WIN32)
    const std::array<const char*, 2> candidates = {
        "C:\\Program Files\\CrealityScan\\CrealityScan_Data\\Plugins\\x86_64\\lib_orbbec_scan.dll",
        "C:\\Program Files (x86)\\CrealityScan\\CrealityScan_Data\\Plugins\\x86_64\\lib_orbbec_scan.dll",
    };
#elif defined(__APPLE__)
    const std::array<const char*, 1> candidates = {
        "/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib",
    };
#else
    const std::array<const char*, 0> candidates = {};
#endif
    for (const char* candidate : candidates) {
        if (PathExists(candidate)) {
            return candidate;
        }
    }
    return std::string();
}

std::string ObscanLibrary::FindStreamingAssetsDirForLibraryPath(const std::string& library_path) {
#if defined(_WIN32)
    const char* kPluginsSuffix = "_Data\\Plugins\\x86_64\\lib_orbbec_scan.dll";
    const char* kStreamingAssetsSuffix = "_Data\\StreamingAssets";
#else
    const char* kPluginsSuffix = "/Contents/PlugIns/liborbbec_scan.dylib";
    const char* kStreamingAssetsSuffix = "/Contents/Resources/Data/StreamingAssets";
#endif
    if (library_path.size() <= std::strlen(kPluginsSuffix) ||
        library_path.compare(library_path.size() - std::strlen(kPluginsSuffix), std::strlen(kPluginsSuffix),
                              kPluginsSuffix) != 0) {
        return std::string();
    }
    std::string app_root = library_path.substr(0, library_path.size() - std::strlen(kPluginsSuffix));
    std::string streaming_assets_dir = app_root + kStreamingAssetsSuffix;
    return PathExists(streaming_assets_dir) ? streaming_assets_dir : std::string();
}

}  // namespace openscanner
