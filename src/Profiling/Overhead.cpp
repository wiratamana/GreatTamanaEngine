#include "Overhead.h"

#include "ProfilingClock.h"
#include "ScopeLog.h"

#include <cmath>
#include <memory>

namespace gte::Profiling {

double MeasureScopeEventCostNs(std::size_t iterations) noexcept
{
    if (iterations == 0) {
        return 0.0;
    }
    std::unique_ptr<ScopeLog> log;
    try {
        log = std::make_unique<ScopeLog>();
    } catch (...) {
        return 0.0;
    }

    FrameGate::MarkMainThread();
    constexpr std::size_t kBatch = 1024; // stays under the event buffer capacity
    constexpr NameId kName = 2;          // any non-reserved id; the log never resolves it

    log->BeginFrame(true);
    const std::uint64_t begin = GetProfilingPerformanceCounter();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (i % kBatch == 0) {
            log->EndFrame();
            log->BeginFrame(true);
        }
        const ScopeStart start = log->Begin(GetProfilingPerformanceCounter());
        log->End(start, kName, ScopeKind::Work, ViewSlot::Shared, GetProfilingPerformanceCounter());
    }
    const std::uint64_t end = GetProfilingPerformanceCounter();
    log->EndFrame();

    return end > begin ? static_cast<double>(end - begin) / static_cast<double>(iterations) : 0.0;
}

OverheadEstimate EstimateOverhead(double costPerEventNs, std::size_t eventCount, double frameMs) noexcept
{
    OverheadEstimate estimate;
    if (!(costPerEventNs > 0.0) || !std::isfinite(costPerEventNs)) {
        return estimate;
    }
    estimate.known = true;
    estimate.costPerEventNs = costPerEventNs;
    estimate.estimatedMs = static_cast<double>(eventCount) * costPerEventNs / 1.0e6;
    estimate.frameShare = frameMs > 0.0 ? estimate.estimatedMs / frameMs : 0.0;
    estimate.warn = estimate.frameShare > kOverheadWarnShare;
    return estimate;
}

} // namespace gte::Profiling
