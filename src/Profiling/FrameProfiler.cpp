#include "FrameProfiler.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstring>

namespace gte::Profiling {

namespace {

// Milliseconds elapsed between `startTicks` (a prior SDL_GetPerformanceCounter()
// reading) and now - see AGENTS.md/PROFILER_STRATEGY_v2.md Step 3a for why
// SDL's own performance counter is this module's ONE clock, rather than
// std::chrono or a platform-specific API: SDL is already the one platform-
// abstraction layer this engine depends on for everything else timing-
// adjacent, and is always linked regardless of GTE_ENABLE_EDITOR.
double ElapsedMilliseconds(std::uint64_t startTicks) noexcept
{
    const std::uint64_t nowTicks = SDL_GetPerformanceCounter();
    const std::uint64_t frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0) {
        return 0.0; // Defensive - never divide by a queried value blindly.
    }
    return static_cast<double>(nowTicks - startTicks) * 1000.0 / static_cast<double>(frequency);
}

} // namespace

FrameProfiler& FrameProfiler::Instance() noexcept
{
    static FrameProfiler instance;
    return instance;
}

void FrameProfiler::BeginFrame() noexcept
{
    if (!m_captureEnabled) {
        m_frameInProgress = false;
        return;
    }

    m_frameInProgress = true;
    m_frameStartTicks = SDL_GetPerformanceCounter();
    m_current = FrameSample{};
    m_current.frameIndex = m_frameIndex;
    m_current.frameStartTicks = m_frameStartTicks;

    // Phase 5 (Profiler Integration - Worker Timeline): reset the atomic
    // reservation counter for the new frame - see this member's own comment
    // in FrameProfiler.h.
    m_currentWorkerJobCount.store(0, std::memory_order_relaxed);
}

void FrameProfiler::EndFrame() noexcept
{
    if (!m_captureEnabled || !m_frameInProgress) {
        m_frameInProgress = false;
        return;
    }

    m_current.cpuFrameMilliseconds = ElapsedMilliseconds(m_frameStartTicks);

    // Phase 5: snapshot the atomic reservation counter into the plain,
    // copyable workerJobCount field - clamped to the fixed array's own
    // capacity, since RecordWorkerJobSample()'s fetch_add can legitimately
    // keep incrementing past kMaxWorkerJobSamplesPerFrame (every caller
    // still gets a well-defined reserved index to check against the
    // capacity, even once it's been exceeded - see that method's own
    // comment).
    m_current.workerJobCount = std::min<std::size_t>(
        m_currentWorkerJobCount.load(std::memory_order_acquire), kMaxWorkerJobSamplesPerFrame);

    m_history[m_historyHead] = m_current;
    m_historyHead = (m_historyHead + 1) % kMaxFrameHistory;
    if (m_historyCount < kMaxFrameHistory) {
        ++m_historyCount;
    }

    ++m_frameIndex;
    m_frameInProgress = false;
}

void FrameProfiler::RecordCpuScope(const char* name, double milliseconds) noexcept
{
    if (!m_captureEnabled || !m_frameInProgress || name == nullptr) {
        return;
    }

    for (std::size_t i = 0; i < m_current.cpuScopeCount; ++i) {
        CpuScopeSample& existing = m_current.cpuScopes[i];
        // Pointer equality first (the common case - the same call site's
        // string literal address, cheap), falling back to strcmp() since
        // two different translation units' identical literals are not
        // guaranteed to share an address.
        if (existing.name == name || std::strcmp(existing.name, name) == 0) {
            existing.totalMilliseconds += milliseconds;
            ++existing.callCount;
            return;
        }
    }

    if (m_current.cpuScopeCount < kMaxCpuScopesPerFrame) {
        CpuScopeSample& sample = m_current.cpuScopes[m_current.cpuScopeCount];
        sample.name = name;
        sample.totalMilliseconds = milliseconds;
        sample.callCount = 1;
        ++m_current.cpuScopeCount;
    }
    // Else: silently dropped - see this method's own header comment.
}

