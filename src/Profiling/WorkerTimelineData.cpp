#include "WorkerTimelineData.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>

namespace gte::Profiling {

std::vector<WorkerTimelinePoint> BuildWorkerTimelinePoints(const FrameSample& frame)
{
    std::vector<WorkerTimelinePoint> points;
    if (frame.workerJobCount == 0) {
        return points;
    }

    // SDL_GetPerformanceFrequency() is a fixed value for the life of the
    // process (see AGENTS.md, "Profiling") - reading it once here, rather
    // than per-sample, is purely an optimization, never a correctness
    // requirement.
    const std::uint64_t frequency = SDL_GetPerformanceFrequency();

    points.reserve(frame.workerJobCount);
    for (std::size_t i = 0; i < frame.workerJobCount; ++i) {
        const WorkerJobSample& sample = frame.workerJobs[i];

        WorkerTimelinePoint point;
        point.workerIndex = sample.workerIndex;
        point.name = sample.name;
        point.startMilliseconds = frequency != 0
            ? static_cast<double>(sample.startTicks - frame.frameStartTicks) * 1000.0
                / static_cast<double>(frequency)
            : 0.0;
        point.durationMilliseconds = sample.milliseconds;
        points.push_back(point);
    }

    return points;
}

std::size_t ComputeDistinctWorkerCount(const std::vector<WorkerTimelinePoint>& points)
{
    // A small, linear-scan "have we already counted this worker index"
    // check - perfectly fine here: the number of DISTINCT workers is always
    // small (bounded by gte::Jobs::JobSystem::WorkerCount(), typically a
    // handful of hardware threads), regardless of how many total points
    // (job samples) exist.
    std::vector<std::size_t> seen;
    for (const WorkerTimelinePoint& point : points) {
        if (std::find(seen.begin(), seen.end(), point.workerIndex) == seen.end()) {
            seen.push_back(point.workerIndex);
        }
    }
    return seen.size();
}

} // namespace gte::Profiling
