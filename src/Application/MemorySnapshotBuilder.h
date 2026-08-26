#pragma once

#include "../Profiling/ProfilingTypes.h"
#include "../Renderer/Memory/GpuMemoryTracker.h"

#include <cstdint>

namespace gte {

// Reshapes Renderer::GetMemoryTotals()'s result (GpuMemoryTracker::Totals,
// a Vulkan-tied VkDeviceSize/std::size_t-based type) into a
// Profiling::MemorySnapshot (a plain, Vulkan-free std::uint64_t-based
// type) for FrameProfiler::SetMemorySnapshot() - see PHASE5_GPU_MEMORY_
// HISTORY_STRATEGY_v2.md. Deliberately lives HERE, not inside
// src/Profiling/ (which must stay Vulkan-free - see ProfilingTypes.h's
// own doc comment on MemorySnapshot) and not inside GpuMemoryTracker.h/.cpp
// (which has no reason to know the Profiling module exists) - Application
// is already the one place in this engine that knows about BOTH Renderer's
// data shapes and Profiling's API.
//
// Deliberately its OWN small header (not an anonymous-namespace helper
// inlined into Application.cpp) specifically so it can be called directly
// from a unit test - see tests/Application/MemorySnapshotBuilderTests.cpp.
// The mapping below is arithmetic-free and branch-free, genuinely
// "trivial" - but trivial field-mapping code (eight fields, easy to
// silently transpose two of, e.g. bufferBytes/textureBytes) is exactly
// the class of bug that is easy to introduce and easy to miss in review
// when it's buried anonymously among unrelated per-frame loop logic, and
// a bug here would be invisible to every OTHER test this phase adds
// (those all hand-construct a MemorySnapshot directly and never call this
// function - see PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md, "Changes from
// v1"). Needs nothing but a plain GpuMemoryTracker::Totals value (no live
// VkDevice/VmaAllocator/Renderer) to test directly, same Tier-1 bar this
// engine already applies to e.g. Renderer/DrawStats.h.
//
// Always reports GpuSampleStatus::Present: unlike a GpuPass's draw-call/
// triangle count (which can genuinely be "this pass didn't run this
// frame"), Renderer::GetMemoryTotals() has no such concept - it is always
// a valid, meaningful O(1) read for as long as a live Renderer exists.
inline Profiling::MemorySnapshot BuildMemorySnapshot(const GpuMemoryTracker::Totals& totals) noexcept
{
    Profiling::MemorySnapshot snapshot;
    snapshot.status = Profiling::GpuSampleStatus::Present;
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
