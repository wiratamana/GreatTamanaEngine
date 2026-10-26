#pragma once

#include "../Profiling/ProfilingTypes.h"
#include "../Renderer/Memory/GpuMemoryTracker.h"

#include <cstdint>

namespace gte {

// Copies the renderer's memory totals into the profiler's plain snapshot type.
// Always Present: the totals are a cheap read that exists whenever a renderer does.
inline Profiling::MemorySnapshot BuildMemorySnapshot(const GpuMemoryTracker::Totals& totals) noexcept
{
    Profiling::MemorySnapshot snapshot;
    snapshot.status = Profiling::SampleStatus::Present;
    snapshot.totalBytes = static_cast<std::uint64_t>(totals.totalBytes);
    snapshot.bufferBytes = static_cast<std::uint64_t>(totals.bufferBytes);
    snapshot.textureBytes = static_cast<std::uint64_t>(totals.textureBytes);
    snapshot.gpuOnlyBytes = static_cast<std::uint64_t>(totals.gpuOnlyBytes);
    snapshot.cpuOnlyBytes = static_cast<std::uint64_t>(totals.cpuOnlyBytes);
    snapshot.sharedBytes = static_cast<std::uint64_t>(totals.sharedBytes);
    snapshot.bufferCount = static_cast<std::uint64_t>(totals.bufferCount);
    snapshot.textureCount = static_cast<std::uint64_t>(totals.textureCount);
    return snapshot;
}

} // namespace gte
