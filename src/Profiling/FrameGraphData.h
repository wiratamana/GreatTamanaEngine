#pragma once

#include "FrameProfiler.h"
#include "ProfilingTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gte::Profiling {

// Plottable data of one retained frame. Pure reshape with no UI dependency.
struct FrameGraphPoint {
    std::uint64_t frameIndex = 0;
    double cpuMilliseconds = 0.0;
    MemorySnapshot memory{};
};

// Min and max of one series. Check hasData before plotting.
struct FrameGraphRange {
    bool hasData = false;
    double minMilliseconds = 0.0;
    double maxMilliseconds = 0.0;
};

// Retained history as points, oldest first. Reads finished frames only.
std::vector<FrameGraphPoint> BuildFrameGraphPoints(const FrameProfiler& profiler);

// CPU time range. hasData is false only for an empty span.
FrameGraphRange ComputeCpuMillisecondsRange(std::span<const FrameGraphPoint> points);

struct MemoryBytesRange {
    bool hasData = false;
    std::uint64_t minBytes = 0;
    std::uint64_t maxBytes = 0;
};

// Total GPU memory range over points whose status is Present. Other points are skipped.
MemoryBytesRange ComputeMemoryBytesRange(std::span<const FrameGraphPoint> points);

} // namespace gte::Profiling
