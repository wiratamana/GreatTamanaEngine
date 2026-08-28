#pragma once

#include "../Profiling/WorkerTimelineData.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gte {

// Job System Phase 7 (Editor "Jobs" Panel -
// task_manager/job_system/JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md, Step 3.6):
// the panel's pure, ImGui-free data-shaping logic - follows
// ProfilerPanelData.h/MemoryPanelData.h's own template exactly (plain
// reshape/format functions, Tier-1-tested despite living under
// src/Editor/ - see AGENTS.md, "Testability & Regression Safety").
// Panels/JobsPanel.cpp is the thin ImGui-facing wrapper around these. No
// new engine-level tracking is introduced here - every function below
// reshapes/formats data Job System Phase 5
// (Profiling::BuildWorkerTimelinePoints(), src/Profiling/WorkerTimelineData.h)
// already produces.

// A simple, ImGui-free RGB color - callers (Panels/JobsPanel.cpp) pack this
// into whatever pixel/ImU32 format ImGui's own draw-list API expects; this
// header stays completely free of any ImGui dependency, same as every other
// *PanelData.h in this module.
struct JobColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// A deterministic, stable color for a given job name - the SAME name (by
// CONTENT, not pointer identity: a job's name may be a different
// string-literal address in a different translation unit despite identical
// text) always maps to the same color, on every call, with no persistent
// "seen names" table required - this is what lets every "SkinVertices"
// segment across every worker row in a frame (and across every frame)
// render with the same recognizable color, matching the visual language the
// campaign's own attached Unity Profiler Timeline screenshots use. A null
// `name` maps to a fixed neutral gray, never undefined behavior.
JobColor ColorForJobName(const char* name);

// "How many of the engine's real workers had at least one recorded job
// this frame" - the panel's own at-a-glance answer to "is this actually
// balanced" (see JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md, Step 2). Computed
// directly from `points` (Profiling::WorkerTimelinePoint - see
// WorkerTimelineData.h) plus the TOTAL number of real workers
// (gte::Jobs::JobSystem::Instance().WorkerCount(), passed in as
// `totalWorkerCount` so this function stays independent of the Job System
// module itself - it never calls JobSystem::Instance() directly).
struct WorkerUtilizationSummary {
    std::size_t workersWithAtLeastOneJob = 0;
    std::size_t totalWorkerCount = 0;
};

WorkerUtilizationSummary ComputeWorkerUtilizationSummary(
    const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t totalWorkerCount);

// Formats WorkerUtilizationSummary as e.g. "6 / 8 workers had at least one
// job this frame - 2 idle the whole frame" - pulled out as its own pure
// function so Panels/JobsPanel.cpp and its test share the exact same text.
// A totalWorkerCount of 0 (should never happen in practice - see
// gte::Jobs::JobSystem::WorkerCount()'s own "always >= 1" contract, but
// this function stays total/defensive regardless) is reported as "no
// workers" rather than a divide-by-zero-shaped 0/0 message.
std::string FormatWorkerUtilizationSummary(const WorkerUtilizationSummary& summary);

// Returns, in the SAME relative order they appear in `points` (never
// re-sorted - mirrors BuildWorkerTimelinePoints()'s own "never re-sorts"
// contract), only the entries whose workerIndex equals `workerIndex` - the
// per-row filter Panels/JobsPanel.cpp uses to decide which colored segments
// belong on a given worker's own timeline row.
std::vector<Profiling::WorkerTimelinePoint> PointsForWorker(
    const std::vector<Profiling::WorkerTimelinePoint>& points, std::size_t workerIndex);

// Whether GTE_PROFILE_JOB_SCOPE actually records anything anywhere in this
// build, i.e. whether GTE_ENABLE_PROFILER was ON when this translation unit
// was compiled - mirrors ProfilerPanelData.h's own
// kCpuScopeInstrumentationCompiledIn exactly (see AGENTS.md, "Profiling").
// A plain compile-time constant, not a runtime probe.
inline constexpr bool kJobTimingInstrumentationCompiledIn =
#if GTE_ENABLE_PROFILER
    true;
#else
    false;
#endif

// The Jobs timeline's own empty-state message - distinguishes "this build
// can never show worker job data, by design" (GTE_ENABLE_PROFILER=OFF, see
// kJobTimingInstrumentationCompiledIn above) from "genuinely no job samples
// were recorded yet this particular frame" (still true, transiently, even
// in a build where it's compiled in - e.g. no rigged model has animated
// yet, or Capture is currently disabled). Mirrors
// ProfilerPanelData.h::CpuScopeTableEmptyMessage()'s own precedent and its
// dedicated "message text matches the compile-time flag" regression test.
const char* JobsTimelineEmptyMessage();

// GPU Vertex Skinning campaign, Phase 7 (Editor Toggle & Profiling UX -
// task_manager/gpu_skinning/GPU_SKINNING_PHASE7_EDITOR_PROFILING_UX_STRATEGY_v1.md,
// Step 3.1). The "Jobs" panel's own CPU/GPU skinning-mode toggle displays
// and reasons about the mode purely as a `bool isGpuMode` here - deliberately
// NOT gte::AnimationSystem::SkinningMode itself, so this pure, ImGui-free
// header (like every other *PanelData.h in this module) never needs to
// depend on Game/AnimationSystem at all; Panels/JobsPanel.cpp (which already
// depends on Game for the toggle's actual read/write) is the one place that
// translates between the two.

// Plain display text for the toggle's own current value - "CPU (Job
// System)" or "GPU (Compute)".
const char* SkinningModeDisplayName(bool isGpuMode);

// The one-line "look over there instead" note this phase's own strategy
// document calls for (Step 3.1/3.3): flipping this toggle makes an entire
// category of already-existing profiling data appear/disappear elsewhere in
// the Editor (this SAME panel's own worker timeline below, for CPU mode; the
// "Render Graph" panel's "SkinModel:..." pass(es), for GPU mode) - neither
// panel needs (or gets) a new "N/A"/fabricated-value state for this (see
// AGENTS.md, "Profiling": the absence of a row/segment already IS the
// honest signal), but a user watching one panel who flips this toggle and
// sees data vanish from it deserves to be told where to look instead,
// rather than left to wonder if something broke.
const char* SkinningModeCrossReferenceHint(bool isGpuMode);

} // namespace gte
