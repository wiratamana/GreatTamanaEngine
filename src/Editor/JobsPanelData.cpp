#include "JobsPanelData.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace gte {

namespace {

// A small, deterministic FNV-1a-style hash over `name`'s own bytes - not
// cryptographic, not needed to be; only needed to spread distinct job names
// across a fixed palette below in a way that's stable across runs/machines
// (unlike, say, hashing the pointer VALUE itself, which would vary run to
// run and defeat the whole "same name -> same color, always" goal).
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

// A small, fixed palette of visually-distinct, reasonably saturated colors
// - a job name's hash picks one of these deterministically (below) rather
// than deriving an arbitrary RGB triple directly from the hash, which tends
// to produce muddy/indistinguishable colors for hash values that happen to
// land close together. A handful of entries is plenty: this engine's own
// Job System has only ever had a single named job kind in production as of
// Phase 7 ("SkinVertices" - see JOBSYSTEM_PHASE6_COMPLETION_REPORT.md), and
// even a future engine with a dozen distinct job kinds would still cycle
// through this palette perfectly reasonably.
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

    // Reuses the exact same "distinct worker indices present" logic Phase 5
    // already established (Profiling::ComputeDistinctWorkerCount()) rather
    // than re-deriving it here - this function's own value-add is purely
    // pairing that count against the ENGINE's total worker count, which
    // ComputeDistinctWorkerCount() deliberately has no knowledge of (see
    // that function's own header comment).
    const std::size_t distinctWorkersWithData = Profiling::ComputeDistinctWorkerCount(points);

    // Never report more "workers with a job" than the engine actually has -
    // a defensive clamp, not expected to ever trigger in practice (a
    // WorkerTimelinePoint's workerIndex should never exceed
    // JobSystem::WorkerCount() - 1), but keeps this function total/safe
    // even against a caller-supplied totalWorkerCount that happens to be
    // stale/smaller than what actually produced `points`.
    summary.workersWithAtLeastOneJob = std::min(distinctWorkersWithData, totalWorkerCount);
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

} // namespace gte
