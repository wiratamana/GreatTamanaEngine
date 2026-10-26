#include "WorkerTimelineData.h"

#include "NameTable.h"
#include "ProfilingClock.h"

#include <algorithm>

namespace gte::Profiling {

std::vector<WorkerTimelinePoint> BuildWorkerTimelinePoints(const FrameRecord& frame)
{
    std::vector<WorkerTimelinePoint> points;
    const auto events = frame.workers.Events();
    points.reserve(events.size());

    const std::uint64_t frameStart = frame.summary.startTicks;
    for (const WorkerEvent& event : events) {
        WorkerTimelinePoint point;
        point.workerIndex = event.worker;
        point.name = ResolveName(event.name);
        point.startMilliseconds = event.startTicks > frameStart ? TicksToMilliseconds(event.startTicks - frameStart) : 0.0;
        point.durationMilliseconds = event.endTicks > event.startTicks ? TicksToMilliseconds(event.endTicks - event.startTicks) : 0.0;
        points.push_back(point);
    }
    return points;
}

std::size_t ComputeDistinctWorkerCount(const std::vector<WorkerTimelinePoint>& points)
{
    std::vector<std::size_t> seen;
    for (const WorkerTimelinePoint& point : points) {
        if (std::find(seen.begin(), seen.end(), point.workerIndex) == seen.end()) {
            seen.push_back(point.workerIndex);
        }
    }
    return seen.size();
}

} // namespace gte::Profiling
