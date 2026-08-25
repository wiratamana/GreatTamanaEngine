#include "FrameProfiler.h"

#include <SDL3/SDL_timer.h>

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
}

void FrameProfiler::EndFrame() noexcept
{
    if (!m_captureEnabled || !m_frameInProgress) {
        m_frameInProgress = false;
        return;
    }

    m_current.cpuFrameMilliseconds = ElapsedMilliseconds(m_frameStartTicks);

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

void FrameProfiler::SetGpuPassSample(GpuPass pass, GpuSampleStatus status, double milliseconds,
    std::uint32_t drawCallCount, std::uint32_t triangleCount) noexcept
{
    if (!m_captureEnabled || !m_frameInProgress) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(pass);
    if (index >= kGpuPassCount) {
        return;
    }

    GpuPassSample& sample = m_current.gpuPasses[index];
    sample.status = status;
    sample.milliseconds = milliseconds;
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
}

} // namespace gte::Profiling
