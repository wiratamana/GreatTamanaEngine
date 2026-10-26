#include "FrameProfiler.h"

#include "FrameChecks.h"
#include "ProfilingClock.h"

#include <limits>

namespace gte::Profiling {

FrameProfiler& FrameProfiler::Instance() noexcept
{
    static FrameProfiler instance;
    return instance;
}

void FrameProfiler::SetCaptureEnabled(bool enabled) noexcept
{
    m_control.capture.store(enabled, std::memory_order_relaxed);
    m_ring.SetEnabled(enabled);
}

void FrameProfiler::BeginFrame() noexcept
{
    FrameGate::MarkMainThread();

    const bool captureOn = IsCaptureEnabled();
    m_log.BeginFrame(captureOn);
    if (!captureOn) {
        m_frameInProgress = false;
        return;
    }

    m_frameInProgress = true;
    m_frameStartTicks = GetProfilingPerformanceCounter();
    m_openMemory = MemorySnapshot{};
}

void FrameProfiler::EndFrame() noexcept
{
    m_log.EndFrame();
    if (!m_frameInProgress) {
        return;
    }
    m_frameInProgress = false;

    const std::uint64_t endTicks = GetProfilingPerformanceCounter();

    FrameRecord& record = m_last;
    record.scopes = m_log.Buffer();
    record.workers.Clear();
    const WorkerDrainStats drain = m_ring.Drain(m_frameStartTicks, endTicks, record.workers);
    const GateCounters gate = m_log.Gate().TakeCounters();

    FrameSummary& summary = record.summary;
    summary = FrameSummary{};
    summary.frameIndex = m_frameIndex;
    summary.startTicks = m_frameStartTicks;
    summary.endTicks = endTicks;
    summary.cpuMilliseconds = TicksToMilliseconds(endTicks - m_frameStartTicks);
    summary.memory = m_openMemory;
    summary.lateScopes = gate.lateScopes;
    summary.foreignScopes = gate.foreignScopes;
    summary.lostWorkerEvents = drain.lost;

    const FrameTotals totals = ComputeFrameTotals(record);
    summary.topLevelTicks = totals.topLevelTicks;
    summary.blockedTicks = totals.blockedTicks;
    summary.blockedOtherTicks = totals.blockedOtherTicks;

    m_history[m_historyHead] = summary;
    m_historyHead = (m_historyHead + 1) % kMaxFrameHistory;
    if (m_historyCount < kMaxFrameHistory) {
        ++m_historyCount;
    }
    ++m_frameIndex;
}

ScopeStart FrameProfiler::BeginScope() noexcept
{
    if (m_log.Gate().ScopeToken() == 0) {
        return {};
    }
    return m_log.Begin(GetProfilingPerformanceCounter());
}

void FrameProfiler::EndScope(const ScopeStart& start, NameId name, ScopeKind kind, ViewSlot view) noexcept
{
    if (start.token == 0) {
        return;
    }
    m_log.End(start, name, kind, view, GetProfilingPerformanceCounter());
}

void FrameProfiler::RecordMarker(NameId name, ViewSlot view) noexcept
{
    if (m_log.Gate().ScopeToken() == 0) {
        return;
    }
    m_log.Marker(name, view, GetProfilingPerformanceCounter());
}

void FrameProfiler::RecordWorkerScope(
    std::size_t workerIndex, NameId name, std::uint64_t startTicks, std::uint64_t endTicks) noexcept
{
    const WorkerEvent event{ startTicks, endTicks, name, static_cast<std::uint16_t>(workerIndex), 0, 0 };
    m_ring.Push(event);
}

void FrameProfiler::SetMemorySnapshot(const MemorySnapshot& snapshot) noexcept
{
    if (m_frameInProgress) {
        m_openMemory = snapshot;
    }
}

const FrameSummary& FrameProfiler::HistoryAt(std::size_t indexFromOldest) const noexcept
{
    const std::size_t oldest = (m_historyHead + kMaxFrameHistory - m_historyCount) % kMaxFrameHistory;
    return m_history[(oldest + indexFromOldest) % kMaxFrameHistory];
}

const FrameRecord& FrameProfiler::LastCompletedFrame() const noexcept
{
    return m_last;
}

void FrameProfiler::ResetForTesting() noexcept
{
    m_log.BeginFrame(false);
    m_frameInProgress = false;
    m_frameIndex = 0;
    m_frameStartTicks = 0;
    m_openMemory = MemorySnapshot{};
    m_last = FrameRecord{};
    m_history = {};
    m_historyHead = 0;
    m_historyCount = 0;

    // Discard worker events that are still waiting in the ring.
    WorkerEventBuffer discard;
    m_ring.Drain(0, std::numeric_limits<std::uint64_t>::max(), discard);
    m_log.Gate().TakeCounters();
}

} // namespace gte::Profiling
