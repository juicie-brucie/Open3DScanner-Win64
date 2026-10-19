// A bound a caller owns, enforced independently of the calling thread.
//
// Row #89: the per-call bound around the vendor's obscan_context_destroy()
// never fired on the rerun — the block was entered and then no diagnostic (not
// even the lock-free quit trace that follows it) appeared for 119 s, so a bound
// enforced only by the calling thread is not a bound once that thread is
// blocked behind a lock the hung vendor teardown holds.
//
// The protocol: Arm() before a call that may not return, Disarm() the moment
// the caller's wait completes. A watchdog thread, which the hung call cannot
// block, runs `on_fire` when an armed deadline passes (plus a grace, so a
// caller that is merely a little late wins the race and disarms first).
//
// The decision is split from the timing: `PollOnce()` evaluates the bound
// against the injected clock and fires at most once. The thread loop simply
// calls it; a test can construct the watchdog with `run_thread=false` and a
// manual clock and drive `PollOnce()` itself, so its assertions are about
// behaviour and never race a wall clock.
//
// `on_fire` must not depend on anything the hung call might hold — no stdout,
// no allocator. The shipping caller reports through the write(2) quit trace and
// then terminates the process with the timeout exit code.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>

namespace openscanner::bounded {

// Monotonic milliseconds, so the bound is unaffected by wall-clock jumps.
inline int64_t SteadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class Watchdog {
public:
    using Clock = std::function<int64_t()>;

    // `run_thread=false` is the deterministic seam: no thread is started and
    // the caller drives PollOnce() itself (with whatever clock it injected).
    Watchdog(std::function<void()> on_fire, int grace_ms, int poll_ms = 50, Clock clock = SteadyNowMs,
             bool run_thread = true)
        : on_fire_(std::move(on_fire)), grace_ms_(grace_ms), poll_ms_(poll_ms), clock_(std::move(clock)) {
        if (run_thread) thread_ = std::thread([this] { Run(); });
    }

    ~Watchdog() {
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    // Bound the call that is about to start: the watchdog fires if the caller
    // has not Disarm()ed within `timeout_ms` (+ grace). A fresh Arm() is a
    // fresh bound — it clears a previous fire.
    void Arm(int timeout_ms) {
        fired_.store(false, std::memory_order_release);
        deadline_ms_.store(clock_() + timeout_ms, std::memory_order_release);
    }

    // The caller's wait completed (returned or timed out); it owns the outcome.
    void Disarm() { deadline_ms_.store(0, std::memory_order_release); }

    // Evaluate the bound once against the current clock. Returns true iff it
    // fired. Called by the thread loop, and directly by a deterministic test.
    bool PollOnce() {
        const int64_t deadline = deadline_ms_.load(std::memory_order_acquire);
        if (deadline == 0) return false;  // disarmed or already consumed
        if (clock_() < deadline + grace_ms_) return false;
        fired_.store(true, std::memory_order_release);
        on_fire_();
        // Consume the bound: leaving it armed would re-fire on every poll. A
        // later Arm() is a fresh bound.
        deadline_ms_.store(0, std::memory_order_release);
        return true;
    }

    bool armed() const { return deadline_ms_.load(std::memory_order_acquire) != 0; }
    bool fired() const { return fired_.load(std::memory_order_acquire); }

private:
    void Run() {
        while (!stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
            if (stop_.load(std::memory_order_acquire)) return;
            PollOnce();
        }
    }

    std::function<void()> on_fire_;
    int grace_ms_;
    int poll_ms_;
    Clock clock_;
    std::atomic<int64_t> deadline_ms_{0};
    std::atomic<bool> stop_{false};
    std::atomic<bool> fired_{false};
    std::thread thread_;
};

}  // namespace openscanner::bounded
