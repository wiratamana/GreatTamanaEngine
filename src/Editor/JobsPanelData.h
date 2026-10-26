#pragma once

#include "../Profiling/WorkerTimelineData.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gte {

// Pure data shaping and text formatting for the Jobs panel. No ImGui.

// Plain RGB color (0..1). The panel packs it into its own pixel format.
struct JobColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// Stable color for a job name, chosen by text content. A null or empty name gives a neutral gray.
JobColor ColorForJobName(const char* name);

// How many workers ran at least one job in the frame.
struct WorkerUtilizationSummary {
    std::size_t workersWithAtLeastOneJob = 0;
    std::size_t totalWorkerCount = 0;
};

// Counts distinct workers in points, never more than totalWorkerCount.
WorkerUtilizationSummary ComputeWorkerUtilizationSummary(
    const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t totalWorkerCount);

// "6 / 8 workers had at least one job this frame - 2 idle the whole frame", or "No workers.".
std::string FormatWorkerUtilizationSummary(const WorkerUtilizationSummary& summary);

// Points of one worker, keeping their order.
std::vector<Profiling::WorkerTimelinePoint> PointsForWorker(
    const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t workerIndex);

// True when GTE_ENABLE_PROFILER was on for this build.
inline constexpr bool kJobTimingInstrumentationCompiledIn =
#if GTE_ENABLE_PROFILER
    true;
#else
    false;
#endif

// Empty-timeline text. Says whether job data can ever appear in this build.
const char* JobsTimelineEmptyMessage();

// Display name of the skinning mode: "CPU (Job System)" or "GPU (Compute)".
const char* SkinningModeDisplayName(bool isGpuMode);

// Tooltip hint pointing at where the other mode's data shows up.
const char* SkinningModeCrossReferenceHint(bool isGpuMode);

} // namespace gte
