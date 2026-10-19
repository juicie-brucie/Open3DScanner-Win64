// Per-tick latency instrumentation (row #111). Measurement only: it times the
// existing operations and never changes what they are, their order, or the
// cadence. The hardware baseline is #112.
//
// Per tick it accumulates four segments — pose, the publish getter, our
// post-processing + encode, and the broadcast — plus the realized tick period
// and the cloud frame's points/bytes. A `[perf] window` line is printed every
// 5 s (the data must survive a teardown that can hang, #89), with a cumulative
// summary at teardown.
//
// No allocation on the timing path: SegTimer is one steady_clock pair into a
// caller-owned int64; samples land in fixed arrays. The percentile scratch is a
// stack array at flush time only.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace openscanner::perf {

enum Segment : int { kPose = 0, kGetter = 1, kPostproc = 2, kBroadcast = 3, kSegmentCount = 4 };

inline const char* SegmentName(int segment) {
    switch (segment) {
        case kPose: return "pose";
        case kGetter: return "getter";
        case kPostproc: return "post";
        case kBroadcast: return "bcast";
        default: return "?";
    }
}

inline int64_t UsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
}

// One steady_clock pair per scope, accumulating into a caller-owned slot.
class SegTimer {
public:
    explicit SegTimer(int64_t* acc) : acc_(acc), start_(std::chrono::steady_clock::now()) {}
    ~SegTimer() { Stop(); }
    // End the timed scope early (the caller may need the rest of the block
    // untimed, e.g. an operation that has its own segment).
    void Stop() {
        if (acc_ == nullptr) return;
        *acc_ += UsSince(start_);
        acc_ = nullptr;
    }
    SegTimer(const SegTimer&) = delete;
    SegTimer& operator=(const SegTimer&) = delete;

private:
    int64_t* acc_;
    std::chrono::steady_clock::time_point start_;
};

class TickPerf {
public:
    static constexpr int kCapacity = 512;        // > one 5 s window at 10 Hz
    static constexpr int kWindowMs = 5000;

    // Default on for the baseline; OPENSCANNER_TICK_PERF=0 disables it.
    TickPerf() {
        const char* v = std::getenv("OPENSCANNER_TICK_PERF");
        enabled_ = !(v != nullptr && v[0] == '0' && v[1] == '\0');
    }

    bool enabled() const { return enabled_; }

    // Call at the very top of the tick: the realized period is start-to-start.
    void BeginTick() {
        if (!enabled_) return;
        const auto now = std::chrono::steady_clock::now();
        if (have_last_) {
            Push(periods_, period_n_, std::chrono::duration_cast<std::chrono::microseconds>(now - last_).count());
        }
        last_ = now;
        have_last_ = true;
        if (!flush_started_) {
            flush_start_ = now;
            flush_started_ = true;
        }
    }

    // Call once at the end of the tick. Microseconds per segment.
    void EndTick(int64_t pose_us, int64_t getter_us, int64_t post_us, int64_t broadcast_us, uint32_t points,
                 uint64_t bytes) {
        if (!enabled_) return;
        const int64_t values[kSegmentCount] = {pose_us, getter_us, post_us, broadcast_us};
        for (int s = 0; s < kSegmentCount; ++s) {
            Push(samples_[s], counts_[s], values[s]);
            cumulative_us_[s] += values[s];
            if (values[s] > max_us_[s]) max_us_[s] = values[s];
        }
        Push(points_, points_n_, static_cast<int64_t>(points));
        Push(bytes_, bytes_n_, static_cast<int64_t>(bytes));
        ++ticks_;
        cumulative_points_ += points;
        cumulative_bytes_ += bytes;
        MaybeFlush();
    }

