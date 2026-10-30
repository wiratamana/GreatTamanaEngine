#include "FrameGraphData.h"

#include "ProfilingClock.h"

#include <algorithm>
#include <limits>

namespace gte::Profiling {

double CpuBusyMilliseconds(const FrameSummary& frame) noexcept
{
    return std::max(0.0, frame.cpuMilliseconds - TicksToMilliseconds(frame.blockedTicks));
}

double GpuSpanMilliseconds(const GpuViewMetric& span) noexcept
{
    return span.status == SampleStatus::Present ? static_cast<double>(span.milliseconds)
                                                : std::numeric_limits<double>::quiet_NaN();
}

double GraphAxisMaxMilliseconds(double largestVisibleMs) noexcept
{
    if (!(largestVisibleMs > 0.0)) {
        return kGraphGuideLowMs;
    }
    return std::max(kGraphGuideLowMs, largestVisibleMs * 1.1);
}

void BuildFrameGraphPoints(const FrameProfiler& profiler, std::vector<FrameGraphPoint>& out)
{
    out.clear();
    out.reserve(profiler.HistoryCount());
    for (std::size_t i = 0; i < profiler.HistoryCount(); ++i) {
        const FrameSummary& frame = profiler.HistoryAt(i);
        out.push_back({ frame.frameIndex, frame.cpuMilliseconds, frame.memory, CpuBusyMilliseconds(frame), frame.gpu.span });
    }
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
