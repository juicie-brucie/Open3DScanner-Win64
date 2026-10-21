// MSVC defines M_PI only when _USE_MATH_DEFINES is set before <math.h> is first
// included; define it above every include so no transitive pull can beat it.
#define _USE_MATH_DEFINES

// Streams point-cloud frames over a local WebSocket so the web UI
// (ui/index.html) can render them. Three modes:
//
//   scan_server [port]   (or: scan_server --synthetic [port])
//       Synthetic, animated sphere — exists so the UI's render/latency
//       pipeline can be built and measured without a physical scanner.
//
//   Per-tick latency instrumentation (row #111; measurement only) is on by
//   default and prints a `[perf] window` line every 5 s plus a summary at
//   teardown. Set OPENSCANNER_TICK_PERF=0 to disable it.
//
//   scan_server --live [port] [path] [seconds] [--record <file>]
//                                 [--frame-rate auto|standard|balanced|fast]
//                                 [--slow-start]
//       Drives a real OBScan session; --record writes V1 frames to <file>.
//       --frame-rate selects ObScanModeConfig.frame_rate (default auto, the
//       pre-existing behavior); --slow-start waits 5 s before obscan_scan_start
//       (default off). Both are fork-only experimental isolation levers.
//       Plane removal (findings plane-removal.md §3), env-driven:
//         OPENSCANNER_PLANE_REMOVE=1            auto plane removal on
//         OPENSCANNER_TURNTABLE_PLANE_REMOVE=1  turntable plane removal on
//         OPENSCANNER_MANUAL_PLANE=a,b,c,d      manual plane for the SDK
//       Our own removal decision (row #77), independent of the toggles:
//         OPENSCANNER_OUR_PLANE_REMOVE=1        fit a plane, cut below it
//         OPENSCANNER_PLANE_MARGIN=<float>      keep signed distance >= margin
//                                               (default 1% of cloud radius)
//
//   scan_server --replay <file> [port]
//       Replays a recorded session (V1 frames) without a device.
//
//   scan_server --live [port] [path-to-liborbbec_scan.dylib-or-dll]
//       Drives a real OBScan session against a physically-connected
//       scanner (same context/session/scan lifecycle as
//       tools/live_capture_test) and broadcasts whatever it gets back.
//
//       Live frames now come from obscan_scan_get_model_point_color_clouds,
//       which publishes the model cloud while scanning, so they can carry
//       points during the scan; the per-tick payload is decimated to a
//       fixed budget so the UI stays live at high accumulated point counts.
//       The final pause -> get_realtime_model_mesh snapshot is still
//       broadcast before teardown, so the UI shows full captured geometry
//       at least once per session.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <fstream>
#include <iterator>
#include <limits.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#endif

#include "openscanner/capture_source.h"
#include "openscanner/frame_protocol.h"
#include "openscanner/obscan_loader.h"
#include "openscanner/ws_broadcast_server.h"
#include "openscanner/worker_process.h"
#include "tracking_status.h"  // the vendor's per-frame verdict, by pinned name (row #91)
#include "bounded_watchdog.h"  // the per-call bound that survives a stuck caller (row #89)
#include "perf_window.h"       // per-tick latency instrumentation (row #111)

namespace {

namespace perf = openscanner::perf;  // row #111 tick instrumentation

// Quit-path instrumentation (row #79): append-only trace shared with the
// app. A raw write to a pre-opened fd (no stdout FILE, no allocator) so a
// diagnostic cannot be swallowed by a lock the hung vendor teardown thread is
// holding. Windows uses the _open/_write CRT equivalents.
#if defined(_WIN32)
#define OSC_OPEN _open
#define OSC_WRITE _write
#define OSC_GETPID _getpid
#define OSC_OPEN_FLAGS (_O_WRONLY | _O_CREAT | _O_APPEND)
#else
#define OSC_OPEN open
#define OSC_WRITE write
#define OSC_GETPID getpid
#define OSC_OPEN_FLAGS (O_WRONLY | O_CREAT | O_APPEND)
#endif

int g_quit_log_fd = -1;

void QuitTimestamp(long* sec, int* usec) {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    const unsigned long long us = (u.QuadPart - 116444736000000000ULL) / 10ULL;
    *sec = static_cast<long>(us / 1000000ULL);
    *usec = static_cast<int>(us % 1000000ULL);
#else
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    *sec = static_cast<long>(tv.tv_sec);
    *usec = static_cast<int>(tv.tv_usec);
#endif
}

void QuitTraceInit() {
    if (g_quit_log_fd >= 0) return;
    const char* path = std::getenv("OPENSCANNER_QUIT_LOG");
#if defined(_WIN32)
    std::string win_default;
    if (path == nullptr || path[0] == '\0') {
        const char* temp = std::getenv("TEMP");
        win_default = (temp != nullptr && temp[0] != '\0') ? std::string(temp) + "\\openscanner-quit.log"
                                                           : std::string("openscanner-quit.log");
        path = win_default.c_str();
    }
#else
    if (path == nullptr || path[0] == '\0') path = "/tmp/openscanner-quit.log";
#endif
    g_quit_log_fd = OSC_OPEN(path, OSC_OPEN_FLAGS, 0644);
}
void QuitTrace(const char* msg) {
    if (g_quit_log_fd < 0) return;
    long sec = 0;
    int usec = 0;
    QuitTimestamp(&sec, &usec);
    char buf[256];
    int n = std::snprintf(buf, sizeof(buf), "%ld.%06d [worker pid %d] %s\n", sec, usec,
                          static_cast<int>(OSC_GETPID()), msg);
    if (n > 0) {
        long ignored = static_cast<long>(OSC_WRITE(g_quit_log_fd, buf, static_cast<unsigned int>(n)));
        (void)ignored;
    }
}

// The formatted sibling of QuitTrace. Same raw-write path — no stdout FILE, no
// allocator — so a diagnostic printed here cannot be swallowed by a lock the
// hung vendor teardown thread is holding (row #89). Keep messages short.
void QuitTracef(const char* fmt, ...) {
    if (g_quit_log_fd < 0) return;
    long sec = 0;
    int usec = 0;
    QuitTimestamp(&sec, &usec);
    char buf[256];
    int n = std::snprintf(buf, sizeof(buf), "%ld.%06d [worker pid %d] ", sec, usec,
                          static_cast<int>(OSC_GETPID()));
    if (n < 0 || n >= static_cast<int>(sizeof(buf))) return;
    va_list ap;
    va_start(ap, fmt);
    int m = std::vsnprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), fmt, ap);
    va_end(ap);
    if (m < 0) return;
    size_t used = static_cast<size_t>(n) + std::min(static_cast<size_t>(m), sizeof(buf) - static_cast<size_t>(n) - 1);
    if (used >= sizeof(buf) - 1) used = sizeof(buf) - 2;  // make room for the newline
    buf[used++] = '\n';
    long ignored = static_cast<long>(OSC_WRITE(g_quit_log_fd, buf, static_cast<unsigned int>(used)));
    (void)ignored;
}

// Our own removal decision (row #77): fit a dominant plane from the live
// cloud (deterministic RANSAC) and cut points below a margin. This is the
// decision layer only — it runs on whatever cloud the vendor path published,
// so it composes with (or replaces) the #71 toggle-bound vendor removal.
std::atomic<uint64_t> g_our_plane_cut{0};
std::atomic<uint64_t> g_our_plane_kept{0};
std::atomic<uint64_t> g_our_plane_fits{0};

double view_radius_guess(const ObScanPoint* pts, uint32_t n) {
    if (n == 0) return 0.0;
    double sx = 0, sy = 0, sz = 0;
    for (uint32_t i = 0; i < n; ++i) { sx += pts[i].x; sy += pts[i].y; sz += pts[i].z; }
    double cx = sx / n, cy = sy / n, cz = sz / n;
    double sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
        double dx = pts[i].x - cx, dy = pts[i].y - cy, dz = pts[i].z - cz;
        sum += dx * dx + dy * dy + dz * dz;
    }
    return std::sqrt(sum / n);
}

struct OurPlane {
    double a = 0, b = 0, c = 0, d = 0;
    bool valid = false;
    uint32_t inliers = 0;
};

// Canonicalize the normal's sign so "below the plane" has one meaning per
// run: the positive side is the scanner (or +z when no pose is available).
OurPlane FitDominantPlane(const ObScanPoint* pts, uint32_t n, int iterations, double inlier_dist, double ref_x,
                          double ref_y, double ref_z, bool have_ref) {
    OurPlane best;
    if (n < 3) return best;
    uint64_t seed = 0x9e3779b97f4a7c15ULL;
    auto rnd = [&seed](uint32_t m) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>((seed >> 33) % m);
    };
    for (int it = 0; it < iterations; ++it) {
        uint32_t i = rnd(n), j = rnd(n), k = rnd(n);
        if (i == j || j == k || i == k) continue;
        const ObScanPoint& p0 = pts[i];
        const ObScanPoint& p1 = pts[j];
        const ObScanPoint& p2 = pts[k];
        double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
        double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
        double a = uy * vz - uz * vy;
        double b = uz * vx - ux * vz;
        double c = ux * vy - uy * vx;
        double norm = std::sqrt(a * a + b * b + c * c);
        if (norm < 1e-9) continue;
        a /= norm; b /= norm; c /= norm;
        double d = -(a * p0.x + b * p0.y + c * p0.z);
        uint32_t inliers = 0;
        for (uint32_t t = 0; t < n; ++t) {
            double dist = std::fabs(a * pts[t].x + b * pts[t].y + c * pts[t].z + d);
            if (dist <= inlier_dist) ++inliers;
        }
        if (inliers > best.inliers) {
            best = {a, b, c, d, true, inliers};
        }
    }
    if (best.valid) {
        double side = have_ref ? (best.a * ref_x + best.b * ref_y + best.c * ref_z + best.d) : best.c;
        if (side < 0) {
            best.a = -best.a;
            best.b = -best.b;
            best.c = -best.c;
            best.d = -best.d;
        }
    }
    return best;
}

// Directory containing the running binary (with trailing separator), or "".
std::string ExecutableDir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return "";
    std::string p(buf);
    size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    char resolved[PATH_MAX];
    if (realpath(buf, resolved) == nullptr) return "";
    std::string p(resolved);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string p(buf);
#endif
#if !defined(_WIN32)
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
#endif
}

// UI directory served on the same port as the WebSocket. Resolved relative
// to the running binary first (a bundled .app's cwd is "/"), then the cwd
// for dev-tree runs. Returns "" if none is found (caller warns loudly).
std::string FindUiDir() {
    std::vector<std::string> candidates;
    std::string exe = ExecutableDir();
    if (!exe.empty()) {
        candidates.push_back(exe + "ui");                    // exe dir has ui/
        candidates.push_back(exe + "../Resources/ui");       // .app: MacOS -> Resources/ui
        candidates.push_back(exe + "../../../ui");           // build/tools/scan_server -> repo/ui
    }
    candidates.push_back("ui");
    candidates.push_back("../ui");
    candidates.push_back("../../ui");
    for (const std::string& c : candidates) {
        std::string index = c + "/index.html";
        if (FILE* f = std::fopen(index.c_str(), "rb")) {
            std::fclose(f);
            return c;
        }
    }
    return "";
}