    // Teardown: flush any remaining window, then a cumulative summary.
    void Summary() {
        if (!enabled_) return;
        Flush("final");
        if (ticks_ == 0) return;
        std::printf("[perf] summary ticks=%lld mean_points=%.1f mean_bytes=%.1f",
                    static_cast<long long>(ticks_),
                    static_cast<double>(cumulative_points_) / ticks_,
                    static_cast<double>(cumulative_bytes_) / ticks_);
        for (int s = 0; s < kSegmentCount; ++s) {
            std::printf(" %s_mean=%.2fms %s_max=%.2fms", SegmentName(s),
                        static_cast<double>(cumulative_us_[s]) / ticks_ / 1000.0, SegmentName(s),
                        static_cast<double>(max_us_[s]) / 1000.0);
        }
        std::printf("\n");
        std::fflush(stdout);
    }

private:
    void MaybeFlush() {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - flush_start_).count();
        if (elapsed_ms < kWindowMs) return;
        Flush("window");
        flush_start_ = now;
    }

    void Flush(const char* tag) {
        if (period_n_ == 0 && counts_[kPose] == 0) return;
        const double window_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - flush_start_).count();
        const double mean_period_us = period_n_ > 0 ? static_cast<double>(Sum(periods_, period_n_)) / period_n_ : 0.0;
        const double rate = mean_period_us > 0 ? 1e6 / mean_period_us : 0.0;
        const double mean_points = points_n_ > 0 ? static_cast<double>(Sum(points_, points_n_)) / points_n_ : 0.0;
        const double mean_bytes = bytes_n_ > 0 ? static_cast<double>(Sum(bytes_, bytes_n_)) / bytes_n_ : 0.0;

        std::printf("[perf] %s %.1fs n=%d rate=%.1f/s points=%.1f bytes=%.1f", tag, window_s, period_n_, rate,
                    mean_points, mean_bytes);
        for (int s = 0; s < kSegmentCount; ++s) {
            std::printf(" | %s p50=%.2f p95=%.2f max=%.2f ms", SegmentName(s),
                        Percentile(samples_[s], counts_[s], 50) / 1000.0,
                        Percentile(samples_[s], counts_[s], 95) / 1000.0,
                        Percentile(samples_[s], counts_[s], 100) / 1000.0);
        }
        std::printf(" | tick p50=%.2f p95=%.2f max=%.2f ms\n", Percentile(periods_, period_n_, 50) / 1000.0,
                    Percentile(periods_, period_n_, 95) / 1000.0, Percentile(periods_, period_n_, 100) / 1000.0);
        std::fflush(stdout);

        for (int s = 0; s < kSegmentCount; ++s) counts_[s] = 0;
        period_n_ = points_n_ = bytes_n_ = 0;
    }

    static void Push(int64_t* buffer, int& count, int64_t value) {
        if (count < kCapacity) buffer[count++] = value;
    }

    static int64_t Sum(const int64_t* buffer, int count) {
        int64_t total = 0;
        for (int i = 0; i < count; ++i) total += buffer[i];
        return total;
    }

    // Nearest-rank percentile (p in percent); sorted copy on the stack, so the
    // flush path does not allocate.
    static int64_t Percentile(const int64_t* values, int count, int percent) {
        if (count <= 0) return 0;
        int64_t scratch[kCapacity];
        std::copy(values, values + count, scratch);
        std::sort(scratch, scratch + count);
        int index = (percent * count + 99) / 100 - 1;
        if (index < 0) index = 0;
        if (index >= count) index = count - 1;
        return scratch[index];
    }

    std::chrono::steady_clock::time_point last_{};
    std::chrono::steady_clock::time_point flush_start_{};
    bool have_last_ = false;
    bool flush_started_ = false;

    int64_t samples_[kSegmentCount][kCapacity] = {};
    int counts_[kSegmentCount] = {};
    int64_t periods_[kCapacity] = {};
    int period_n_ = 0;
    int64_t points_[kCapacity] = {};
    int points_n_ = 0;
    int64_t bytes_[kCapacity] = {};
    int bytes_n_ = 0;

    long long ticks_ = 0;
    long long cumulative_points_ = 0;
    long long cumulative_bytes_ = 0;
    int64_t cumulative_us_[kSegmentCount] = {};
    int64_t max_us_[kSegmentCount] = {};
    bool enabled_ = true;
};

}  // namespace openscanner::perf
