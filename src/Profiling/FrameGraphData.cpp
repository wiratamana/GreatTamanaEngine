#include "FrameGraphData.h"

#include <algorithm>

namespace gte::Profiling {

std::vector<FrameGraphPoint> BuildFrameGraphPoints(const FrameProfiler& profiler)
{
    std::vector<FrameGraphPoint> points;
    points.reserve(profiler.HistoryCount());

    for (std::size_t i = 0; i < profiler.HistoryCount(); ++i) {
        const FrameSummary& frame = profiler.HistoryAt(i);
        points.push_back({ frame.frameIndex, frame.cpuMilliseconds, frame.memory });
    }
    return points;
}

FrameGraphRange ComputeCpuMillisecondsRange(std::span<const FrameGraphPoint> points)
{
    FrameGraphRange range;
    if (points.empty()) {
        return range;
    }

    range.hasData = true;
    range.minMilliseconds = points[0].cpuMilliseconds;
    range.maxMilliseconds = points[0].cpuMilliseconds;
    for (const FrameGraphPoint& point : points.subspan(1)) {
        range.minMilliseconds = std::min(range.minMilliseconds, point.cpuMilliseconds);
        range.maxMilliseconds = std::max(range.maxMilliseconds, point.cpuMilliseconds);
    }
    return range;
}

MemoryBytesRange ComputeMemoryBytesRange(std::span<const FrameGraphPoint> points)
{
    MemoryBytesRange range;
    for (const FrameGraphPoint& point : points) {
        if (point.memory.status != SampleStatus::Present) {
            continue;
        }
        if (!range.hasData) {
            range.hasData = true;
            range.minBytes = point.memory.totalBytes;
            range.maxBytes = point.memory.totalBytes;
        } else {
            range.minBytes = std::min(range.minBytes, point.memory.totalBytes);
            range.maxBytes = std::max(range.maxBytes, point.memory.totalBytes);
        }
    }
    return range;
}

} // namespace gte::Profiling