// Loud at startup rather than a silent 404 per request: a missing ui/
// means the page cannot load, and the app's retry sees 404 as success.
void ConfigureStatic(openscanner::WsBroadcastServer& server) {
    std::string ui = FindUiDir();
    if (ui.empty()) {
        std::fprintf(stderr,
                     "[worker] WARNING: ui/ directory not found (checked next to the binary and cwd); "
                     "static serving disabled — the page will 404.\n");
    } else {
        std::printf("[worker] Serving UI from: %s\n", ui.c_str());
    }
    server.SetStaticRoot(ui);
}

std::string g_record_path;
std::atomic<bool> g_context_destroy_timed_out{false};
// Exit code for the context_destroy deadline, distinct from the room's
// rule-10 "skipped, no dylib" exit 2 and from the worker's "no device" 2.
constexpr int kExitContextDestroyTimeout = 4;
// Row #89: the per-call bound around obscan_context_destroy(). It is a file
// scope constant because a watchdog thread must enforce it independently of
// the thread that made the call.
constexpr int kContextDestroyCallTimeoutMs = 5000;
// The watchdog's grace past the bound: a little slack so the caller's own
// timeout path (which disarms the watchdog) wins the race and exits cleanly.
constexpr int kContextDestroyWatchdogGraceMs = 500;
uint64_t g_recorded_frame_count = 0;
constexpr int kPointGridSize = 48;        // kPointGridSize^2 points on a sphere.
constexpr double kFrameIntervalMs = 1000.0 / 30.0;  // ~30 Hz.

struct Vec3 {
    float x, y, z;
};

std::vector<Vec3> MakeSpherePoints(int grid_size) {
    std::vector<Vec3> points;
    points.reserve(static_cast<size_t>(grid_size) * grid_size);
    for (int i = 0; i < grid_size; ++i) {
        double theta = M_PI * (static_cast<double>(i) + 0.5) / grid_size;  // 0..pi
        for (int j = 0; j < grid_size; ++j) {
            double phi = 2.0 * M_PI * static_cast<double>(j) / grid_size;  // 0..2pi
            float x = static_cast<float>(std::sin(theta) * std::cos(phi));
            float y = static_cast<float>(std::cos(theta));
            float z = static_cast<float>(std::sin(theta) * std::sin(phi));
            points.push_back({x, y, z});
        }
    }
    return points;
}

void RotateY(std::vector<Vec3>* points, double angle_rad) {
    float c = static_cast<float>(std::cos(angle_rad));
    float s = static_cast<float>(std::sin(angle_rad));
    for (auto& p : *points) {
        float x = p.x * c + p.z * s;
        float z = -p.x * s + p.z * c;
        p.x = x;
        p.z = z;
    }
}

double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(system_clock::now().time_since_epoch()).count();
}

void AppendU32(std::vector<uint8_t>* buf, uint32_t v) {
    buf->push_back(static_cast<uint8_t>(v & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf->push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AppendF64(std::vector<uint8_t>* buf, double v) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 8 bytes");
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        buf->push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
}

void AppendF32(std::vector<uint8_t>* buf, float v) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(v), "float must be 4 bytes");
    std::memcpy(&bits, &v, sizeof(bits));
    AppendU32(buf, bits);
}

// --- Control channel (UI -> server) and status channel (server -> UI) -----
// Text frames over the same WebSocket, independent of the binary
// point-cloud frames above. Hand-parsed rather than pulling in a JSON
// library, matching the rest of this file's "implement the small fixed
// shape we actually need" style (see ws_broadcast_server.cpp's SHA-1/
// Base64 for the same reasoning). Only flat {"key": value, ...} objects
// with string/number/bool leaves are supported — nothing here ever nests.

// Finds "key": <value> in `json` and returns the raw, unparsed value text
// (quotes kept on if it was a string). false if the key isn't present.
bool FindJsonValue(const std::string& json, const std::string& key, std::string* out_value) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    size_t end = pos;
    if (end < json.size() && json[end] == '"') {
        end = json.find('"', end + 1);
        if (end == std::string::npos) return false;
        *out_value = json.substr(pos + 1, end - pos - 1);
        return true;
    }
    while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
    *out_value = json.substr(pos, end - pos);
    // Trim trailing whitespace left over from the scan above.
    while (!out_value->empty() && std::isspace(static_cast<unsigned char>(out_value->back()))) {
        out_value->pop_back();
    }
    return true;
}

bool FindJsonBool(const std::string& json, const std::string& key, bool* out_value) {
    std::string raw;
    if (!FindJsonValue(json, key, &raw)) return false;
    if (raw == "true") {
        *out_value = true;
        return true;
    }
    if (raw == "false") {
        *out_value = false;
        return true;
    }
    return false;
}

bool FindJsonInt(const std::string& json, const std::string& key, int* out_value) {
    std::string raw;
    if (!FindJsonValue(json, key, &raw)) return false;
    if (raw.empty()) return false;
    *out_value = std::atoi(raw.c_str());
    return true;
}

// Combined exposure/laser-current range per obscan_api.h
// (fn_obscan_scan_config_combined_exposure_and_laser_current); reused as
// the UI's manual exposure/brightness slider range for both modes so one
// value means the same thing everywhere.
constexpr int kExposureMin = 1;
constexpr int kExposureMax = 1000;
constexpr int kDefaultExposure = 900;  // Matches the value this file already drove live scans with.

// Last per-frame tracking status (findings scan-quality-gate.md §2), surfaced
// in the status JSON so a consumer can show the vendor's per-pose verdict.
// The value is the SDK's own — see findings tracking-mechanism.md §1 for what
// it carries. -1 means no FRAME_READY sample has been seen yet.
std::atomic<int> g_last_tracking_status{-1};
// The per-reason tally across the poses seen so far, surfaced alongside the
// last status so the UI can show the accumulated rejection reasons (row #91).
// One slot per declared ObScanTrackingStatus value (index = status value).
std::atomic<uint64_t> g_tracking_samples{0};
std::atomic<uint64_t> g_tracking_hist[16];

// Shared between the WS reader thread(s) (writer, via HandleControlMessage)
// and whichever thread owns the scan loop (reader, via its tick) — hence
// all-atomic, no locking. Mirrors the synthetic/live scan lifecycle: a
// scan is idle, scanning, or paused; exposure is either auto or a held
// manual value; FOV is a two-position switch on the real scanner (no
// continuous zoom exists in the hardware API — see obscan_api.h's
// ObScanFov) applied at scan creation, so for live mode this field is
// informational only once scanning has started (see RunLiveServer).
struct ControlState {
    std::atomic<bool> scanning{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> finish_requested{false};
    std::atomic<bool> auto_exposure{true};
    std::atomic<int> exposure_value{kDefaultExposure};
    std::atomic<bool> fov_wide{false};
};

// Applies one control command to `state`. Safe to call from any thread;
// callers that also need the command to affect real hardware (RunLiveServer)
// check the same atomics from their own loop rather than acting here, so
// nothing in this function ever touches the vendor SDK directly.
void HandleControlMessage(const std::string& json, ControlState* state) {
    std::string cmd;
    if (!FindJsonValue(json, "cmd", &cmd)) return;
    if (cmd == "start") {
        state->scanning = true;
        state->paused = false;
    } else if (cmd == "pause") {
        state->paused = true;
    } else if (cmd == "finish") {
        state->scanning = false;
        state->paused = false;
        state->finish_requested = true;
    } else if (cmd == "set_exposure") {
        bool is_auto = false;
        if (FindJsonBool(json, "auto", &is_auto) && is_auto) {
            state->auto_exposure = true;
        } else {
            int value = kDefaultExposure;
            if (FindJsonInt(json, "value", &value)) {
                value = std::max(kExposureMin, std::min(kExposureMax, value));
                state->exposure_value = value;
            }
            state->auto_exposure = false;
        }
    } else if (cmd == "set_fov") {
        std::string value;
        if (FindJsonValue(json, "value", &value)) {
            state->fov_wide = (value == "wide");
        }
    }
}

// mode is "synthetic" or "live"; fov_controllable is false once a live
// scan has actually started, since the real SDK has no call to change FOV
// mid-scan (see ControlState's comment above) — the UI uses it to decide
// whether to let the operator touch that control at all.
std::string BuildStatusJson(const char* mode, const ControlState& state, bool fov_controllable) {
    const char* scan_state = !state.scanning.load() ? "idle" : (state.paused.load() ? "paused" : "scanning");
    std::string json = "{\"type\":\"status\",\"mode\":\"";
    json += mode;
    json += "\",\"state\":\"";
    json += scan_state;
    json += "\",\"autoExposure\":";
    json += state.auto_exposure.load() ? "true" : "false";
    json += ",\"exposure\":";
    json += std::to_string(state.exposure_value.load());
    json += ",\"exposureMin\":";
    json += std::to_string(kExposureMin);
    json += ",\"exposureMax\":";
    json += std::to_string(kExposureMax);
    json += ",\"fov\":\"";
    json += state.fov_wide.load() ? "wide" : "normal";
    json += "\",\"fovControllable\":";
    json += fov_controllable ? "true" : "false";
    json += openscanner::tracking::TrackingStatusJsonTail(g_last_tracking_status.load());
    uint64_t tracking_counts[openscanner::tracking::kTrackingStatusCount];
    for (int32_t i = 0; i < openscanner::tracking::kTrackingStatusCount; ++i) {
        tracking_counts[i] = g_tracking_hist[i].load();
    }
    json += openscanner::tracking::TrackingCountsJsonTail(tracking_counts,
                                                          openscanner::tracking::kTrackingStatusCount);
    json += "}";
    return json;
}

// Wire format shared with ui/app.js: u32 frame_id, f64 capture_timestamp_ms,
// u32 point_count, then point_count * (f32 x, f32 y, f32 z).
std::vector<uint8_t> BuildFrame(uint32_t frame_id, double capture_timestamp_ms, const float* xyz,
                                 uint32_t point_count) {
    return openscanner::frame_protocol::encode_cloud_snapshot(frame_id, capture_timestamp_ms, NowMs(),
                                                             xyz == nullptr ? nullptr : xyz, point_count);
}

static void RecordFrame(const std::vector<uint8_t>& frame) {
    if (g_record_path.empty()) return;
    static std::ofstream out;
    static bool header_written = false;
    static size_t recorded_bytes = 0;
    constexpr size_t kMaxRecordBytes = 1ULL << 30;
    if (!out.is_open()) {
        out.open(g_record_path, std::ios::binary | std::ios::trunc);
        if (!out) { std::fprintf(stderr, "[worker] cannot open record file: %s\n", g_record_path.c_str()); g_record_path.clear(); return; }
    }
    if (!header_written) {
        const char magic[4] = {'O','S','C','R'};
        out.write(magic, 4);
        uint32_t version = 1;
        out.write(reinterpret_cast<const char*>(&version), 4);
        uint64_t count = 0; // frame count patched at teardown
        out.write(reinterpret_cast<const char*>(&count), 8);
        if (!out) { std::fprintf(stderr, "[worker] record header write failed\n"); g_record_path.clear(); return; }
        header_written = true;
        recorded_bytes = 16;
    }
    if (recorded_bytes + frame.size() > kMaxRecordBytes) {
        std::fprintf(stderr, "[worker] record cap reached; disabling recording\n");
        out.flush();
        g_record_path.clear();
        return;
    }
    out.write(reinterpret_cast<const char*>(frame.data()), frame.size());
    out.flush();
    if (!out) {
        std::fprintf(stderr, "[worker] record write failed; disabling recording\n");
        g_record_path.clear();
        return;
    }
    recorded_bytes += frame.size();
    ++g_recorded_frame_count;
}

void RunSyntheticServer(uint16_t port) {
    openscanner::WsBroadcastServer server;
    ConfigureStatic(server);
    ControlState control;
    control.scanning = true;  // Streams immediately, as before, if no UI ever sends a command.

    server.SetTextMessageHandler([&control, &server](const std::string& msg) {
        HandleControlMessage(msg, &control);
        // FOV always "controllable" here — the synthetic sphere has no
        // hardware FOV constraint, it's purely a render-side parameter
        // ui/app.js reads back out of this same status message.
        server.BroadcastText(BuildStatusJson("synthetic", control, /*fov_controllable=*/true));
    });

    if (!server.Start(port)) {
        std::fprintf(stderr, "Failed to start WebSocket server on port %u (port in use?)\n", port);
        std::exit(1);
    }
    std::printf("Streaming synthetic point-cloud frames on ws://0.0.0.0:%u\n", port);
    std::printf("Open ui/index.html in a browser to view it.\n");

    std::vector<Vec3> base_points = MakeSpherePoints(kPointGridSize);
    uint32_t frame_id = 0;
    double angle = 0.0;
    double last_status_broadcast_ms = 0.0;
    // Row #111: the same per-tick instrumentation as the live loop, so the
    // baseline runnable without a device reports it too. Pose/getter are N/A.
    perf::TickPerf tick_perf;
    if (!tick_perf.enabled()) std::printf("[perf] per-tick timing disabled by OPENSCANNER_TICK_PERF=0\n");

    while (true) {
        auto tick_start = std::chrono::steady_clock::now();
        tick_perf.BeginTick();
        int64_t s_post_us = 0, s_bcast_us = 0;
        uint32_t frame_points_count = 0;
        uint64_t frame_bytes = 0;

        if (control.finish_requested.exchange(false)) {
            // "Finish" resets the demo back to a fresh idle sphere, ready
            // for the next "Start" — there's no real session to tear down
            // in synthetic mode.
            frame_id = 0;
            angle = 0.0;
        }

        double now_ms = NowMs();
        if (now_ms - last_status_broadcast_ms >= 1000.0) {
            std::string status_json = BuildStatusJson("synthetic", control, /*fov_controllable=*/true);
            {
                perf::SegTimer t(&s_bcast_us);
                server.BroadcastText(status_json);
            }
            frame_bytes += status_json.size();
            last_status_broadcast_ms = now_ms;
        }

        if (control.scanning.load() && !control.paused.load()) {
            std::vector<uint8_t> buf;
            {
                perf::SegTimer t(&s_post_us);
                std::vector<Vec3> frame_points = base_points;
                RotateY(&frame_points, angle);
                angle += 0.02;
                frame_points_count = static_cast<uint32_t>(frame_points.size());
                buf = BuildFrame(frame_id++, now_ms, reinterpret_cast<const float*>(frame_points.data()),
                                 frame_points_count);
            }
            {
                perf::SegTimer t(&s_bcast_us);
                if (server.ConnectedClientCount() > 0) {
                    server.BroadcastBinary(buf.data(), buf.size());
                }
            }
            frame_bytes += buf.size();
            RecordFrame(buf);
        }

        tick_perf.EndTick(0, 0, s_post_us, s_bcast_us, frame_points_count, frame_bytes);

        auto elapsed = std::chrono::steady_clock::now() - tick_start;
        auto sleep_for = std::chrono::milliseconds(static_cast<int64_t>(kFrameIntervalMs)) -
                          std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (sleep_for > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_for);
        }
    }
}

