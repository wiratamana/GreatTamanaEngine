#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gte::Profiling {

// Fixed capacity of the per-frame CPU scope aggregation table (see
// FrameProfiler::RecordCpuScope()) - a handful of named systems today
// (Application::PollEvents, Game::Update, AnimationSystem::Update,
// RenderSystem::CollectRenderables/Draw, Renderer::RenderOffscreen x2,
// IEditorLayer::BuildUI, Renderer::Present - see PROFILER_STRATEGY_v2.md,
// Phase 1), nowhere near the kind of count that would ever need a growable
// container. A frame with more than this many DISTINCT scope names simply
// drops the overflow rather than allocating - see
// FrameProfiler::RecordCpuScope()'s own comment.
inline constexpr std::size_t kMaxCpuScopesPerFrame = 64;

// How many frames of history the ring buffer (FrameProfiler) keeps -
// enough for a frame-time graph (PROFILER_STRATEGY_v2.md, Phase 2) to show
// a few seconds' worth at a typical frame rate, without unbounded growth.
// Storage for this is allocated exactly once, as part of FrameProfiler's
// own static-duration singleton state - never per-frame (see
// PROFILER_STRATEGY_v2.md, Step 3a's "no heap allocation in the per-frame
// hot path" rule).
inline constexpr std::size_t kMaxFrameHistory = 300;

// One CPU-side named scope's aggregated cost FOR THE CURRENT FRAME ONLY -
// deliberately a FLAT (not a nested-tree) model: every ScopeTimer
// construction/destruction pair sharing the same `name`, no matter how
// deeply nested inside another scope, contributes to the SAME entry,
// summed. See PROFILER_STRATEGY_v2.md's Phase 0 "hierarchy vs. flat list"
// design decision for the full reasoning, and its own documented
// limitation: a scope that (directly or indirectly) calls itself within
// the same frame would have its self-time double-counted under this
// model - a known, accepted v1 limitation (no current call site
// recurses).
struct CpuScopeSample {
    // A string literal (or otherwise static-storage-duration) pointer -
    // never owned/copied, and never gated behind GTE_ENABLE_EDITOR (unlike
    // a GPU resource's cosmetic debug name - see AGENTS.md, "GPU Resource
    // Memory Tracking") because a scope name is the PRIMARY payload here,
    // needed in every build, including a future headless benchmark run
    // with no Editor compiled in at all. See ScopeTimer.h.
    const char* name = nullptr;
    double totalMilliseconds = 0.0;
    std::uint32_t callCount = 0;
};

// Whether a per-frame GPU-side measurement (a named pass's timing, or its
// draw-call/triangle count) actually has a real value this frame. Never
// collapse "didn't run" or "not supported on this device" into a bare
// numeric 0 - see PROFILER_STRATEGY_v2.md, Step 3a's tri-state rule and
// its own "no data this frame" constraint (Step 2.3). Not wired to
// anything real yet as of Phase 0/1 (Phase 4/5 are what actually call
// FrameProfiler::SetGpuPassSample()/SetMemorySnapshot() with a real
// value) - the storage/API exists now so those later phases have
// somewhere correct to write into without redesigning this data model.
enum class GpuSampleStatus : std::uint8_t {
    Absent,      // This pass simply didn't run this frame (e.g. a hidden Editor panel) - not measured, not zero.
    Present,     // A real measurement exists - read the sibling value(s).
    Unsupported, // The device/build can't produce this measurement at all (e.g. no GPU timestamp support).
};

// A small, FIXED, named enumeration of GPU passes (deliberately NOT an
// arbitrarily-nestable tree - see PROFILER_STRATEGY_v2.md, Phase 4) -
// today's engine has exactly this many distinct offscreen/swapchain
// recording passes per frame (see Application::Run()).
enum class GpuPass : std::uint8_t {
    GameView = 0,
    SceneView = 1,
    Present = 2,
};
inline constexpr std::size_t kGpuPassCount = 3;

// One named GPU pass's measurement for the current frame - see GpuPass/
// GpuSampleStatus above. Deliberately TWO INDEPENDENT tri-states, not one
// combined `status` (see PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md,
// Step 2.4 for the full reasoning this split fixes): Phase 3 (draw-call/
// triangle counts) produces REAL data for `drawCallCount`/`triangleCount`
// well before Phase 4 (GPU timestamp queries) exists to produce real data
// for `milliseconds` - a single shared `status` field would force Phase 3's
// own call site to falsely claim GPU timing was also measured this frame.
struct GpuPassSample {
    // Governs `milliseconds` ONLY. Stays GpuSampleStatus::Absent until a
    // future Phase 4 (GPU timestamp queries) actually measures a real value
    // for this pass this frame - Phase 3 (draw-call/triangle counts) never
    // touches this field.
    GpuSampleStatus timingStatus = GpuSampleStatus::Absent;
    double milliseconds = 0.0; // Only meaningful when timingStatus == Present.

    // Governs drawCallCount/triangleCount ONLY - Phase 3's own concern,
    // entirely independent of GPU timing. A pure CPU-side count of what was
    // queued via Submit()/FrameRecorder::Submit() this frame.
    GpuSampleStatus countStatus = GpuSampleStatus::Absent;
    std::uint32_t drawCallCount = 0; // Only meaningful when countStatus == Present.
    std::uint32_t triangleCount = 0; // Only meaningful when countStatus == Present.
};

