// Hardware-free smoke test for the vendor per-frame tracking verdict's
// pinned-name rendering (row #91; see tracking_status.h). It guards the
// contract with the header: if recon ever renumbers ObScanTrackingStatus,
// or the name table drifts from the UI's vocabulary, this fails.
//
// Not a hardware test: it exercises only the mapping and the status-JSON
// fragment, so it runs anywhere the build runs.
#include "tracking_status.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace tracking = openscanner::tracking;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "[FAIL] %s\n", what.c_str());
    ++g_failures;
}

// One row per declared ObScanTrackingStatus value: the enum name and the
// pinned label tracking_status.h must render. The enum is the contract
// (obscan_api.h); this table is the cross-check that the two agree.
struct Expected {
    int32_t value;
    const char* name;
};

const Expected kDeclared[] = {
    {OB_SCAN_TRACKING_SUCCESS, "SUCCESS"},
    {OB_SCAN_TRACKING_LOST, "LOST"},
    {OB_SCAN_TRACKING_AMBIGUOUS_PLANE, "AMBIGUOUS_PLANE"},
    {OB_SCAN_TRACKING_FAILURE_INSUFF_VALID_POINTS, "FAILURE_INSUFF_VALID_POINTS"},
    {OB_SCAN_TRACKING_FAILURE_TEXTURELESS, "FAILURE_TEXTURELESS"},
    {OB_SCAN_TRACKING_FAILURE_MARKERLESS, "FAILURE_MARKERLESS"},
    {OB_SCAN_TRACKING_FAILURE_MOVING_TOO_FAST, "FAILURE_MOVING_TOO_FAST"},
};

void CheckDeclaredNames() {
    std::set<std::string> seen;
    for (const Expected& e : kDeclared) {
        const std::string got = tracking::TrackingStatusName(e.value);
        Check(got == e.name, "value " + std::to_string(e.value) + " -> " + got + " (want " + e.name + ")");
        Check(seen.insert(got).second, "duplicate name " + got);
    }
}

void CheckOutOfRangeIsUnknown() {
    // The vendor's own lookup returns "UNKNOWN" out of range; the UI carries
    // the sentinel -1 before a sample arrives. Both must not be mislabelled.
    Check(std::string(tracking::TrackingStatusName(-1)) == "UNKNOWN", "-1 -> UNKNOWN");
    Check(std::string(tracking::TrackingStatusName(7)) == "UNKNOWN", "7 -> UNKNOWN");
    Check(std::string(tracking::TrackingStatusName(1000)) == "UNKNOWN", "1000 -> UNKNOWN");
}

void CheckJsonFragment() {
    // Assemble a status object the same way BuildStatusJson does (the tails are
    // self-contained, leading comma included), so a mis-placed quote at the
    // seam is caught here and not only on a live client.
    const int32_t value = OB_SCAN_TRACKING_FAILURE_MARKERLESS;
    uint64_t counts[tracking::kTrackingStatusCount] = {};
    counts[OB_SCAN_TRACKING_SUCCESS] = 120;
    counts[OB_SCAN_TRACKING_LOST] = 3;
    counts[OB_SCAN_TRACKING_FAILURE_MARKERLESS] = 4;

    const std::string status = "{\"type\":\"status\",\"fovControllable\":true" +
                               tracking::TrackingStatusJsonTail(value) +
                               tracking::TrackingCountsJsonTail(counts, tracking::kTrackingStatusCount) + "}";

    Check(status.find(",\"tracking\":" + std::to_string(value)) != std::string::npos,
          "raw value present");
    Check(status.find(",\"trackingName\":\"FAILURE_MARKERLESS\"") != std::string::npos,
          "pinned name present");
    // The per-reason tally: keyed by pinned name, one entry per declared value.
    Check(status.find(",\"trackingCounts\":{\"SUCCESS\":120,\"LOST\":3,") != std::string::npos,
          "counts keyed by pinned name, in enum order");
    Check(status.find("\"FAILURE_MARKERLESS\":4") != std::string::npos, "counts carry the markerless tally");
    Check(status.find("\"AMBIGUOUS_PLANE\":0") != std::string::npos, "zero-count reasons present");
    Check(status.rfind("\"FAILURE_MOVING_TOO_FAST\":0}") != std::string::npos, "counts end at the last declared reason");
    // The bug this guards: a `,"` prefix before a fragment's own leading quote
    // produced `,""tracking"` — invalid JSON. Catch the empty-string seam and
    // any unbalanced quote count.
    Check(status.find("\"\"") == std::string::npos, "no empty-string seam");
    size_t quotes = 0;
    for (char c : status) quotes += (c == '"') ? 1 : 0;
    Check(quotes % 2 == 0, "balanced quotes");

    const std::string none = tracking::TrackingStatusJsonTail(-1);
    Check(none.find(",\"tracking\":-1") != std::string::npos, "no-sample raw is -1");
    Check(none.find(",\"trackingName\":\"UNKNOWN\"") != std::string::npos, "no-sample name is UNKNOWN");
}

}  // namespace

int main() {
    CheckDeclaredNames();
    CheckOutOfRangeIsUnknown();
    CheckJsonFragment();

    if (g_failures != 0) {
        std::fprintf(stderr, "[tracking_status_test] %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[tracking_status_test] PASS: %zu declared values named, out-of-range -> UNKNOWN, status+counts JSON OK\n",
                sizeof(kDeclared) / sizeof(kDeclared[0]));
    return 0;
}