// --- Live (real hardware) mode ---------------------------------------------
// Mirrors tools/live_capture_test's lifecycle; see that file for the full
// history of why each step is shaped the way it is. Runs inside a worker
// process (same crash/hang isolation as live_capture_test) because this
// exercises the same obscan_context_destroy teardown path known to crash
// or hang on this hardware without it.

constexpr const char* kWorkerFlag = "--live-worker";
constexpr int kDeviceWaitAttempts = 30;    // ~6s at 200ms/attempt.
constexpr int kDeviceWaitIntervalMs = 200;
constexpr int kWantStableFrames = 10;
// Bounded live-session length: this mode is for watching the UI respond to
// a real device, not for running an unattended multi-hour capture. Stop
// streaming after this long and go through the normal pause/stop/destroy
// teardown automatically. Overridable (scan_server --live <port> <path>
// <seconds>) so a short test run doesn't require killing the process
// before teardown — see README.md "Known issues": killing this worker
// before it reaches obscan_scan_stop/obscan_context_destroy skips the
// same teardown that's required a physical power cycle to recover from
// in the past. Let it finish on its own; don't pkill it.
int g_live_session_seconds = 300;
constexpr int kLiveTickIntervalMs = 100;  // ~10Hz live-frame polling.
constexpr int kSlowStartDelaySeconds = 5;

bool ParseFrameRateName(const std::string& name, ObScanFrameRate* out) {
    if (name == "auto") { *out = OB_SCAN_FRAME_RATE_AUTO; return true; }
    if (name == "standard") { *out = OB_SCAN_FRAME_RATE_STANDARD; return true; }
    if (name == "balanced") { *out = OB_SCAN_FRAME_RATE_BALANCED; return true; }
    if (name == "fast") { *out = OB_SCAN_FRAME_RATE_FAST; return true; }
    return false;
}

const char* FrameRateName(ObScanFrameRate rate) {
    switch (rate) {
        case OB_SCAN_FRAME_RATE_AUTO: return "auto";
        case OB_SCAN_FRAME_RATE_STANDARD: return "standard";
        case OB_SCAN_FRAME_RATE_BALANCED: return "balanced";
        case OB_SCAN_FRAME_RATE_FAST: return "fast";
        default: return "unknown";
    }
}

#if defined(_WIN32)
bool SetEnvVar(const char* name, const char* value) { return ::_putenv_s(name, value) == 0; }
#else
bool SetEnvVar(const char* name, const char* value) { return setenv(name, value, 1) == 0; }
#endif

std::atomic<bool> g_got_connect_message{false};
std::atomic<int> g_frame_ready_count{0};
std::atomic<uint64_t> g_cloud_frames_broadcast{0};
std::atomic<uint64_t> g_full_detail_chunks_broadcast{0};
// Per-frame tracking status: the getter is exported and the numeric mapping
// is pinned (findings scan-quality-gate.md §2 / row #78), so samples are
// recorded and rendered by name (tracking_status.h). See row #92 for the
// comment this replaces.
fn_obscan_frame_get_frame_tracking_status g_frame_tracking_status_fn = nullptr;
fn_obscan_frame_release g_frame_release_fn = nullptr;

// Set by a SIGINT/SIGTERM handler so Ctrl+C (or a plain `kill`/pkill, which
// sends SIGTERM by default) breaks out of the streaming loop into the
// normal pause -> get_realtime_model_mesh -> stop -> session_destroy ->
// context_destroy teardown below, instead of abruptly killing the worker
// mid-scan. Skipping that teardown is exactly what has left this hardware
// needing a physical power cycle before (see README.md "Known issues") —
// a signal handler doing real work isn't async-signal-safe in general, but
// setting a sig_atomic_t flag is explicitly allowed and is all this does.
volatile std::sig_atomic_t g_stop_requested = 0;

void OnStopSignal(int) {
    g_stop_requested = 1;
    QuitTrace("signal handler entered (g_stop_requested set)");
}

void OnDeviceMessage(ObScanDeviceMessage msg) {
    // msg_id=0 with non-NULL data is the confirmed real connect-event shape
    // (see obscan_api.h / live_capture_test.cpp).
    if (msg.msg_id == 0 && msg.data != nullptr) {
        g_got_connect_message = true;
    }
}

const char* DataMessageName(int32_t msg_id) {
    switch (msg_id) {
        case OB_SCAN_MESSAGE_ID_NEW_RGB_IMAGE: return "NEW_RGB_IMAGE";
        case OB_SCAN_MESSAGE_ID_NEW_DEPTH_IMAGE: return "NEW_DEPTH_IMAGE";
        case OB_SCAN_MESSAGE_ID_NEW_IR_IMAGE: return "NEW_IR_IMAGE";
        case OB_SCAN_MESSAGE_ID_FRAME_READY: return "FRAME_READY";
        default: return nullptr; // unknown until OQ-P3 names it
    }
}

const char* ProcessMessageName(int32_t msg_id) {
    switch (msg_id) {
        case OB_SCAN_MESSAGE_ID_FRAME_BUFFER_FULL: return "FRAME_BUFFER_FULL";
        case OB_SCAN_MESSAGE_ID_MAX_FRAMES_REACHED: return "MAX_FRAMES_REACHED";
        case OB_SCAN_MESSAGE_ID_DETECT_FACE: return "DETECT_FACE";
        case OB_SCAN_MESSAGE_ID_SCANNING_FAILED: return "SCANNING_FAILED";
        case OB_SCAN_MESSAGE_ID_OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE:
            return "OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE";
        case OB_SCAN_MESSAGE_ID_PASTED_MARKERS_TOO_REGULAR: return "PASTED_MARKERS_TOO_REGULAR";
        default: return nullptr; // not in the header's process-id table
    }
}

