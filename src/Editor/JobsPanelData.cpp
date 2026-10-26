#include "JobsPanelData.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace gte {

namespace {

// FNV-1a over the name's bytes. Stable across runs, unlike a pointer value.
std::uint32_t HashJobName(const char* name)
{
    std::uint32_t hash = 2166136261u;
    if (name != nullptr) {
        for (const char* p = name; *p != '\0'; ++p) {
            hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(*p));
            hash *= 16777619u;
        }
    }
    return hash;
}

// Picks a palette entry from the name's hash, so the same name always gets the same color.
constexpr JobColor kPalette[] = {
    JobColor{ 0.90f, 0.45f, 0.45f }, // red
    JobColor{ 0.45f, 0.75f, 0.90f }, // blue
    JobColor{ 0.55f, 0.85f, 0.45f }, // green
    JobColor{ 0.95f, 0.75f, 0.30f }, // amber
    JobColor{ 0.75f, 0.55f, 0.90f }, // violet
    JobColor{ 0.35f, 0.85f, 0.75f }, // teal
    JobColor{ 0.95f, 0.55f, 0.75f }, // pink
    JobColor{ 0.70f, 0.70f, 0.40f }, // olive
};
constexpr std::size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

} // namespace

JobColor ColorForJobName(const char* name)
{
    if (name == nullptr || name[0] == '\0') {
        return JobColor{ 0.6f, 0.6f, 0.6f }; // Neutral gray - never undefined behavior on a null/empty name.
    }
    const std::uint32_t hash = HashJobName(name);
    return kPalette[hash % kPaletteSize];
}

WorkerUtilizationSummary ComputeWorkerUtilizationSummary(
    const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t totalWorkerCount)
{
    WorkerUtilizationSummary summary;
    summary.totalWorkerCount = totalWorkerCount;

    // Never report more busy workers than exist, even with a stale total.
    summary.workersWithAtLeastOneJob = std::min(Profiling::ComputeDistinctWorkerCount(points), totalWorkerCount);
    return summary;
}

std::string FormatWorkerUtilizationSummary(const WorkerUtilizationSummary& summary)
{
    if (summary.totalWorkerCount == 0) {
        return "No workers.";
    }

    const std::size_t idle = summary.totalWorkerCount - summary.workersWithAtLeastOneJob;

    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%zu / %zu workers had at least one job this frame - %zu idle the whole frame",
        summary.workersWithAtLeastOneJob, summary.totalWorkerCount, idle);
    return std::string(buffer);
}

std::vector<Profiling::WorkerTimelinePoint> PointsForWorker(
    const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t workerIndex)
{
    std::vector<Profiling::WorkerTimelinePoint> result;
    for (const Profiling::WorkerTimelinePoint& point : points) {
        if (point.workerIndex == workerIndex) {
            result.push_back(point);
        }
    }
    return result;
}

const char* JobsTimelineEmptyMessage()
{
    if (!kJobTimingInstrumentationCompiledIn) {
        return "Job timing instrumentation is compiled out (GTE_ENABLE_PROFILER=OFF) - "
               "every worker row below will always show entirely idle in this build.";
    }
    return "No job samples recorded yet this frame.";
}

const char* SkinningModeDisplayName(bool isGpuMode)
{
    return isGpuMode ? "GPU (Compute)" : "CPU (Job System)";
}

const char* SkinningModeCrossReferenceHint(bool isGpuMode)
{
    if (isGpuMode) {
        return "GPU mode: vertex skinning now runs as a compute pass instead - this panel's own "
               "worker timeline below will show no \"SkinVertices\" entries for a GPU-skinned "
               "model any more. See the \"Render Graph\" panel's own \"SkinModel:...\" pass(es) "
               "for its GPU timing instead.";
    }
    return "CPU mode: vertex skinning runs across the worker pool below (look for \"SkinVertices\" "
           "entries in the timeline). Switch to GPU to see that work move to a \"SkinModel:...\" "
           "compute pass in the \"Render Graph\" panel instead.";
}

} // namespace gte
