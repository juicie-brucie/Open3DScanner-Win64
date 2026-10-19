// The vendor's per-frame tracking verdict, rendered by its pinned name so a
// consumer never has to hardcode the enum. Header-only and hardware-free so
// it can be unit-tested (tracking_status_test.cpp).
//
// The value set and its numeric mapping are the contract's, not ours:
// see docs/recon/findings/scan-quality-gate.md §2 (row #78) and the
// ObScanTrackingStatus enum in engine/include/openscanner/obscan_api.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "openscanner/obscan_api.h"

namespace openscanner::tracking {

// How many distinct statuses the contract declares (success through
// moving-too-fast); derived from the enum so it tracks obscan_api.h.
inline constexpr int32_t kTrackingStatusCount = OB_SCAN_TRACKING_FAILURE_MOVING_TOO_FAST + 1;

// The pinned name for a status value, matching the vendor's own
// _OBScanTrackingStatus_name lookup (which bounds-checks the index and
// returns "UNKNOWN" out of range). The labels line up with the UI's
// kTrackingNames vocabulary (ui/app.js) so both surfaces name a value the
// same way; the enum in obscan_api.h is the single source.
inline const char* TrackingStatusName(int32_t status) {
    switch (status) {
        case OB_SCAN_TRACKING_SUCCESS:
            return "SUCCESS";
        case OB_SCAN_TRACKING_LOST:
            return "LOST";
        case OB_SCAN_TRACKING_AMBIGUOUS_PLANE:
            return "AMBIGUOUS_PLANE";
        case OB_SCAN_TRACKING_FAILURE_INSUFF_VALID_POINTS:
            return "FAILURE_INSUFF_VALID_POINTS";
        case OB_SCAN_TRACKING_FAILURE_TEXTURELESS:
            return "FAILURE_TEXTURELESS";
        case OB_SCAN_TRACKING_FAILURE_MARKERLESS:
            return "FAILURE_MARKERLESS";
        case OB_SCAN_TRACKING_FAILURE_MOVING_TOO_FAST:
            return "FAILURE_MOVING_TOO_FAST";
        default:
            return "UNKNOWN";
    }
}

// The status-JSON tail for one tracking value: the raw number (kept for
// backward compatibility with the existing HUD) plus the pinned name, so a
// consumer reading the worker's status path gets the vendor's reason without
// re-deriving the mapping. It is a self-contained fragment — leading comma
// included — so a caller appends it directly and cannot mis-place a quote.
inline std::string TrackingStatusJsonTail(int32_t status) {
    std::string json = ",\"tracking\":";
    json += std::to_string(status);
    json += ",\"trackingName\":\"";
    json += TrackingStatusName(status);
    json += "\"";
    return json;
}

// The per-reason tally across the poses seen so far, keyed by pinned name:
// the vendor's *per-pose rejection reasons* aggregated, so the UI can show
// which reasons the scan is accumulating rather than only the latest one.
// `counts` holds one entry per declared status (index = status value). Like
// the tail above it is self-contained, leading comma included.
inline std::string TrackingCountsJsonTail(const uint64_t* counts, size_t count) {
    std::string json = ",\"trackingCounts\":{";
    for (size_t i = 0; i < count; ++i) {
        if (i != 0) json += ",";
        json += "\"";
        json += TrackingStatusName(static_cast<int32_t>(i));
        json += "\":";
        json += std::to_string(counts[i]);
    }
    json += "}";
    return json;
}

}  // namespace openscanner::tracking