void OnDataMessage(ObScanDataMessage msg) {
    const char* name = DataMessageName(msg.msg_id);
    if (name != nullptr) {
        std::printf("[worker][data] msg_id=%d (%s) data=%p thread=%lu\n", msg.msg_id, name, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    } else {
        std::printf("[worker][data] msg_id=%d data=%p thread=%lu\n", msg.msg_id, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    }
    // Any id not in the header's table is logged numerically (do not
    // invent names); the header's id table is the contract.
    std::fflush(stdout);
    if (msg.msg_id == OB_SCAN_MESSAGE_ID_FRAME_READY) {
        ++g_frame_ready_count;
        if (g_frame_tracking_status_fn != nullptr && msg.data != nullptr) {
            int32_t tracking = -1;
            if (g_frame_tracking_status_fn(msg.data, &tracking) == OB_SCAN_SUCCESS) {
                g_last_tracking_status.store(tracking);
                g_tracking_samples.fetch_add(1);
                if (tracking >= 0 && tracking < 16) g_tracking_hist[tracking].fetch_add(1);
                std::printf("[worker][data] FRAME_READY tracking_status=%d (%s)\n", tracking,
                            openscanner::tracking::TrackingStatusName(tracking));
                std::fflush(stdout);
            }
            if (g_frame_release_fn != nullptr) g_frame_release_fn(msg.data);
        }
    }
}

void OnProcessMessage(ObScanProcessMessage msg) {
    const char* name = ProcessMessageName(msg.msg_id);
    if (name != nullptr) {
        std::printf("[worker][process] msg_id=%d (%s) data=%p thread=%lu\n", msg.msg_id, name, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    } else {
        std::printf("[worker][process] msg_id=%d data=%p thread=%lu\n", msg.msg_id, msg.data,
                    static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    }
    std::fflush(stdout);
}
void OnPreviewStatus(ObScanStatus) {}

// The vendor SDK keeps the listener pointers it's given and calls them from
// its own threads, which can still be running after RunLiveServer returns
// (and during process exit). Static storage keeps them valid for the whole
// process lifetime instead of pointing into a dead stack frame.
ObScanDataMessageListener g_data_listener = {&OnDataMessage};
ObScanProcessMessageListener g_process_listener = {&OnProcessMessage};
ObScanDeviceMessageListener g_device_listener = {&OnDeviceMessage};
void OnPreviewProgress(float) {}

const char* StatusName(ObScanStatus status) {
    switch (status) {
        case OB_SCAN_SUCCESS:
            return "OB_SCAN_SUCCESS";
        case OB_SCAN_ERROR_INVALID_ARGUMENT:
            return "OB_SCAN_ERROR_INVALID_ARGUMENT";
        case OB_SCAN_ERROR_FATAL:
            return "OB_SCAN_ERROR_FATAL";
        case OB_SCAN_ERROR_DEVICE_DISCONNECT:
            return "OB_SCAN_ERROR_DEVICE_DISCONNECT";
        case OB_SCAN_ERROR_SESSION_PAUSED:
            return "OB_SCAN_ERROR_SESSION_PAUSED";
        case OB_SCAN_ERROR_SESSION_NOT_PAUSED:
            return "OB_SCAN_ERROR_SESSION_NOT_PAUSED";
        case OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION:
            return "OB_SCAN_ERROR_UNSUPPORTED_CONFIGURATION";
        case OB_SCAN_ERROR_CAMERA_PERMISSION_NOT_GRANTED:
            return "OB_SCAN_ERROR_CAMERA_PERMISSION_NOT_GRANTED";
        case OB_SCAN_ERROR_DEADLINE_EXCEEDED:
            return "OB_SCAN_ERROR_DEADLINE_EXCEEDED";
        case OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED:
            return "OB_SCAN_ERROR_CAMERA_STREAMING_RETRY_FAILED";
        case OB_SCAN_ERROR_LISTENER_NOT_FOUND:
            return "OB_SCAN_ERROR_LISTENER_NOT_FOUND";
        case OB_SCAN_ERROR_MULTI_CALLED:
            return "OB_SCAN_ERROR_MULTI_CALLED";
        case OB_SCAN_ERROR_CAMERA_NOT_REGISTER:
            return "OB_SCAN_ERROR_CAMERA_NOT_REGISTER";
        case OB_SCAN_ERROR_OUT_MEMORY:
            return "OB_SCAN_ERROR_OUT_MEMORY";
        case OB_SCAN_ERROR_FILE_NOT_EXIST:
            return "OB_SCAN_ERROR_FILE_NOT_EXIST";
        case OB_SCAN_ERROR_FILE_READ_FAILED:
            return "OB_SCAN_ERROR_FILE_READ_FAILED";
        case OB_SCAN_ERROR_FILE_WRITE_FAILED:
            return "OB_SCAN_ERROR_FILE_WRITE_FAILED";
        case OB_SCAN_ERROR_GRAPHICS_PROBLEM:
            return "OB_SCAN_ERROR_GRAPHICS_PROBLEM";
        case OB_SCAN_ERROR_HAVE_NOT_MARKER:
            return "OB_SCAN_ERROR_HAVE_NOT_MARKER";
        case OB_SCAN_ERROR_IMPORT_EMPTY_PROJ:
            return "OB_SCAN_ERROR_IMPORT_EMPTY_PROJ";
        case OB_SCAN_ERROR_IMPORT_SELF_PROJ:
            return "OB_SCAN_ERROR_IMPORT_SELF_PROJ";
        case OB_SCAN_ERROR_NO_PLUG_IN_POWER:
            return "OB_SCAN_ERROR_NO_PLUG_IN_POWER";
        case OB_SCAN_ERROR_INVALID_SCAN:
            return "OB_SCAN_ERROR_INVALID_SCAN";
        case OB_SCAN_ERROR_DEVICE_BUSY:
            return "OB_SCAN_ERROR_DEVICE_BUSY";
        case OB_SCAN_ERROR_CANCEL_RUN:
            return "OB_SCAN_ERROR_CANCEL_RUN";
        case OB_SCAN_ERROR_CAMERA_PARAMS_INCONSISTENT:
            return "OB_SCAN_ERROR_CAMERA_PARAMS_INCONSISTENT";
        case OB_SCAN_ERROR_LASER_MARKER_FRAMEWORK_INVALID_CLOUD_SCAN:
            return "OB_SCAN_ERROR_LASER_MARKER_FRAMEWORK_INVALID_CLOUD_SCAN";
        case OB_SCAN_ERROR_OUT_HARD_DISK:
            return "OB_SCAN_ERROR_OUT_HARD_DISK";
        case OB_SCAN_ERROR_READ_DEVICE_PARAMS:
            return "OB_SCAN_ERROR_READ_DEVICE_PARAMS";
        case OB_SCAN_ERROR_IMPORT_NOT_SUPPORT_PROJ:
            return "OB_SCAN_ERROR_IMPORT_NOT_SUPPORT_PROJ";
        case OB_SCAN_ERROR_IMPORT_HIGH_VERSION_PROJ:
            return "OB_SCAN_ERROR_IMPORT_HIGH_VERSION_PROJ";
        case OB_SCAN_ERROR_RESTORE_FACTORY_CRC_CHECK_FAILED:
            return "OB_SCAN_ERROR_RESTORE_FACTORY_CRC_CHECK_FAILED";
        case OB_SCAN_ERROR_RESTORE_FACTORY_DATA_WRITE_FAILED:
            return "OB_SCAN_ERROR_RESTORE_FACTORY_DATA_WRITE_FAILED";
        case OB_SCAN_ERROR_AI_HEAD_RESTORATION_NOT_DETECT_FACE:
            return "OB_SCAN_ERROR_AI_HEAD_RESTORATION_NOT_DETECT_FACE";
        case OB_SCAN_ERROR_MESH_NOT_FOUND_HOLE:
            return "OB_SCAN_ERROR_MESH_NOT_FOUND_HOLE";
        case OB_SCAN_ERROR_PROJ_NOT_WRITE_PERMISSION:
            return "OB_SCAN_ERROR_PROJ_NOT_WRITE_PERMISSION";
        case OB_SCAN_ERROR_SET_LED_LASER:
            return "OB_SCAN_ERROR_SET_LED_LASER";
        case OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS:
            return "OB_SCAN_ERROR_LOADING_INTR_LASER_PARAMS";
        case OB_SCAN_ERROR_SET_IR_MODE:
            return "OB_SCAN_ERROR_SET_IR_MODE";
        case OB_SCAN_ERROR_AI_REPLACE_RGB_FEATS_NOT_OBVIOUS:
            return "OB_SCAN_ERROR_AI_REPLACE_RGB_FEATS_NOT_OBVIOUS";
        case OB_SCAN_ERROR_IMPORTED_MESH_SIZE_NOT_SUPPORT:
            return "OB_SCAN_ERROR_IMPORTED_MESH_SIZE_NOT_SUPPORT";
        case OB_SCAN_ERROR_MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION:
            return "OB_SCAN_ERROR_MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION";
        case OB_SCAN_ERROR_ALLOCATE_GPU_RESOURCES_FAILED:
            return "OB_SCAN_ERROR_ALLOCATE_GPU_RESOURCES_FAILED";
        default:
            return "OB_SCAN_ERROR_(see obscan_api.h)";
    }
}

void Report(const char* step, ObScanStatus status) {
    std::printf("[worker] %-45s -> %s (%d)\n", step, StatusName(status), static_cast<int>(status));
    std::fflush(stdout);
}

int RunLiveServer(const std::string& library_path, uint16_t port, const std::string& record_path = "") {
    g_record_path = record_path;

    // Isolation variant: the launcher sets these in the child's inherited
    // environment (CreateProcessW with a null env block / fork+exec). The
    // worker's positional argv contract is unchanged.
    ObScanFrameRate frame_rate = OB_SCAN_FRAME_RATE_AUTO;
    const char* frame_rate_env = std::getenv("OPENSCANNER_FRAME_RATE");
    if (frame_rate_env != nullptr && frame_rate_env[0] != '\0') {
        if (!ParseFrameRateName(frame_rate_env, &frame_rate)) {
            std::fprintf(stderr, "[worker] invalid OPENSCANNER_FRAME_RATE '%s'; using auto\n", frame_rate_env);
            frame_rate = OB_SCAN_FRAME_RATE_AUTO;
        }
    }
    const char* slow_env = std::getenv("OPENSCANNER_SLOW_START");
    const bool slow_start = slow_env != nullptr && std::string(slow_env) == "1";
    std::printf("[worker] isolation: frame_rate=%s slow_start=%s\n",
                FrameRateName(frame_rate), slow_start ? "on" : "off");
    std::fflush(stdout);

    QuitTraceInit();
    QuitTrace("RunLiveServer: installing signal handlers");
    std::signal(SIGINT, &OnStopSignal);
    std::signal(SIGTERM, &OnStopSignal);
#if defined(SIGPIPE)
    // A stalled/reset client must not take down the server; SendAll also
    // returns false on failure so the socket is pruned.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    std::printf("[worker] Loading OBScan SDK from: %s\n", library_path.c_str());
    std::printf("[worker] Press Ctrl+C to stop early — this will run the normal pause/stop/destroy teardown\n");
    std::printf("[worker] instead of being killed mid-scan.\n");
    std::fflush(stdout);

    openscanner::ObscanLibrary lib(library_path);
    g_frame_tracking_status_fn = lib.obscan_frame_get_frame_tracking_status;
    g_frame_release_fn = lib.obscan_frame_release;

    std::string algo_dir = openscanner::ObscanLibrary::FindStreamingAssetsDirForLibraryPath(library_path);
    ObScanContextConfig context_config = {};
    context_config.input_algo_dependent_files_dir = algo_dir.c_str();
    context_config.output_ob_sdk_log_dir = "/tmp/openscanner_live_server_sdk_log";
    context_config.output_ob_scan_log_dir = "/tmp/openscanner_live_server_scan_log";
    context_config.output_computer_performance_fps_dir = "/tmp/openscanner_live_server_fps_log";
    context_config.output_windows_gpu_info_dir = "";

    ObScanContextHandle context = nullptr;
    ObScanStatus status = lib.obscan_context_create(&context, &context_config);
    Report("obscan_context_create", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    ObScanDeviceManagementHandle device_mgmt = nullptr;
    status = lib.obscan_context_get_device_management(context, &device_mgmt);
    Report("obscan_context_get_device_management", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_context_add_data_msg_listener(context, &g_data_listener);
    Report("obscan_context_add_data_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_context_add_process_msg_listener(context, &g_process_listener);
    Report("obscan_context_add_process_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_device_management_add_device_msg_listener(device_mgmt, &g_device_listener);
    Report("obscan_device_management_add_device_msg_listener", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    for (int attempt = 0; attempt < kDeviceWaitAttempts && !g_got_connect_message; ++attempt) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kDeviceWaitIntervalMs / 1000.0, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(kDeviceWaitIntervalMs));
#endif
    }

    int32_t device_num = 0;
    status = lib.obscan_device_management_get_device_num(device_mgmt, &device_num);
    std::printf("[worker] obscan_device_management_get_device_num -> %s, count=%d\n", StatusName(status),
                device_num);
    std::fflush(stdout);
    if (device_num == 0) {
        std::printf("[worker] No device detected — nothing to stream.\n");
        return 2;
    }

    ObScanSessionConfig session_config = {};
    session_config.output_dir = "/tmp/openscanner_live_server_session";
    session_config.name = "openscanner_scan_server_live";
    session_config.is_import_mesh_file = false;

    ObScanSessionHandle session = nullptr;
    status = lib.obscan_context_create_session(context, &session_config, &session);
    Report("obscan_context_create_session", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // Ground-truth config, same as live_capture_test (see that file for
    // the full history of how each field was determined).
    ObScanConfig scan_config = {};
    scan_config.mode_config.post_texture = OB_SCAN_NO_TEXTURE;
    scan_config.mode_config.frame_rate = frame_rate;
    scan_config.mode_config.projection = OB_SCAN_BLUE_LINE_LASER;
    scan_config.mode_config.scan_mode = OB_SCAN_MODE_OBJECT_MEDIUM;
    scan_config.mode_config.accuracy = OB_SCAN_ACCURACY_HIGH;
    scan_config.mode_config.fuse_mode = OB_SCAN_FUSE_MODE_MARKER;
    scan_config.mode_config.scan_fov = OB_SCAN_FOV_NORMAL;
    scan_config.mode_config.object_material = OB_SCAN_OBJECT_MATERIAL_STANDARD;
    scan_config.mode_config.light_type = OB_SCAN_LIGHT_TYPE_CROSS;
    scan_config.mode_config.scan_type = OB_SCAN_CLOUD_FUSED;
    scan_config.mode_config.detail_voxel_size = 0.05;
    scan_config.mode_config.voxel_size = 0.1;
    scan_config.name = "openscanner_scan_server_live_scan";

    ObScanScanHandle scan = nullptr;
    status = lib.obscan_session_create_scan(session, &scan_config, &scan);
    Report("obscan_session_create_scan", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_scan_preview(scan, &OnPreviewProgress, &OnPreviewStatus);
    Report("obscan_scan_preview", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    status = lib.obscan_scan_config_combined_exposure_and_laser_current(scan, 900);
    Report("obscan_scan_config_combined_exposure_and_laser_current", status);

    for (int i = 0; i < 100 && g_frame_ready_count.load() < kWantStableFrames; ++i) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
    }
    std::printf("[worker] frame_ready_count=%d\n", g_frame_ready_count.load());
    std::fflush(stdout);

    if (slow_start) {
        std::printf("[worker] slow-start: waiting %ds before obscan_scan_start\n", kSlowStartDelaySeconds);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(kSlowStartDelaySeconds));
    }

    status = lib.obscan_scan_start(scan);
    Report("obscan_scan_start", status);
    if (status != OB_SCAN_SUCCESS) return 1;

    // Plane-removal bind (findings plane-removal.md §3): the three toggles
    // are live-callable and gate-free, so enabling them is lawful at any
    // step. The vendor's own per-frame executor then does the work; the
    // effect is only observable through the cloud getters (compare point
    // counts). Env-driven surface (inherited by the worker):
    // OPENSCANNER_PLANE_REMOVE=1, OPENSCANNER_TURNTABLE_PLANE_REMOVE=1,
    // OPENSCANNER_MANUAL_PLANE="a,b,c,d".
    //
    // Persistence is unconfirmed: whether the toggles survive a pause is
    // not stated in the findings, so re-assert them after every successful
    // resume rather than assuming they persist. Re-calling is lawful
    // (gate-free), and the log shows each re-assert.
    auto apply_plane_config = [&lib, scan]() {
        if (const char* pr = std::getenv("OPENSCANNER_PLANE_REMOVE");
            pr != nullptr && std::string(pr) == "1") {
            Report("obscan_scan_plane_remove(true)", lib.obscan_scan_plane_remove(scan, true));
        }
        if (const char* tpr = std::getenv("OPENSCANNER_TURNTABLE_PLANE_REMOVE");
            tpr != nullptr && std::string(tpr) == "1") {
            Report("obscan_scan_turntable_plane_remove(true)",
                   lib.obscan_scan_turntable_plane_remove(scan, true));
        }
        if (const char* mp = std::getenv("OPENSCANNER_MANUAL_PLANE"); mp != nullptr && mp[0] != '\0') {
            float a = 0, b = 0, c = 0, d = 0;
            if (std::sscanf(mp, "%f,%f,%f,%f", &a, &b, &c, &d) == 4) {
                // Setter's layout: {int state, bool enabled, pad, plane}
                // (findings §4); state==2 is the native's observed int read.
                ObScanManualRPConfig cfg = {};
                cfg.state = 2;
                cfg.enabled = true;
                cfg.plane.a = a;
                cfg.plane.b = b;
                cfg.plane.c = c;
                cfg.plane.d = d;
                Report("obscan_scan_config_manaul_remove_plane(a,b,c,d)",
                       lib.obscan_scan_config_manaul_remove_plane(scan, &cfg));
            } else {
                std::fprintf(stderr, "[worker] OPENSCANNER_MANUAL_PLANE must be \"a,b,c,d\"\n");
            }
        }
    };
    apply_plane_config();

    ControlState control;
    control.scanning = true;  // obscan_scan_start above has already run.
    control.auto_exposure = false;  // Matches the unconditional 900 re-assert this loop already did.
    control.exposure_value = kDefaultExposure;
    control.fov_wide = (scan_config.mode_config.scan_fov == OB_SCAN_FOV_WIDE);

    openscanner::WsBroadcastServer server;
    ConfigureStatic(server);
    server.SetTextMessageHandler([&control, &server](const std::string& msg) {
        HandleControlMessage(msg, &control);
        if (control.finish_requested.load()) g_stop_requested = 1;
        server.BroadcastText(BuildStatusJson("live", control, /*fov_controllable=*/false));
    });
    if (!server.Start(port)) {
        std::fprintf(stderr, "[worker] Failed to start WebSocket server on port %u (port in use?)\n", port);
        // Still try to tear the scan down cleanly below rather than leaving
        // the device mid-scan.
    } else {
        std::printf("[worker] Frame protocol V%u (envelope: magic u32, version u16, type u16, seq u32, t_capture f64, t_server f64, payload_len u32 = 32 bytes)\n",
                    openscanner::frame_protocol::VERSION);
        std::printf("[worker] Streaming live scan frames on ws://0.0.0.0:%u (open ui/index.html)\n", port);
        std::printf(
            "[worker] Note: each tick calls obscan_scan_get_model_point_color_clouds, which publishes the\n"
            "[worker] model cloud, so live frames can carry points during scanning. The per-tick payload is\n"
            "[worker] decimated to a fixed budget so the UI stays live at high accumulated point counts. The\n"
            "[worker] final pause -> get_realtime_model_mesh snapshot is still broadcast at the end.\n");
    }
    std::fflush(stdout);

    uint32_t frame_id = 0;
    const int total_ticks = (g_live_session_seconds * 1000) / kLiveTickIntervalMs;
    // Row #111: measurement only — the cadence and call order below are unchanged.
    perf::TickPerf tick_perf;
    if (!tick_perf.enabled()) std::printf("[perf] per-tick timing disabled by OPENSCANNER_TICK_PERF=0\n");
    uint32_t max_points_seen = 0;
    bool hardware_paused = false;
    // Separate from hardware_paused: records what we last *attempted*
    // (regardless of outcome), so a failed resume on fragile hardware is
    // tried exactly once per click, not retried every 100ms tick until it
    // succeeds — a hammering retry loop is its own risk on an SDK with a
    // documented history of needing a power cycle after the wrong call
    // sequence. Pausing again (or clicking Start again) is still a fresh,
    // deliberate single attempt.
    bool last_requested_paused = false;
#if !defined(_WIN32)
    // Orphan guard: if the supervising app dies without sending SIGTERM,
    // the worker is reparented (ppid changes); tear down gracefully rather
    // than lingering as a second SDK user.
    const pid_t original_parent = getppid();
#endif
    for (int tick = 0; tick < total_ticks; ++tick) {
        tick_perf.BeginTick();
        int64_t s_pose_us = 0, s_getter_us = 0, s_post_us = 0, s_bcast_us = 0;
        uint32_t frame_points = 0;
        uint64_t frame_bytes = 0;
#if !defined(_WIN32)
        if (getppid() != original_parent) {
            QuitTrace("orphan guard: parent changed -> stop requested");
            std::printf("[worker] parent exited — stopping and tearing down cleanly.\n");
            std::fflush(stdout);
            g_stop_requested = 1;
        }
#endif
        if (g_stop_requested) {
            QuitTrace("live loop observed stop request -> breaking to teardown");
            std::printf(
                "[worker] Stop requested (Ctrl+C or UI \"finish\") — ending the live loop and tearing down "
                "cleanly.\n");
            std::fflush(stdout);
            break;
        }
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kLiveTickIntervalMs / 1000.0, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(kLiveTickIntervalMs));
#endif

        // Pause/resume only on a state transition, never repeated every
        // tick. obscan_scan_pause is already proven (this file already
        // called it once, at end-of-session, before this change). Resuming
        // via obscan_scan_start on an already-started scan has not been
        // exercised anywhere in this codebase before now — see the PR this
        // landed in for the risk accepted by wiring it up.
        bool want_paused = control.paused.load();
        if (want_paused != last_requested_paused) {
            last_requested_paused = want_paused;
            ObScanStatus toggle_status = want_paused ? lib.obscan_scan_pause(scan) : lib.obscan_scan_start(scan);
            Report(want_paused ? "obscan_scan_pause (control)" : "obscan_scan_start (resume, control)",
                   toggle_status);
            if (toggle_status == OB_SCAN_SUCCESS) {
                hardware_paused = want_paused;
                if (!want_paused) apply_plane_config();  // toggles may not survive pause
            }
        }

        ObScanPointColorClouds cloud = {};
        ObScanCloudBbox bbox = {};
        uint32_t published_count = 0;
        if (!hardware_paused) {
            // No dedicated auto/manual exposure toggle exists in the ABI; the
            // device's own auto-exposure decays a one-time value back toward
            // its own target within ~15s (see obscan_api.h). Hold a manual
            // value by re-asserting every tick; in auto mode, skip the call
            // and let that decay happen.
            if (!control.auto_exposure.load()) {
                lib.obscan_scan_config_combined_exposure_and_laser_current(scan, control.exposure_value.load());
            }

            // Pose: use the registration pose, which is the live solve that
            // follows the scanner during a scan (the global pose only updates
            // when a registration result is confirmed). Broadcast it as a
            // pose message so the page can follow the scanner POV.
            bool have_scanner_pose = false;
            double scanner_x = 0, scanner_y = 0, scanner_z = 0;
            {
                ObScanPose pose = {};
                ObScanStatus pose_status;
                {
                    perf::SegTimer t(&s_pose_us);
                    pose_status = lib.obscan_scan_get_registration_pose(scan, &pose);
                }
                if (pose_status == OB_SCAN_SUCCESS) {
                    have_scanner_pose = true;
                    scanner_x = pose.m[0][3];
                    scanner_y = pose.m[1][3];
                    scanner_z = pose.m[2][3];
                    std::vector<uint8_t> pbuf;
                    {
                        perf::SegTimer t(&s_post_us);
                        pbuf = openscanner::frame_protocol::encode_pose(
                            frame_id++, NowMs(), NowMs(), &pose.m[0][0]);
                    }
                    {
                        perf::SegTimer t(&s_bcast_us);
                        server.BroadcastBinary(pbuf.data(), pbuf.size());
                    }
                    frame_bytes += pbuf.size();
                    RecordFrame(pbuf);
                }
            }

            // The color getter is the publish trigger for the model cloud
            // (see docs/recon/findings/scan-live-data.md §3): calling it
            // swaps the fusion temp cloud into the published show cloud, so
            // the UI finally see live points while scanning.
            // out_count is not validated by the wrapper, so pass a valid
            // pointer; out_bbox is required non-null.
            {
                perf::SegTimer t(&s_getter_us);
                status = lib.obscan_scan_get_model_point_color_clouds(scan, &cloud, &bbox, &published_count);
            }
            if (status == OB_SCAN_SUCCESS) {
                max_points_seen = std::max(max_points_seen, cloud.num_points);
                if (cloud.num_points > 0 && cloud.points != nullptr) {
                    perf::SegTimer post_timer(&s_post_us);
                    // Our own decision layer (row #77): fit a dominant plane
                    // from a bounded sample and cut below it. Fitting and
                    // filtering both stay O(budget), like the #32 broadcast.
                    constexpr uint32_t kLiveBroadcastPointBudget = 20000;
                    bool use_plane = false;
                    OurPlane plane;
                    // Margin semantics: keep points with signed distance >= margin;
                    // cut the half-space strictly below the plane. The normal is
                    // oriented toward the scanner, so the object side is positive.
                    double margin = 0.0;
                    const char* our_env = std::getenv("OPENSCANNER_OUR_PLANE_REMOVE");
                    if (our_env != nullptr && std::string(our_env) == "1") {
                        const char* margin_env = std::getenv("OPENSCANNER_PLANE_MARGIN");
                        bool margin_given = false;
                        if (margin_env != nullptr && margin_env[0] != '\0') {
                            char* end = nullptr;
                            double parsed = std::strtod(margin_env, &end);
                            if (end == margin_env || *end != '\0' || !std::isfinite(parsed) ||
                                parsed < -1e6 || parsed > 1e6) {
                                std::fprintf(stderr,
                                             "[worker] invalid OPENSCANNER_PLANE_MARGIN '%s'; our plane removal "
                                             "disabled\n",
                                             margin_env);
                            } else {
                                margin = parsed;
                                margin_given = true;
                            }
                        }
                        if (margin_env == nullptr || margin_env[0] == '\0' || margin_given) {
                            uint32_t sample_stride = 1;
                            if (cloud.num_points > kLiveBroadcastPointBudget) {
                                sample_stride = (cloud.num_points + kLiveBroadcastPointBudget - 1) / kLiveBroadcastPointBudget;
                            }
                            std::vector<ObScanPoint> sample;
                            sample.reserve(kLiveBroadcastPointBudget);
                            for (uint32_t s = 0; s < cloud.num_points && sample.size() < kLiveBroadcastPointBudget; s += sample_stride) {
                                sample.push_back(cloud.points[s]);
                            }
                            double extent = std::max(view_radius_guess(sample.data(), (uint32_t)sample.size()), 1e-6);
                            if (!margin_given) margin = extent * 0.01; // drop the plane's own face too
                            plane = FitDominantPlane(sample.data(), (uint32_t)sample.size(), 60, extent * 0.01,
                                                     scanner_x, scanner_y, scanner_z, have_scanner_pose);
                            if (plane.valid) { use_plane = true; ++g_our_plane_fits; }
                        }
                    }
                    uint32_t stride = 1;
                    if (cloud.num_points > kLiveBroadcastPointBudget) {
                        stride = (cloud.num_points + kLiveBroadcastPointBudget - 1) / kLiveBroadcastPointBudget;
                    }
                    std::vector<float> xyz;
                    xyz.reserve(static_cast<size_t>(kLiveBroadcastPointBudget) * 3);
                    for (uint32_t src = 0; src < cloud.num_points; src += stride) {
                        if (use_plane) {
                            double dist = plane.a * cloud.points[src].x + plane.b * cloud.points[src].y +
                                          plane.c * cloud.points[src].z + plane.d;
                            if (dist < margin) { g_our_plane_cut.fetch_add(1); continue; }
                            g_our_plane_kept.fetch_add(1);
                        }
                        xyz.push_back(cloud.points[src].x);
                        xyz.push_back(cloud.points[src].y);
                        xyz.push_back(cloud.points[src].z);
                    }
                    uint32_t broadcast_count = static_cast<uint32_t>(xyz.size() / 3);
                    std::vector<uint8_t> buf = BuildFrame(frame_id++, NowMs(), xyz.data(), broadcast_count);
                    post_timer.Stop();
                    {
                        perf::SegTimer bcast_timer(&s_bcast_us);
                        server.BroadcastBinary(buf.data(), buf.size());
                    }
                    frame_points = broadcast_count;
                    frame_bytes += buf.size();
            RecordFrame(buf);
                    ++g_cloud_frames_broadcast;
                } else {
                    // Broadcast an empty frame too, so the UI's data-fps/HUD
                    // reflects a real, live connection rather than going idle.
                    std::vector<uint8_t> buf;
                    {
                        perf::SegTimer t(&s_post_us);
                        buf = BuildFrame(frame_id++, NowMs(), nullptr, 0);
                    }
                    {
                        perf::SegTimer bcast_timer(&s_bcast_us);
                        server.BroadcastBinary(buf.data(), buf.size());
                    }
                    frame_bytes += buf.size();
            RecordFrame(buf);
                    ++g_cloud_frames_broadcast;
                }
                // Release is a no-op in the vendor (native `ret`); call it
                // for API symmetry per the header. Buffers are borrowed, so
                // copy out before the next publish.
                lib.obscan_scan_model_clouds_release(scan);
            }
        }

        if (tick % 10 == 0) {  // ~every 1s
            std::string status_json = BuildStatusJson("live", control, /*fov_controllable=*/false);
            {
                perf::SegTimer bcast_timer(&s_bcast_us);
                server.BroadcastText(status_json);
            }
            frame_bytes += status_json.size();
        }
        if (tick % 50 == 0) {  // ~every 5s
            std::printf("[worker] tick %d/%d: color_cloud num_points=%u (max seen so far: %u), clients=%zu, paused=%d\n",
                        tick, total_ticks, cloud.num_points, max_points_seen, server.ConnectedClientCount(),
                        hardware_paused ? 1 : 0);
            std::fflush(stdout);
        }
        tick_perf.EndTick(s_pose_us, s_getter_us, s_post_us, s_bcast_us, frame_points, frame_bytes);
    }

    // Row #111: print before the teardown, which can hang (#89).
    tick_perf.Summary();

    if (!hardware_paused) {
        status = lib.obscan_scan_pause(scan);
        Report("obscan_scan_pause", status);
    }

    {
        ObScanTriangleMesh mesh = {};
        ObScanStatus mesh_status = lib.obscan_scan_get_realtime_model_mesh(scan, &mesh);
        std::printf("[worker] obscan_scan_get_realtime_model_mesh -> %s (%d), num_vertices=%u, num_triangles=%u\n",
                    StatusName(mesh_status), static_cast<int>(mesh_status), mesh.num_vertices, mesh.num_triangles);
        std::fflush(stdout);
        if (mesh_status == OB_SCAN_SUCCESS && mesh.num_vertices > 0 && mesh.vertices != nullptr) {
            // Bounded preview frame (matches the live path) so no single
            // frame is unbounded; full detail follows as chunks below.
            constexpr uint32_t kFinalMeshPreviewBudget = 20000;
            uint32_t stride = 1;
            uint32_t preview_count = mesh.num_vertices;
            if (preview_count > kFinalMeshPreviewBudget) {
                stride = (preview_count + kFinalMeshPreviewBudget - 1) / kFinalMeshPreviewBudget;
                preview_count = (mesh.num_vertices + stride - 1) / stride;
            }
            std::vector<float> xyz(static_cast<size_t>(preview_count) * 3);
            for (uint32_t i = 0, src = 0; i < preview_count; ++i, src += stride) {
                xyz[i * 3 + 0] = mesh.vertices[src].x;
                xyz[i * 3 + 1] = mesh.vertices[src].y;
                xyz[i * 3 + 2] = mesh.vertices[src].z;
            }
            std::vector<uint8_t> buf = BuildFrame(frame_id++, NowMs(), xyz.data(), preview_count);
            server.BroadcastBinary(buf.data(), buf.size());
            RecordFrame(buf);
            ++g_cloud_frames_broadcast;
            std::printf("[worker] Broadcast final reconstructed mesh preview (%u of %u points).\n",
                        preview_count, mesh.num_vertices);

            // On-demand full-detail path: stream the dense mesh as bounded
            // chunks so the page can accumulate and render it without any
            // unbounded frame. The page (ui/app.js) accumulates binary
            // frames between full_mesh_start and full_mesh_done.
            server.BroadcastText(std::string("{\"type\":\"full_mesh_start\",\"total\":") +
                                std::to_string(mesh.num_vertices) + "}");
            constexpr uint32_t kFullDetailChunk = 20000;
            for (uint32_t off = 0; off < mesh.num_vertices; off += kFullDetailChunk) {
                uint32_t cnt = std::min(kFullDetailChunk, mesh.num_vertices - off);
                std::vector<float> cxyz(static_cast<size_t>(cnt) * 3);
                for (uint32_t i = 0; i < cnt; ++i) {
                    cxyz[i * 3 + 0] = mesh.vertices[off + i].x;
                    cxyz[i * 3 + 1] = mesh.vertices[off + i].y;
                    cxyz[i * 3 + 2] = mesh.vertices[off + i].z;
                }
                std::vector<uint8_t> cbuf = BuildFrame(frame_id++, NowMs(), cxyz.data(), cnt);
                server.BroadcastBinary(cbuf.data(), cbuf.size());
                RecordFrame(cbuf);
                ++g_full_detail_chunks_broadcast;
            }
            server.BroadcastText("{\"type\":\"full_mesh_done\"}");
            std::printf("[worker] Streamed full-detail mesh in %u-point chunks.\n",
                        kFullDetailChunk);

            // Compile/runtime proof that the loader now resolves the declared
            // release; the frames above already copied the vertices out.
            lib.obscan_scan_realtime_model_mesh_release(scan);
            // Give any connected client a moment to actually receive it
            // before teardown starts closing sockets.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

        }
    }

    for (int i = 0; i < 25; ++i) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
    }

    // is_block=false returns immediately: the SDK runs the save/serialize
    // tail on its "default" pool instead of blocking this thread. The
    // destroys below then refuse (MULTI_CALLED / "must first wait other
    // process finished") until that tail completes, so poll them instead
    // of treating the first refusal as a failure.
    QuitTrace("teardown: calling obscan_scan_stop(is_block=false)");
    status = lib.obscan_scan_stop(scan, &OnPreviewProgress, &OnPreviewStatus, /*is_block=*/false);
    QuitTrace("teardown: obscan_scan_stop returned");
    Report("obscan_scan_stop", status);

    {
        int polls = 0;
        int refusals = 0;
        auto started = std::chrono::steady_clock::now();
        do {
            ++polls;
            status = lib.obscan_session_destroy(&session);
            if (status == OB_SCAN_ERROR_MULTI_CALLED) {
                ++refusals;
                if (refusals == 1 || refusals % 10 == 0) {
                    std::printf("[worker] obscan_session_destroy poll %d: refused (%s)\n", polls, StatusName(status));
                    std::fflush(stdout);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } while (status == OB_SCAN_ERROR_MULTI_CALLED && polls < 3000);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        std::printf("[worker] obscan_session_destroy: %d poll(s), %d refusal(s), %.1fs, final %s\n",
                    polls, refusals, elapsed / 1000.0, StatusName(status));
        std::fflush(stdout);
        Report("obscan_session_destroy", status);
        QuitTrace("teardown: session_destroy poll loop done");
    }

    QuitTrace("teardown: starting context_destroy poll loop");
    {
        int polls = 0;
        int refusals = 0;
        auto started = std::chrono::steady_clock::now();
        bool timed_out = false;
        // Row #89: the bound must hold even when the thread that made the
        // vendor call cannot itself make progress. The rerun showed the block
        // entered and then no diagnostic at all (not even the lock-free
        // "poll loop done" trace) for 119 s, so a bound enforced only by the
        // calling thread is not a bound. This watchdog enforces it from a
        // thread the hung vendor teardown cannot block, and exits with the
        // timeout code if the caller never completes the wait.
        openscanner::bounded::Watchdog watchdog(
            []() {
                QuitTracef("teardown: context_destroy: WATCHDOG fired (%d ms bound + %d ms grace exceeded); "
                           "the caller never completed the wait - exiting %d",
                           kContextDestroyCallTimeoutMs, kContextDestroyWatchdogGraceMs, kExitContextDestroyTimeout);
                std::_Exit(kExitContextDestroyTimeout);
            },
            kContextDestroyWatchdogGraceMs);
        do {
            ++polls;
            // Arm the watchdog before anything that can block, so "the caller
            // is stuck" is observable from here until the wait completes.
            watchdog.Arm(kContextDestroyCallTimeoutMs);
            QuitTracef("teardown: context_destroy: poll %d: entered the %d ms bound (before any allocation)",
                       polls, kContextDestroyCallTimeoutMs);
            // The thread owns its state: on the detached (timeout) path it
            // may still be inside the vendor call after this scope ends, so
            // shared_ptr captures keep status/finished/context alive.
            auto destroy_fn = lib.obscan_context_destroy;
            auto ctx = std::make_shared<ObScanContextHandle>(context);
            auto finished = std::make_shared<std::atomic<bool>>(false);
            auto result = std::make_shared<ObScanStatus>(OB_SCAN_ERROR_FATAL);
            QuitTracef("teardown: context_destroy: poll %d: state allocated; spawning the vendor call", polls);
            std::thread worker([destroy_fn, ctx, finished, result]() {
                *result = destroy_fn(ctx.get());
                finished->store(true, std::memory_order_release);
            });
            QuitTracef("teardown: context_destroy: poll %d: vendor call thread spawned; wait started (%d ms)",
                       polls, kContextDestroyCallTimeoutMs);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kContextDestroyCallTimeoutMs);
            while (!finished->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            // The wait is over either way: the caller is alive and owns the
            // outcome from here. Stand the watchdog down before any output.
            watchdog.Disarm();
            if (!finished->load(std::memory_order_acquire)) {
                timed_out = true;
                status = OB_SCAN_ERROR_DEADLINE_EXCEEDED;
                QuitTracef("teardown: context_destroy: poll %d: wait TIMED OUT after %d ms; abandoning context (not joined)",
                           polls, kContextDestroyCallTimeoutMs);
                worker.detach();
                break;
            }
            QuitTracef("teardown: context_destroy: poll %d: wait returned (%s)", polls, StatusName(*result));
            worker.join();
            status = *result;
            if (status == OB_SCAN_ERROR_MULTI_CALLED) {
                ++refusals;
                if (refusals == 1 || refusals % 10 == 0) {
                    std::printf("[worker] obscan_context_destroy poll %d: refused (%s)\n", polls, StatusName(status));
                    std::fflush(stdout);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } while (!timed_out && status == OB_SCAN_ERROR_MULTI_CALLED && polls < 3000);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        if (timed_out) {
            // The vendor call never returned and its abandoned thread still
            // holds whatever locks it grabbed. Every line on the abandon path
            // goes through write(2), never stdout, so the outcome is reported
            // even when the hung thread holds the stdio lock — which is the
            // failure the rerun showed. The exit-code flag is set first, so
            // main() reaches the timeout exit even if a later call blocks.
            g_context_destroy_timed_out.store(true);
            QuitTracef("teardown: context_destroy: ABANDONED after %.1fs (context left for process exit to reclaim)",
                       elapsed / 1000.0);
            QuitTracef("teardown: context_destroy -> ABANDONED (no vendor return); exiting %d", kExitContextDestroyTimeout);
        } else {
            // The vendor returned; stdout was never suspect on this path.
            std::printf("[worker] obscan_context_destroy: %d poll(s), %d refusal(s), %.1fs, final %s\n",
                        polls, refusals, elapsed / 1000.0, StatusName(status));
            std::fflush(stdout);
            Report("obscan_context_destroy", status);
        }
        QuitTrace("teardown: context_destroy poll loop done");
    }
    QuitTrace("teardown complete");

    if (!g_record_path.empty()) {
        std::fstream out(g_record_path, std::ios::binary | std::ios::in | std::ios::out);
        if (out) {
            out.seekp(8);
            uint64_t count = g_recorded_frame_count;
            out.write(reinterpret_cast<const char*>(&count), sizeof(count));
            out.seekp(0, std::ios::end);
            const char term[4] = {'O','S','C','E'};
            out.write(term, 4);
            out.flush();
        }
    }

    std::printf("[worker] Frame protocol V%u counters: cloud_frames=%llu full_detail_chunks=%llu\n",
                openscanner::frame_protocol::VERSION,
                (unsigned long long)g_cloud_frames_broadcast.load(),
                (unsigned long long)g_full_detail_chunks_broadcast.load());
    {
        std::printf("[worker] tracking_status samples=%llu histogram:",
                    (unsigned long long)g_tracking_samples.load());
        for (int i = 0; i < 16; ++i) {
            uint64_t n = g_tracking_hist[i].load();
            if (n > 0) std::printf(" %s=%llu", openscanner::tracking::TrackingStatusName(i), (unsigned long long)n);
        }
        std::printf("\n");
    }
    std::printf("[worker] our_plane_removal fits=%llu kept=%llu cut=%llu\n",
                (unsigned long long)g_our_plane_fits.load(),
                (unsigned long long)g_our_plane_kept.load(),
                (unsigned long long)g_our_plane_cut.load());
    std::fflush(stdout);

    server.Stop();
    QuitTrace("RunLiveServer returning (worker about to exit)");
    if (g_context_destroy_timed_out.load()) {
        // Row #89: lock-free, like the abandon path above — the hung vendor
        // thread may still hold the stdio lock, and a blocked printf here
        // would stop the process from ever returning this code.
        QuitTracef("teardown: Exit code %d = context_destroy deadline exceeded (distinct from 2 = skipped/no dylib)",
                   kExitContextDestroyTimeout);
        return kExitContextDestroyTimeout;
    }
    return 0;
}

int RunLiveMode(int argc, char** argv) {
    // Ctrl+C in an interactive terminal delivers SIGINT to this launcher
    // too (same process group as the worker). Ignore it here rather than
    // dying immediately — RunSelfAsWorker below just keeps waiting, and
    // the worker's own handler (see OnStopSignal) is what actually does
    // the graceful pause/stop/destroy teardown before it exits.
    std::signal(SIGINT, SIG_IGN);

    // scan_server --live [port] [library_path] [session_seconds]
    //             [--record <file>] [--frame-rate auto|standard|balanced|fast]
    //             [--slow-start]
    uint16_t port = 8787;
    std::string library_path;
    int session_seconds = g_live_session_seconds;
    std::string record_path;
    std::string frame_rate_name = "auto";
    bool slow_start = false;
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--record" && i + 1 < argc) {
            record_path = argv[++i];
        } else if (arg == "--frame-rate") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--frame-rate needs a value (auto|standard|balanced|fast)\n");
                return 2;
            }
            frame_rate_name = argv[++i];
        } else if (arg == "--slow-start") {
            slow_start = true;
        } else {
            positional.push_back(arg);
        }
    }
    ObScanFrameRate frame_rate = OB_SCAN_FRAME_RATE_AUTO;
    if (!ParseFrameRateName(frame_rate_name, &frame_rate)) {
        std::fprintf(stderr, "invalid --frame-rate '%s' (want auto|standard|balanced|fast)\n",
                     frame_rate_name.c_str());
        return 2;
    }
    if (positional.size() > 0) port = static_cast<uint16_t>(std::atoi(positional[0].c_str()));
    if (positional.size() > 1) library_path = positional[1];
    if (positional.size() > 2) session_seconds = std::atoi(positional[2].c_str());
    if (library_path.empty()) library_path = openscanner::ObscanLibrary::FindDefaultLibraryPath();
    if (library_path.empty()) {
        std::fprintf(stderr, "No OBScan SDK library found. Pass the path explicitly: scan_server --live <port> <path>\n");
        return 1;
    }

#if defined(__APPLE__) && defined(VULKAN_DISABLE_INTERPOSE_DYLIB)
    // See tools/live_capture_test/main.cpp and vulkan_disable_interpose.c
    // for the full story — required for obscan_context_destroy to tear
    // down cleanly instead of crashing/hanging on this hardware.
    std::string moltenvk_path;
    {
        size_t slash = library_path.find_last_of('/');
        if (slash != std::string::npos) {
            moltenvk_path = library_path.substr(0, slash + 1) + "libMoltenVK.dylib";
        }
    }
    if (!moltenvk_path.empty()) {
        std::string insert = moltenvk_path + ":" + VULKAN_DISABLE_INTERPOSE_DYLIB;
        setenv("DYLD_INSERT_LIBRARIES", insert.c_str(), /*overwrite=*/1);
    }
#endif

    std::printf("[launcher] Running live scan server in a worker process (up to %ds)...\n", session_seconds);
    std::fflush(stdout);

    // Pass the isolation levers to the worker through the inherited
    // environment (CreateProcessW null env block / fork+exec); the worker's
    // positional argv contract stays unchanged. Set both explicitly so an
    // inherited shell value cannot alter the default run.
    if (!SetEnvVar("OPENSCANNER_FRAME_RATE", frame_rate_name.c_str()) ||
        !SetEnvVar("OPENSCANNER_SLOW_START", slow_start ? "1" : "0")) {
        std::fprintf(stderr, "failed to set the live worker's isolation environment\n");
        return 2;
    }
    std::printf("[launcher] isolation: frame_rate=%s slow_start=%s\n",
                FrameRateName(frame_rate), slow_start ? "on" : "off");
    std::fflush(stdout);

    int worker_exit_code = -1;
    std::string crash_reason;
    // Budget: device-connect (<=6s) + frame-ready wait (<=20s) +
    // session_seconds of streaming + pause/mesh/stop settle + the SDK's
    // out-of-core serialization tail (is_block=false, polled above; can
    // run for minutes on a large scan). Generous margin past that.
    const int timeout_seconds = session_seconds + 300;
    std::vector<std::string> worker_args = {kWorkerFlag, std::to_string(port), library_path, std::to_string(session_seconds)};
    if (!record_path.empty()) worker_args.push_back(record_path);
    bool worker_ok = openscanner::RunSelfAsWorker(
        worker_args, &worker_exit_code, &crash_reason, timeout_seconds);
    if (worker_ok) {
        std::printf("[launcher] Worker exited normally with code %d.\n", worker_exit_code);
        return worker_exit_code;
    }
    std::printf("[launcher] Worker crashed or timed out inside the vendor SDK (%s).\n", crash_reason.c_str());
    return 2;
}

int RunReplayMode(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: scan_server --replay <file> [port]\n");
        return 1;
    }
    const char* path = argv[2];
    uint16_t port = argc > 3 ? static_cast<uint16_t>(std::atoi(argv[3])) : 8787;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Cannot open replay file: %s\n", path);
        return 1;
    }
    char header[16];
    in.read(header, 16);
    if (!in || std::memcmp(header, "OSCR", 4) != 0) {
        std::fprintf(stderr, "Bad replay header: %s\n", path);
        return 1;
    }
    uint32_t version = (unsigned char)header[4] | ((unsigned char)header[5] << 8) |
                       ((unsigned char)header[6] << 16) | ((unsigned char)header[7] << 24);
    if (version != 1) {
        std::fprintf(stderr, "Unsupported replay version: %u\n", version);
        return 1;
    }

    openscanner::WsBroadcastServer server;
    ConfigureStatic(server);
    if (!server.Start(port)) {
        std::fprintf(stderr, "[worker] Failed to start WebSocket server on port %u\n", port);
        return 1;
    }
    std::printf("[worker] Replaying %s on ws://0.0.0.0:%u\n", path, port);
    std::fflush(stdout);

    size_t frames = 0;
    size_t pose_frames = 0;
    size_t cloud_frames = 0;
    double prev_t = 0;
    bool have_prev = false;
    bool saw_terminator = false;
    while (true) {
        char peek[4];
        in.read(peek, 4);
        if (in.gcount() != 4) {
            if (in.gcount() == 0) break; // clean EOF
            std::fprintf(stderr, "[worker] truncated terminator\n"); break;
        }
        if (std::memcmp(peek, "OSCE", 4) == 0) { saw_terminator = true; break; }
        in.seekg(-4, std::ios::cur);
        char env[32];
        in.read(env, 32);
        if (!in) { std::fprintf(stderr, "[worker] truncated frame header\n"); break; }
        uint32_t payload_len = (unsigned char)env[28] | ((unsigned char)env[29]<<8) |
                               ((unsigned char)env[30]<<16) | ((unsigned char)env[31]<<24);
        if (payload_len > (64u * 1024 * 1024)) { std::fprintf(stderr, "[worker] replay frame too large\n"); break; }
        size_t frame_len = 32 + payload_len;
        std::vector<uint8_t> frame(frame_len);
        std::memcpy(frame.data(), env, 32);
        if (payload_len > 0) in.read(reinterpret_cast<char*>(frame.data()+32), payload_len);
        if (!in) { std::fprintf(stderr, "[worker] truncated frame payload\n"); break; }

        // Validate the V1 envelope (magic/version/type), then decode per
        // type. Any valid V1 frame is replayed as the live client sees it;
        // a recording carries pose and cloud frames (and may carry more).
        uint32_t magic = (unsigned char)env[0] | ((unsigned char)env[1] << 8) |
                         ((unsigned char)env[2] << 16) | ((unsigned char)env[3] << 24);
        uint16_t version = (unsigned char)env[4] | ((unsigned char)env[5] << 8);
        uint16_t type = (unsigned char)env[6] | ((unsigned char)env[7] << 8);
        if (magic != openscanner::frame_protocol::MAGIC ||
            version != openscanner::frame_protocol::VERSION) {
            std::fprintf(stderr, "[worker] invalid envelope in replay\n");
            break;
        }
        uint64_t tcu = 0;
        for (int i = 0; i < 8; ++i) tcu |= (uint64_t)(unsigned char)env[12 + i] << (i * 8);
        double tc = 0;
        std::memcpy(&tc, &tcu, sizeof(tc));
        uint32_t seq = 0;
        if (type == (uint16_t)openscanner::frame_protocol::MessageType::CloudSnapshot) {
            uint32_t count = 0; double ts = 0; const float* xyz = nullptr;
            if (!openscanner::frame_protocol::decode_cloud_snapshot(frame.data(), frame.size(), &seq, &tc, &ts, &xyz, &count)) {
                std::fprintf(stderr, "[worker] invalid cloud frame in replay\n");
                break;
            }
            ++cloud_frames;
        } else if (type == (uint16_t)openscanner::frame_protocol::MessageType::Pose) {
            double ts = 0; const double* m = nullptr;
            if (!openscanner::frame_protocol::decode_pose(frame.data(), frame.size(), &seq, &tc, &ts, &m)) {
                std::fprintf(stderr, "[worker] invalid pose frame in replay\n");
                break;
            }
            ++pose_frames;
        }
        // Other V1 kinds are broadcast as-is (envelope already validated).

        if (!have_prev) { prev_t = tc; have_prev = true; }
        else {
            double dt = tc - prev_t;
            // Clamp: skip absurd deltas (clock jumps / corrupt timestamps).
            if (dt > 0 && dt < 10000) std::this_thread::sleep_for(std::chrono::milliseconds((int)dt));
            prev_t = tc;
        }
        server.BroadcastBinary(frame.data(), frame.size());
        ++frames;
    }
    if (saw_terminator) {
        std::printf("[worker] Replay complete (%zu frames: %zu cloud, %zu pose).\n", frames, cloud_frames, pose_frames);
    } else {
        std::printf("[worker] Replay incomplete (%zu frames: %zu cloud, %zu pose): missing terminator.\n", frames,
                    cloud_frames, pose_frames);
    }
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(5));
    server.Stop();
    return 0;
}


// CaptureSource seam (architecture doc §6): both the live and the replay
// path implement openscanner::CaptureSource. subscribe() is a documented
// stub for now; the full §6 surface (status/start/pause/stop/...) and a
// FrameSink-backed unification of RunLiveServer's lifecycle are deliberately
// left to a follow-up row — see the PR notes.
class ObscanCaptureSource : public openscanner::CaptureSource {
public:
    ObscanCaptureSource(int argc, char** argv) : argc_(argc), argv_(argv) {}
    void subscribe(openscanner::FrameSink&) override {}
    int run() override {
        if (argc_ >= 3 && std::string(argv_[1]) == kWorkerFlag) {
            uint16_t port = static_cast<uint16_t>(std::atoi(argv_[2]));
            std::string library_path = argc_ > 3 ? argv_[3] : "";
            if (argc_ > 4) g_live_session_seconds = std::atoi(argv_[4]);
            std::string record_path = argc_ > 5 ? argv_[5] : "";
            return RunLiveServer(library_path, port, record_path);
        }
        if (argc_ >= 2 && std::string(argv_[1]) == "--live") {
            return RunLiveMode(argc_, argv_);
        }
        return 0;
    }
private:
    int argc_;
    char** argv_;
};

class ReplayCaptureSource : public openscanner::CaptureSource {
public:
    ReplayCaptureSource(int argc, char** argv) : argc_(argc), argv_(argv) {}
    void subscribe(openscanner::FrameSink&) override {}
    int run() override {
        if (argc_ >= 2 && std::string(argv_[1]) == "--replay") {
            return RunReplayMode(argc_, argv_);
        }
        return 0;
    }
private:
    int argc_;
    char** argv_;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == kWorkerFlag) {
        std::unique_ptr<openscanner::CaptureSource> src(new ObscanCaptureSource(argc, argv));
        return src->run();
    }

    if (argc >= 2 && std::string(argv[1]) == "--live") {
        std::unique_ptr<openscanner::CaptureSource> src(new ObscanCaptureSource(argc, argv));
        return src->run();
    }

    if (argc >= 2 && std::string(argv[1]) == "--replay") {
        std::unique_ptr<openscanner::CaptureSource> src(new ReplayCaptureSource(argc, argv));
        return src->run();
    }

    if (argc >= 2 && std::string(argv[1]) == "--synthetic") {
        uint16_t port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 8787;
        RunSyntheticServer(port);
        return 0;
    }

    uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 8787;
    RunSyntheticServer(port);
    return 0;
}
