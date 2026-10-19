// Smoke test for the per-call bound protocol (row #89; see
// bounded_watchdog.h).
//
// Deterministic by construction: the watchdog is built with run_thread=false
// and a clock the test controls, and the test drives PollOnce() itself. There
// is no sleep and no wall-clock race, so a loaded CI runner cannot change the
// verdict. The threaded loop is a thin glue around the same PollOnce().
#include "bounded_watchdog.h"

#include <atomic>
#include <cstdio>

namespace bounded = openscanner::bounded;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (condition) return;
    std::fprintf(stderr, "[FAIL] %s\n", what);
    ++g_failures;
}

constexpr int kBoundMs = 100;
constexpr int kGraceMs = 50;
// Fires at arm_time + kBoundMs + kGraceMs.

// A manual clock: the test owns "now".
struct Fixture {
    int64_t now = 1000;
    std::atomic<int> fires{0};
    bounded::Watchdog watchdog;
    Fixture()
        : watchdog([this] { fires.fetch_add(1); }, kGraceMs, /*poll_ms=*/10, [this] { return now; },
                   /*run_thread=*/false) {}
};
void CheckDisarmedNeverFires() {
    Fixture f;
    f.watchdog.Arm(kBoundMs);
    Check(f.watchdog.armed(), "arm() reports the bound is live");
    f.watchdog.Disarm();
    Check(!f.watchdog.armed(), "disarm() clears the bound");
    f.now = 1'000'000;  // far past any deadline
    Check(!f.watchdog.PollOnce(), "a disarmed bound never fires");
    Check(f.fires.load() == 0, "on_fire did not run for a disarmed bound");
}

void CheckFiresOnlyPastTheGrace() {
    Fixture f;
    f.now = 1000;
    f.watchdog.Arm(kBoundMs);  // deadline = 1100, fire threshold = 1150
    Check(f.watchdog.armed(), "bound armed");

    f.now = 1099;
    Check(!f.watchdog.PollOnce(), "does not fire before the deadline");
    f.now = 1149;  // one ms inside the grace
    Check(!f.watchdog.PollOnce(), "does not fire inside the grace");
    Check(f.fires.load() == 0, "no fire inside the grace");

    f.now = 1150;  // exactly deadline + grace
    Check(f.watchdog.PollOnce(), "fires once the grace has passed");
    Check(f.fires.load() == 1, "on_fire ran exactly once");
    Check(!f.watchdog.armed(), "a fired bound is consumed");

    f.now = 9'000'000;
    Check(!f.watchdog.PollOnce(), "a consumed bound does not re-fire");
    Check(f.fires.load() == 1, "still exactly one fire");
}

void CheckRearmIsANewBound() {
    Fixture f;
    f.now = 1000;
    f.watchdog.Arm(kBoundMs);
    f.watchdog.Disarm();
    Check(!f.watchdog.fired(), "a disarmed bound did not fire");

    f.now = 2000;
    f.watchdog.Arm(kBoundMs);  // deadline = 2100, fire threshold = 2150
    f.now = 2149;
    Check(!f.watchdog.PollOnce(), "the fresh bound does not fire inside its grace");
    f.now = 2150;
    Check(f.watchdog.PollOnce(), "a re-armed bound fires on the stalled call");
    Check(f.fires.load() == 1, "exactly one fire across both bounds");
}

void CheckLateCompletionInsideTheGraceIsSilent() {
    Fixture f;
    f.now = 3000;
    f.watchdog.Arm(kBoundMs);  // deadline = 3100, fire threshold = 3150
    f.now = 3120;              // past the deadline, inside the grace
    f.watchdog.Disarm();
    f.now = 3149;
    Check(!f.watchdog.PollOnce(), "a late-but-in-grace completion prevents the fire");
    Check(f.fires.load() == 0, "no fire after a late completion");
}

}  // namespace

int main() {
    CheckDisarmedNeverFires();
    CheckFiresOnlyPastTheGrace();
    CheckRearmIsANewBound();
    CheckLateCompletionInsideTheGraceIsSilent();

    if (g_failures != 0) {
        std::fprintf(stderr, "[bounded_watchdog_test] %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[bounded_watchdog_test] PASS: deterministic — disarmed never fires, fires only past the grace, "
                "a fired bound is consumed, re-arm is a fresh bound\n");
    return 0;
}
