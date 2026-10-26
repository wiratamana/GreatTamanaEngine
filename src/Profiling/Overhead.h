#pragma once

// Estimated cost of the profiler itself: events recorded times the cost of one event, measured once.

#include <cstddef>
#include <cstdint>

namespace gte::Profiling {

// Estimated overhead above this share of the frame is reported as a warning.
inline constexpr double kOverheadWarnShare = 0.03;

// Iterations of the measuring loop. Large enough to average out clock granularity.
inline constexpr std::size_t kOverheadMeasureIterations = 20000;

// Nanoseconds per recorded scope, measured on a private log with the real clock. Marks the calling thread
// as the main thread, so call it once from there. Returns 0 when it could not run.
double MeasureScopeEventCostNs(std::size_t iterations = kOverheadMeasureIterations) noexcept;

struct OverheadEstimate {
    double costPerEventNs = 0.0;
    double estimatedMs = 0.0; // events * cost
    double frameShare = 0.0;  // estimatedMs / frameMs, 0 when the frame time is unknown
    bool warn = false;        // above kOverheadWarnShare
    bool known = false;       // false when no cost was measured
};

OverheadEstimate EstimateOverhead(double costPerEventNs, std::size_t eventCount, double frameMs) noexcept;

} // namespace gte::Profiling
