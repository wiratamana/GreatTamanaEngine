#pragma once

#include "EventBuffer.h"
#include "ScopeAggregate.h"
#include "ScopeEvent.h"
#include "WorkerRing.h"

#include <cstddef>
#include <cstdint>

namespace gte::Profiling {

// Frames kept in the summary history.
inline constexpr std::size_t kMaxFrameHistory = 300;

// Plain copy of the GPU memory totals, so this module needs no renderer headers.
struct MemorySnapshot {
    SampleStatus status = SampleStatus::Absent;
    std::uint64_t totalBytes = 0;
    std::uint64_t bufferBytes = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t gpuOnlyBytes = 0;
    std::uint64_t cpuOnlyBytes = 0;
    std::uint64_t sharedBytes = 0;
    std::uint64_t bufferCount = 0;
    std::uint64_t textureCount = 0;
};

// One GPU time with its status. `milliseconds` means something only when the status is Present.
struct GpuViewMetric {
    float milliseconds = 0.0f;
    SampleStatus status = SampleStatus::Absent;
    std::uint32_t foldedPasses = 0; // passes merged into an overflow record and not timed one by one
};

// GPU numbers of the newest frame whose timestamps were read back. Views are indexed by ViewSlot.
struct GpuFrameMetrics {
    GpuViewMetric views[kViewSlotCount]{};
    GpuViewMetric span{};                           // first pass start to the final stamp
    std::uint32_t indirectDraws[kViewSlotCount]{};  // indirect draws are never counted as triangles
    std::uint32_t declaredPasses = 0;               // executed plus culled
    std::uint32_t executedPasses = 0;
    std::uint32_t recordedPasses = 0;               // GPU records the frame carried
    std::uint32_t disabledByToggle = 0;             // never declared because a toggle switched them off
    std::uint32_t droppedFrames = 0;                // frames whose GPU data was lost since start
};

// One finished frame, small enough to keep hundreds of them.
struct FrameSummary {
    std::uint64_t frameIndex = 0;
    std::uint64_t startTicks = 0;
    std::uint64_t endTicks = 0;
    double cpuMilliseconds = 0.0;
    MemorySnapshot memory{};
    GpuFrameMetrics gpu{};

    std::uint32_t lateScopes = 0;    // scopes that ended after their frame closed
    std::uint32_t foreignScopes = 0; // scopes finished off the main thread and dropped
    std::uint32_t lostWorkerEvents = 0;

    std::uint64_t topLevelTicks = 0;     // summed top-level scopes
    std::uint64_t blockedTicks = 0;      // summed Blocked scopes
    std::uint64_t blockedOtherTicks = 0; // Blocked scopes that are not a named GPU or worker wait

    std::uint64_t untrackedTicks = 0;      // frame time outside every top-level scope
    std::uint64_t blockedGpuTicks = 0;     // named GPU waits, present waits included
    std::uint64_t blockedPresentTicks = 0; // the acquire and present part of blockedGpuTicks
    std::uint64_t blockedWorkerTicks = 0;
    NameId largestBlockedName = kInvalidNameId; // the single longest Blocked scope
    std::uint64_t largestBlockedTicks = 0;
};

// The full detail of one finished frame. Large: keep it off the stack.
struct FrameRecord {
    FrameSummary summary{};
    ScopeEventBuffer scopes;
    WorkerEventBuffer workers;
    ScopeAggregateBuffer aggregates; // all lanes
    GpuPassRowBuffer gpuPasses;
};

// A frozen spike holds a whole frame record, so its size decides the memory of the spike store.
static_assert(sizeof(FrameRecord) <= sizeof(FrameSummary) + sizeof(ScopeEventBuffer) + sizeof(WorkerEventBuffer)
        + sizeof(ScopeAggregateBuffer) + sizeof(GpuPassRowBuffer) + 64,
    "FrameRecord grew past the sum of its parts");

} // namespace gte::Profiling