void FrameProfiler::SetGpuPassTiming(GpuPass pass, GpuSampleStatus status, double milliseconds) noexcept
{
    if (!m_captureEnabled || !m_frameInProgress) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(pass);
    if (index >= kGpuPassCount) {
        return;
    }

    GpuPassSample& sample = m_current.gpuPasses[index];
    sample.timingStatus = status;
    sample.milliseconds = milliseconds;
}

void FrameProfiler::SetGpuPassDrawStats(
    GpuPass pass, GpuSampleStatus status, std::uint32_t drawCallCount, std::uint32_t triangleCount) noexcept
{
    if (!m_captureEnabled || !m_frameInProgress) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(pass);
    if (index >= kGpuPassCount) {
        return;
    }

    GpuPassSample& sample = m_current.gpuPasses[index];
    sample.countStatus = status;
    sample.drawCallCount = drawCallCount;
    sample.triangleCount = triangleCount;
}

void FrameProfiler::SetMemorySnapshot(const MemorySnapshot& snapshot) noexcept
{
    if (!m_captureEnabled || !m_frameInProgress) {
        return;
    }
    m_current.memory = snapshot;
}

void FrameProfiler::RecordWorkerJobSample(
    std::size_t workerIndex, const char* name, double milliseconds, std::uint64_t startTicks) noexcept
{
    if (!m_captureEnabled || !m_frameInProgress || name == nullptr) {
        return;
    }

    // A single atomic fetch-and-increment reservation - see this method's
    // own header comment (FrameProfiler.h) for why this is the ONE method
    // on this class safe to call concurrently from many worker threads at
    // once: each caller gets its own, distinct, never-repeated index, so
    // concurrent writes below always land on DISJOINT array elements.
    const std::size_t index = m_currentWorkerJobCount.fetch_add(1, std::memory_order_acq_rel);
    if (index >= kMaxWorkerJobSamplesPerFrame) {
        return; // Overflow - silently dropped, mirroring RecordCpuScope()'s own overflow behavior.
    }

    WorkerJobSample& sample = m_current.workerJobs[index];
    sample.workerIndex = workerIndex;
    sample.name = name;
    sample.milliseconds = milliseconds;
    sample.startTicks = startTicks;
}

const FrameSample& FrameProfiler::HistoryAt(std::size_t indexFromOldest) const noexcept
{
    const std::size_t oldestPhysicalIndex = (m_historyHead + kMaxFrameHistory - m_historyCount) % kMaxFrameHistory;
    const std::size_t physicalIndex = (oldestPhysicalIndex + indexFromOldest) % kMaxFrameHistory;
    return m_history[physicalIndex];
}

const FrameSample& FrameProfiler::LastCompletedFrame() const noexcept
{
    static const FrameSample kEmpty{};
    if (m_historyCount == 0) {
        return kEmpty;
    }
    return HistoryAt(m_historyCount - 1);
}

void FrameProfiler::ResetForTesting() noexcept
{
    m_frameInProgress = false;
    m_frameIndex = 0;
    m_frameStartTicks = 0;
    m_current = FrameSample{};
    m_history = {};
    m_historyHead = 0;
    m_historyCount = 0;
    m_currentWorkerJobCount.store(0, std::memory_order_relaxed); // Phase 5.
}

void FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting(double milliseconds) noexcept
{
    if (m_historyCount == 0) {
        return;
    }

    // m_historyHead already points PAST the most recently written slot (see
    // EndFrame()'s own m_historyHead = (m_historyHead + 1) % kMaxFrameHistory
    // advance) - so the most recent entry sits one slot behind it, wrapping,
    // exactly mirroring HistoryAt(m_historyCount - 1)'s own physical-index
    // math without needing a non-const HistoryAt() overload.
    const std::size_t mostRecentPhysicalIndex = (m_historyHead + kMaxFrameHistory - 1) % kMaxFrameHistory;
    m_history[mostRecentPhysicalIndex].cpuFrameMilliseconds = milliseconds;
}

} // namespace gte::Profiling
