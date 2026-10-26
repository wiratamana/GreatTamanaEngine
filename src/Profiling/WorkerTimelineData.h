#pragma once

#include "FrameProfiler.h"
#include "ProfilingTypes.h"

#include <cstddef>
#include <vector>

namespace gte::Profiling {

// One job span on one worker row, relative to the start of its frame.
struct WorkerTimelinePoint {
    std::size_t workerIndex = 0;
    const char* name = nullptr; // interned, valid for the process lifetime
    double startMilliseconds = 0.0;
    double durationMilliseconds = 0.0;
};

// Worker events of one frame in recorded order. A job that started before the frame starts at 0.
std::vector<WorkerTimelinePoint> BuildWorkerTimelinePoints(const FrameRecord& frame);

// Number of different workers that appear in points.
std::size_t ComputeDistinctWorkerCount(const std::vector<WorkerTimelinePoint>& points);

} // namespace gte::Profiling
