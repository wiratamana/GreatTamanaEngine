#pragma once

#include "LiveBlockTable.h"
#include "ProfilingTypes.h"
#include "ScopeLog.h"
#include "SpikeStore.h"
#include "WorkerRing.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gte::Profiling {

// Frames whose time is compared to find a spike.
inline constexpr std::size_t kSpikeMedianWindow = 60;

// Runtime switches. Safe to read and write from any thread.
struct ProfilerControl {
    std::atomic<bool> capture{ true };       // record events
    std::atomic<bool> displayPaused{ false }; // UI freezes its view; recording continues
    std::atomic<float> spikeThresholdMs{ 0.0f }; // > 0 replaces the median rule for freezing spikes
};

// Owns the open frame's event log and the history of finished frames. Frame and scope calls are
// main thread only; worker threads report through RecordWorkerScope().
class FrameProfiler {
public:
    static FrameProfiler& Instance() noexcept;

    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;

    ProfilerControl& Control() noexcept { return m_control; }
    const ProfilerControl& Control() const noexcept { return m_control; }

    // Waits in progress on every thread. Safe to use from any thread.
    LiveBlockTable& LiveBlocks() noexcept { return m_liveBlocks; }
    const LiveBlockTable& LiveBlocks() const noexcept { return m_liveBlocks; }

    // Turning capture off also stops worker threads from pushing events.
    void SetCaptureEnabled(bool enabled) noexcept;
    bool IsCaptureEnabled() const noexcept { return m_control.capture.load(std::memory_order_relaxed); }

    void SetDisplayPaused(bool paused) noexcept { m_control.displayPaused.store(paused, std::memory_order_relaxed); }
    bool IsDisplayPaused() const noexcept { return m_control.displayPaused.load(std::memory_order_relaxed); }

    // Opens a frame. The calling thread becomes the main thread. No frame is recorded while capture is off.
    void BeginFrame() noexcept;

    // Closes the frame and stores its record. Does nothing when BeginFrame recorded nothing.
    void EndFrame() noexcept;

    // Scope bracket. Cheap no-op outside a captured frame.
    ScopeStart BeginScope() noexcept;
    void EndScope(const ScopeStart& start, NameId name, ScopeKind kind, ViewSlot view) noexcept;
    void RecordMarker(NameId name, ViewSlot view = ViewSlot::Shared) noexcept;

    // Any thread. Reports one finished job scope.
    void RecordWorkerScope(std::size_t workerIndex, NameId name, std::uint64_t startTicks, std::uint64_t endTicks) noexcept;

    // Memory snapshot of the open frame.
    void SetMemorySnapshot(const MemorySnapshot& snapshot) noexcept;

    // GPU numbers of the open frame. A frame that never sets them keeps every GPU number Absent.
    void SetGpuMetrics(const GpuFrameMetrics& metrics) noexcept;

    // Frames closed so far.
    std::uint64_t CompletedFrameCount() const noexcept { return m_frameIndex; }

    // Summary history, oldest first. indexFromOldest must be < HistoryCount().
    std::size_t HistoryCount() const noexcept { return m_historyCount; }
    const FrameSummary& HistoryAt(std::size_t indexFromOldest) const noexcept;

    // Event detail of the newest finished frame. An empty record before the first frame.
    const FrameRecord& LastCompletedFrame() const noexcept;

    // Frames frozen because they were much slower than the recent median. Any thread may read and pin.
    SpikeStore& Spikes() noexcept { return m_spikes; }
    const SpikeStore& Spikes() const noexcept { return m_spikes; }

    // Main thread. A GPU report that arrived after its frame closed; kept only for a frozen spike.
    void AttachLateGpuReport(std::uint64_t frameIndex, const GpuFrameMetrics& gpu) noexcept;

    // Spike threshold in ms; 0 selects the median rule.
    void SetSpikeThresholdMs(float milliseconds) noexcept { m_control.spikeThresholdMs.store(milliseconds, std::memory_order_relaxed); }
    float SpikeThresholdMs() const noexcept { return m_control.spikeThresholdMs.load(std::memory_order_relaxed); }

    // Measured cost of one recorded scope in ns; 0 until measured. Set once from the main thread at startup.
    void SetEventCostNs(double nanoseconds) noexcept { m_eventCostNs = nanoseconds; }
    double EventCostNs() const noexcept { return m_eventCostNs; }

    // Forgets all history, frozen spikes and pending worker events. Keeps the runtime switches.
    void ResetForTesting() noexcept;

private:
    FrameProfiler() = default;

    // Freezes the closing frame into the spike store when it is much slower than the recent median.
    void FreezeIfSpike(const FrameRecord& record) noexcept;

    ProfilerControl m_control;
    LiveBlockTable m_liveBlocks;
    bool m_frameInProgress = false;
    std::uint64_t m_frameIndex = 0;
    std::uint64_t m_frameStartTicks = 0;
    MemorySnapshot m_openMemory{};
    GpuFrameMetrics m_openGpu{};

    ScopeLog m_log;
    WorkerRing m_ring;
    FrameRecord m_last{};

    std::array<FrameSummary, kMaxFrameHistory> m_history{};
    std::size_t m_historyHead = 0; // next slot to write
    std::size_t m_historyCount = 0;

    SpikeStore m_spikes;
    double m_eventCostNs = 0.0;
};

} // namespace gte::Profiling
