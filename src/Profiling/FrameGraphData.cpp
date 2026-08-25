#include "FrameGraphData.h"

#include <algorithm>

namespace gte::Profiling {

std::vector<FrameGraphPoint> BuildFrameGraphPoints(const FrameProfiler& profiler)
{
    std::vector<FrameGraphPoint> points;
    points.reserve(profiler.HistoryCount());

    for (std::size_t i = 0; i < profiler.HistoryCount(); ++i) {
        const FrameSample& frame = profiler.HistoryAt(i);

        FrameGraphPoint point;
        point.frameIndex = frame.frameIndex;
        point.cpuMilliseconds = frame.cpuFrameMilliseconds;
        point.gpuPasses = frame.gpuPasses;
        points.push_back(point);
    }

    return points;
}

FrameGraphRange ComputeCpuMillisecondsRange(std::span<const FrameGraphPoint> points)
{
    FrameGraphRange range;
    if (points.empty()) {
        return range; // hasData stays false.
    }

    double minMilliseconds = points[0].cpuMilliseconds;
    double maxMilliseconds = points[0].cpuMilliseconds;
    for (std::size_t i = 1; i < points.size(); ++i) {
        minMilliseconds = std::min(minMilliseconds, points[i].cpuMilliseconds);
        maxMilliseconds = std::max(maxMilliseconds, points[i].cpuMilliseconds);
    }

    range.hasData = true;
    range.minMilliseconds = minMilliseconds;
    range.maxMilliseconds = maxMilliseconds;
    return range;
}

FrameGraphRange ComputeGpuMillisecondsRange(std::span<const FrameGraphPoint> points, GpuPass pass)
{
    FrameGraphRange range;

    // Unconditional bounds check (active in Debug AND Release) - mirrors
    // FrameProfiler::SetGpuPassSample()'s own already-shipped handling of
    // exactly this same kind of invalid input (see FrameProfiler.cpp) rather
    // than a debug-only assert, which would leave a Release build's
    // out-of-range read exactly as unsafe as having no check at all. See
    // PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md, Step 3.2.
    const std::size_t index = static_cast<std::size_t>(pass);
    if (index >= kGpuPassCount) {
        return range; // hasData stays false.
    }

    bool found = false;
    double minMilliseconds = 0.0;
    double maxMilliseconds = 0.0;

    for (const FrameGraphPoint& point : points) {
        const GpuPassSample& sample = point.gpuPasses[index];
        // Only ever branch on `timingStatus` - never on whether
        // `milliseconds` happens to look like zero. An Absent/Unsupported
        // sample is excluded regardless of whatever numeric value is
        // sitting in its own milliseconds field (see this function's own
        // header comment).
        if (sample.timingStatus != GpuSampleStatus::Present) {
            continue;
        }

        if (!found) {
            minMilliseconds = sample.milliseconds;
            maxMilliseconds = sample.milliseconds;
            found = true;
        } else {
            minMilliseconds = std::min(minMilliseconds, sample.milliseconds);
            maxMilliseconds = std::max(maxMilliseconds, sample.milliseconds);
        }
    }

    range.hasData = found;
    range.minMilliseconds = minMilliseconds;
    range.maxMilliseconds = maxMilliseconds;
    return range;
}

} // namespace gte::Profiling