// A plain, Vulkan-free copy of GpuMemoryTracker::Totals's shape
// (src/Renderer/Memory/GpuMemoryTracker.h) - deliberately NOT that type
// itself, so this always-compiled, Editor/Renderer-independent module
// (see PROFILER_STRATEGY_v2.md, Phase 0's own "no Vulkan, no ImGui, no
// Editor dependency at all" goal) never needs to include a single Vulkan
// header. Phase 5's actual per-frame sampling step is the one place that
// converts between the two, once it exists.
struct MemorySnapshot {
    GpuSampleStatus status = GpuSampleStatus::Absent;
    std::uint64_t totalBytes = 0;
    std::uint64_t bufferBytes = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t gpuOnlyBytes = 0;
    std::uint64_t cpuOnlyBytes = 0;
    std::uint64_t sharedBytes = 0;
    std::uint64_t bufferCount = 0;
    std::uint64_t textureCount = 0;
};

// Phase 5 (Profiler Integration - Worker Timeline - see
// task_manager/job_system/JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md):
// one job body's own recorded CPU scope, attributed to whichever Job System
// worker thread actually ran it - the per-worker-timeline analog of
// CpuScopeSample above. Deliberately a SEPARATE, additive list from
// cpuScopes (never merged into it): cpuScopes is a flat, name-keyed
// AGGREGATE with no per-call timing/ordering information (repeated calls to
// the same name are summed together) - exactly wrong for a worker TIMELINE,
// which needs to know WHEN, on WHICH worker, each individual scope ran, so
// it can be drawn as its own positioned, colored segment on that worker's
// own row (see Profiling::BuildWorkerTimelinePoints(), WorkerTimelineData.h).
struct WorkerJobSample {
    std::size_t workerIndex = 0;

    // Same string-literal/static-storage-duration convention as
    // CpuScopeSample::name above - never owned/copied. See JobScopeTimer.h.
    const char* name = nullptr;

    double milliseconds = 0.0;

    // The raw SDL_GetPerformanceCounter() reading (the SAME clock/units this
    // whole module standardizes on - see AGENTS.md, "Profiling") this scope
    // STARTED at - an ABSOLUTE tick count with no fixed epoch across
    // frames/machines, needed ONLY so a future reshape
    // (BuildWorkerTimelinePoints(), WorkerTimelineData.h) can compute this
    // sample's own frame-relative start offset from FrameSample::
    // frameStartTicks below; milliseconds alone (a DURATION) says nothing
    // about WHEN within the frame a scope ran.
    std::uint64_t startTicks = 0;
};

// Fixed capacity for the per-frame worker-job-sample log (see
// FrameProfiler::RecordWorkerJobSample()) - deliberately far larger than
// kMaxCpuScopesPerFrame above, since this is a raw per-CALL log (every
// GTE_PROFILE_JOB_SCOPE construction/destruction pair gets its OWN entry,
// never summed/deduplicated by name the way cpuScopes is) - a single
// Dispatch() call can already produce dozens of batch jobs, across every
// worker, in one frame. Still a small, fixed, generously-sized capacity
// (never a growable container) - the same convention this module already
// established for kMaxCpuScopesPerFrame/kMaxFrameHistory. A frame that
// genuinely produces more than this many worker job samples simply drops
// the overflow rather than allocating - see
// FrameProfiler::RecordWorkerJobSample()'s own comment.
inline constexpr std::size_t kMaxWorkerJobSamplesPerFrame = 1024;

// One whole frame's worth of profiling data - exactly what FrameProfiler's
// ring buffer stores one of, per frame. Every field is plain/POD and
// fixed-size (std::array, never a growable container) - see
// PROFILER_STRATEGY_v2.md, Step 3a's "no heap allocation in the per-frame
// hot path" rule.
struct FrameSample {
    std::uint64_t frameIndex = 0;
    double cpuFrameMilliseconds = 0.0;

    // The raw SDL_GetPerformanceCounter() reading BeginFrame() took to start
    // THIS frame - an ABSOLUTE tick count with no fixed epoch across
    // frames/machines; needed ONLY so a future reshape (see
    // Profiling::BuildWorkerTimelinePoints(), WorkerTimelineData.h) has a
    // stable "time zero" to compute each recorded WorkerJobSample's own
    // frame-relative start offset from (Phase 5) - never meant to be read
    // directly by ordinary calling code.
    std::uint64_t frameStartTicks = 0;

    std::array<CpuScopeSample, kMaxCpuScopesPerFrame> cpuScopes{};
    std::size_t cpuScopeCount = 0;

    std::array<GpuPassSample, kGpuPassCount> gpuPasses{};

    MemorySnapshot memory{};

    // Phase 5 (Profiler Integration - Worker Timeline): every job-body scope
    // recorded via GTE_PROFILE_JOB_SCOPE this frame, attributed to whichever
    // worker thread ran it - a raw, per-CALL log (never summed/deduplicated
    // by name like cpuScopes above), since a worker TIMELINE needs to know
    // WHEN and on WHICH worker each individual scope ran, not just an
    // aggregate total. Populated via FrameProfiler::RecordWorkerJobSample(),
    // the ONE thread-safe write path this module exposes - every other
    // field on this struct remains written exclusively from the main thread.
    std::array<WorkerJobSample, kMaxWorkerJobSamplesPerFrame> workerJobs{};
    std::size_t workerJobCount = 0;
};

} // namespace gte::Profiling
