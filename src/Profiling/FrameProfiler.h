#pragma once

#include "ProfilingTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gte::Profiling {

// The single, always-compiled (regardless of GTE_ENABLE_PROFILER - see
// AGENTS.md, "Profiling") owner of the in-progress frame's sample plus a
// fixed-capacity ring buffer of completed ones - the Profiling module's
// whole data model (PROFILER_STRATEGY_v2.md, Phase 0).
//
// A Meyers singleton (Instance()) rather than an object threaded by
// reference through every layer (Application -> Game -> RenderSystem ->
// ...): profiling data is inherently process-wide/per-frame, exactly like
// Unity's own static Profiler.BeginSample()/EndSample() API, and this
// engine is explicitly single-threaded (see GpuMemoryTracker's own class
// comment) so there is no thread-safety concern a singleton would
// otherwise raise - see PROFILER_STRATEGY_v2.md's own "no multi-threaded/
// job-system-aware profiling infrastructure" scope refusal.
//
// Compiling this class in UNCONDITIONALLY (unlike ScopeTimer's own body,
// which IS gated behind GTE_ENABLE_PROFILER - see ScopeTimer.h) keeps it
// available/testable even in a build configured with
// GTE_ENABLE_PROFILER=OFF - the same "the class stays available/testable
// even when its production call site is gated off" precedent
// SdlMemoryTracker already established (see AGENTS.md, "CPU Dependency
// Memory Tracking", and src/Memory/SdlMemoryTracker.h).
//
// Not thread-safe (matches the rest of this single-threaded engine, same
// as GpuMemoryTracker).
class FrameProfiler {
public:
    static FrameProfiler& Instance() noexcept;

    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;
    FrameProfiler(FrameProfiler&&) = delete;
    FrameProfiler& operator=(FrameProfiler&&) = delete;

    // The runtime half of the two-layer on/off switch (see
    // PROFILER_STRATEGY_v2.md, Phase 0b) - defaults to true. While false,
    // BeginFrame()/EndFrame()/RecordCpuScope()/SetGpuPassSample()/
    // SetMemorySnapshot() all become true no-ops (no clock read, no ring
    // buffer write at all) - this is what a future "Profiler" panel's
    // pause toggle and a future benchmark-mode CLI flag both drive.
    void SetCaptureEnabled(bool enabled) noexcept { m_captureEnabled = enabled; }
    bool IsCaptureEnabled() const noexcept { return m_captureEnabled; }

    // Call once per frame, bracketing the WHOLE frame (see
    // Application::Run()) - starts a fresh in-progress FrameSample and
    // records the wall-clock start time. A no-op (frame count does not
    // advance) while capture is disabled.
    void BeginFrame() noexcept;

    // Finishes the in-progress frame: records its total CPU wall-clock
    // duration, pushes it into the ring buffer (overwriting the oldest
    // entry once full), and advances the completed-frame count. A no-op
    // (nothing pushed, count does not advance) while capture is disabled,
    // or if BeginFrame() was never called for this frame.
    void EndFrame() noexcept;

    // Adds one CPU scope sample's duration to the CURRENT in-progress
    // frame's flat, name-keyed aggregation table (see CpuScopeSample) -
    // called from ScopeTimer's destructor; not meant to be called directly
    // by ordinary engine code (use GTE_PROFILE_SCOPE(name) instead - see
    // ScopeTimer.h). `name` MUST be a string literal/static-storage-
    // duration pointer. A no-op while capture is disabled or outside a
    // BeginFrame()/EndFrame() bracket. Silently drops the sample (rather
    // than allocating) if this frame already has kMaxCpuScopesPerFrame
    // distinct scope names.
    void RecordCpuScope(const char* name, double milliseconds) noexcept;

    // Records one named GPU pass's measurement for the current in-progress
    // frame. Not wired to anything real yet as of Phase 0/1 - see
    // ProfilingTypes.h's own comment on GpuSampleStatus - but the storage/
    // API already exists so a future Phase 4 (GPU timestamp queries)/
    // Phase 3 (draw-call and triangle counts) has somewhere correct to
    // write into. `status` must be GpuSampleStatus::Present for
    // milliseconds/drawCallCount/triangleCount to be meaningful.
    void SetGpuPassSample(GpuPass pass, GpuSampleStatus status, double milliseconds = 0.0,
        std::uint32_t drawCallCount = 0, std::uint32_t triangleCount = 0) noexcept;

    // Records this frame's GPU memory snapshot. Not wired to anything real
    // yet as of Phase 0/1 - a future Phase 5 (GPU memory usage over time)
    // is what actually calls this every frame with
    // Renderer::GetMemoryTotals() reshaped into a MemorySnapshot.
    void SetMemorySnapshot(const MemorySnapshot& snapshot) noexcept;

    // How many frames EndFrame() has fully completed so far (i.e. the
    // frame index BeginFrame() will assign to the NEXT frame it starts).
    std::uint64_t CompletedFrameCount() const noexcept { return m_frameIndex; }

    // Ring buffer read access, oldest-to-newest: HistoryAt(0) is the oldest
    // still-retained frame, HistoryAt(HistoryCount() - 1) is the most
    // recently completed one. Precondition: indexFromOldest < HistoryCount()
    // - same "caller's responsibility, no bounds-checking" contract as
    // std::array::operator[] elsewhere in this codebase.
    std::size_t HistoryCount() const noexcept { return m_historyCount; }
    const FrameSample& HistoryAt(std::size_t indexFromOldest) const noexcept;

    // The most recently completed frame, or a default-constructed
    // (all-absent/all-zero) FrameSample if none has completed yet -
    // convenience for a live "current" display (a future Editor Profiler
    // panel) that doesn't want to special-case an empty history itself.
    const FrameSample& LastCompletedFrame() const noexcept;

    // Test-only: discards all history and resets the frame index/in-
    // progress state back to how a freshly-constructed FrameProfiler would
    // look, WITHOUT touching the runtime capture-enabled flag. Since
    // Instance() is a process-wide singleton, tests must not assume a
    // pristine state - this exists purely so
    // tests/Profiling/FrameProfilerTests.cpp/ScopeTimerTests.cpp can start
    // each test case from a known baseline, mirroring the "capture
    // LiveBytes()/LiveAllocationCount() BEFORE each test's own calls and
    // assert on the DELTA" convention AGENTS.md documents for
    // SdlMemoryTracker/ImGuiMemoryTracker - resetting outright is simpler
    // and just as safe here, since nothing else in the engine depends on
    // FrameProfiler's history surviving across test cases.
    void ResetForTesting() noexcept;

private:
    FrameProfiler() = default;

    bool m_captureEnabled = true;
    bool m_frameInProgress = false;
    std::uint64_t m_frameIndex = 0;
    std::uint64_t m_frameStartTicks = 0;

    FrameSample m_current{};

    std::array<FrameSample, kMaxFrameHistory> m_history{};
    std::size_t m_historyHead = 0; // Next slot EndFrame() will write to.
    std::size_t m_historyCount = 0;
};

} // namespace gte::Profiling
