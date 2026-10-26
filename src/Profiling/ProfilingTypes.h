#pragma once

#include "EventBuffer.h"
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

// One finished frame, small enough to keep hundreds of them.
struct FrameSummary {
    std::uint64_t frameIndex = 0;
    std::uint64_t startTicks = 0;
    std::uint64_t endTicks = 0;
    double cpuMilliseconds = 0.0;
    MemorySnapshot memory{};

    std::uint32_t lateScopes = 0;    // scopes that ended after their frame closed
    std::uint32_t foreignScopes = 0; // scopes finished off the main thread and dropped
    std::uint32_t lostWorkerEvents = 0;
};

// The full event detail of one finished frame.
struct FrameRecord {
    FrameSummary summary{};
    ScopeEventBuffer scopes;
    WorkerEventBuffer workers;
};

} // namespace gte::Profiling
