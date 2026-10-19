#pragma once

#include <stdexcept>
#include <string>

#include "openscanner/obscan_api.h"

namespace openscanner {

// Thrown when the vendor library can't be found or a required symbol is
// missing from it (e.g. a newer/older SDK build than this header targets).
class ObscanLoadError : public std::runtime_error {
public:
    explicit ObscanLoadError(const std::string& message) : std::runtime_error(message) {}
};

// Dynamically loads Orbbec's OBScan scanning SDK (liborbbec_scan on macOS,
// lib_orbbec_scan.dll on Windows) and resolves the function pointers this
// project uses. The vendor binary is never bundled with or linked into
// this project at build time — the caller points this loader at an
// existing CrealityScan install (or an extracted copy) at runtime.
//
// Not copyable. The loaded module is intentionally never unloaded (see
// ~ObscanLibrary in obscan_loader.cpp for why).
class ObscanLibrary {
public:
    // Loads from an explicit path. Throws ObscanLoadError on failure.
    explicit ObscanLibrary(const std::string& library_path);
    ~ObscanLibrary();

    ObscanLibrary(const ObscanLibrary&) = delete;
    ObscanLibrary& operator=(const ObscanLibrary&) = delete;

    // Returns the first default install location for this OS that exists
    // on disk, or an empty string if none was found. Callers should fall
    // back to prompting the user for a path when this returns empty.
    static std::string FindDefaultLibraryPath();

    // Derives CrealityScan's Unity StreamingAssets directory from an
    // already-resolved library_path (as returned by FindDefaultLibraryPath
    // or passed in explicitly), or empty if it doesn't exist on disk.
    // obscan_context_create()'s ObScanContextConfig::input_algo_dependent_
    // files_dir must point here — leaving it empty causes the vendor SDK
    // to abort with an uncaught exception ("<file> the file is not
    // exists !") the first time it needs a GPU shader (dggv2_laser_*.
    // metallib) or ML model file (rot_model_core_1.bin, tex_model_core_1.
    // bin) that normally ships inside CrealityScan.app's own bundle.
    static std::string FindStreamingAssetsDirForLibraryPath(const std::string& library_path);

    fn_obscan_version obscan_version = nullptr;
    fn_obscan_context_create obscan_context_create = nullptr;
    fn_obscan_context_destroy obscan_context_destroy = nullptr;
    fn_obscan_context_get_device_management obscan_context_get_device_management = nullptr;
    fn_obscan_context_add_data_msg_listener obscan_context_add_data_msg_listener = nullptr;
    fn_obscan_context_add_process_msg_listener obscan_context_add_process_msg_listener = nullptr;
    fn_obscan_context_remove_data_msg_listener obscan_context_remove_data_msg_listener = nullptr;
    fn_obscan_context_remove_process_msg_listener obscan_context_remove_process_msg_listener = nullptr;
    fn_obscan_device_management_get_device_num obscan_device_management_get_device_num = nullptr;
    fn_obscan_device_management_get_device_info obscan_device_management_get_device_info = nullptr;
    fn_obscan_device_management_get_active_idx obscan_device_management_get_active_idx = nullptr;
    fn_obscan_device_management_select_active_device obscan_device_management_select_active_device = nullptr;
    fn_obscan_device_management_add_device_msg_listener obscan_device_management_add_device_msg_listener = nullptr;

    fn_obscan_context_create_session obscan_context_create_session = nullptr;
    fn_obscan_session_destroy obscan_session_destroy = nullptr;
    fn_obscan_session_create_scan obscan_session_create_scan = nullptr;
    fn_obscan_scan_preview obscan_scan_preview = nullptr;
    fn_obscan_scan_start obscan_scan_start = nullptr;
    fn_obscan_scan_pause obscan_scan_pause = nullptr;
    fn_obscan_scan_stop obscan_scan_stop = nullptr;
    fn_obscan_scan_get_raw_cloud obscan_scan_get_raw_cloud = nullptr;
    fn_obscan_frame_get_frame_tracking_status obscan_frame_get_frame_tracking_status = nullptr;
    fn_obscan_frame_release obscan_frame_release = nullptr;
    fn_obscan_scan_plane_remove obscan_scan_plane_remove = nullptr;
    fn_obscan_scan_turntable_plane_remove obscan_scan_turntable_plane_remove = nullptr;
    fn_obscan_scan_config_manaul_remove_plane obscan_scan_config_manaul_remove_plane = nullptr;
    fn_obscan_scan_get_global_pose obscan_scan_get_global_pose = nullptr;
    fn_obscan_scan_get_registration_pose obscan_scan_get_registration_pose = nullptr;
    fn_obscan_scan_get_model_point_color_clouds obscan_scan_get_model_point_color_clouds = nullptr;
    fn_obscan_scan_model_clouds_release obscan_scan_model_clouds_release = nullptr;
    fn_obscan_scan_get_realtime_model_mesh obscan_scan_get_realtime_model_mesh = nullptr;
    fn_obscan_scan_realtime_model_mesh_release obscan_scan_realtime_model_mesh_release = nullptr;
    fn_obscan_scan_get_mode_config obscan_scan_get_mode_config = nullptr;
    fn_obscan_scan_config_combined_exposure_and_laser_current obscan_scan_config_combined_exposure_and_laser_current =
        nullptr;

private:
    void* handle_ = nullptr;
};

}  // namespace openscanner
