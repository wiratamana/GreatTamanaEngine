#pragma once

#include "FrameProfiler.h"
#include "ProfilingTypes.h"

#include <cstddef>
#include <vector>

namespace gte::Profiling {

// Job System Phase 5 (Profiler Integration - Worker Timeline - see
// task_manager/job_system/JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md):
// the pure "one frame's raw WorkerJobSample log -> a per-worker timeline a
// future Editor panel (Phase 7's planned 'Jobs' panel) can render directly"
// reshape - mirrors FrameGraphData.h's own "always-compiled, ImGui-free
// reshape" precedent exactly (no GTE_ENABLE_EDITOR/GTE_ENABLE_PROFILER
// dependency at all - same tier as FrameProfiler.h/.cpp itself), so a future
// Phase 7 "Jobs" panel (and any future benchmark-mode consumer) reads
// through this one function rather than re-deriving the same reshape logic
// independently.

// One segment of a single worker's own timeline row, for ONE frame - a
// directly-plottable (startMilliseconds, durationMilliseconds) span, already
// relative to that frame's OWN start (FrameSample::frameStartTicks) - never
// a raw absolute tick count a future caller would otherwise have to
// re-derive the frame's own start from.
struct WorkerTimelinePoint {
    std::size_t workerIndex = 0;

    // Same string-literal/static-storage-duration convention as
    // WorkerJobSample::name (ProfilingTypes.h) - never owned/copied.
    const char* name = nullptr;

    // Offset from the frame's OWN start (FrameSample::frameStartTicks) -
    // never a raw tick count. Can legitimately be slightly negative-ish-
    // looking-if-misread as an unsigned subtraction underflow ONLY if a
    // worker job's own startTicks somehow predates frameStartTicks (should
    // never happen under normal operation, since every job is scheduled
    // strictly after BeginFrame() runs) - see BuildWorkerTimelinePoints()'s
    // own comment for how this is computed.
    double startMilliseconds = 0.0;
    double durationMilliseconds = 0.0;
};

// Reshapes ONE frame's worth of raw WorkerJobSample entries (see
// ProfilingTypes.h) into an ordered list of WorkerTimelinePoints, in the
// exact order FrameSample::workerJobs recorded them in (never re-sorted by
// this function) - each point's startMilliseconds computed relative to
// `frame.frameStartTicks` (the raw SDL_GetPerformanceCounter() reading
// BeginFrame() took to start `frame`) via SDL_GetPerformanceFrequency(),
// the same clock/units this whole module standardizes on (see AGENTS.md,
// "Profiling").
//
// Deliberately takes a single FrameSample (not the whole FrameProfiler) -
// a caller wanting "the last completed frame's timeline" passes
// `profiler.LastCompletedFrame()` directly; a caller wanting several
// frames' worth calls this once per FrameSample of interest, mirroring
// FrameGraphData.h's own "one call per unit of interest" shape rather than
// building a second, parallel "whole history" overload prematurely - no
// current consumer needs it (Phase 7's own planned panel only ever displays
// ONE frame's timeline at a time, per its own strategy document).
//
// Returns an empty vector whenever `frame.workerJobCount == 0` - never a
// special-cased default point.
std::vector<WorkerTimelinePoint> BuildWorkerTimelinePoints(const FrameSample& frame);

// How many DISTINCT worker indices appear anywhere in `points` - the "how
// many rows does this frame's timeline actually need real data for" count a
// future panel uses to decide how many worker rows to draw. This function
// only reports how many rows have REAL data for this frame; per Phase 7's
// own planned design, a future panel is responsible for still drawing an
// "entirely idle" row for every OTHER worker up through
// gte::Jobs::JobSystem::Instance().WorkerCount() - showing an idle worker is
// just as meaningful a signal as showing a busy one.
std::size_t ComputeDistinctWorkerCount(const std::vector<WorkerTimelinePoint>& points);

} // namespace gte::Profiling
