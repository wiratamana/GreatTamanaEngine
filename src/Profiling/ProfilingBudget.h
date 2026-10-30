#pragma once

#include <cstddef>

namespace gte::Profiling {

// Per-frame caps. Overflow is counted, never silent.
inline constexpr std::size_t kMaxScopeEventsPerFrame = 2048;
inline constexpr std::size_t kMaxWorkerEventsPerFrame = 512;
inline constexpr std::size_t kMaxScopeAggregatesPerFrame = 768;
inline constexpr std::size_t kMaxGpuPassRowsPerFrame = 256;

// Per-name history: one track per lane and name.
inline constexpr std::size_t kHistoryTracks = 256;

// Lock-free worker ring.
inline constexpr std::size_t kWorkerRingSlots = 1024;
inline constexpr std::size_t kWorkerRingSlotBytes = 32; // sequence word + 24 byte event
inline constexpr std::size_t kWorkerEventBytes = 24;
static_assert((kWorkerRingSlots & (kWorkerRingSlots - 1)) == 0, "ring size must be a power of two");

// Name table.
inline constexpr std::size_t kNameArenaBytes = 64 * 1024;
inline constexpr std::size_t kNameSlots = 4096; // hash slots, power of two
inline constexpr std::size_t kMaxNames = 3072;  // keeps the hash table at 75% load at most
inline constexpr std::size_t kMaxNameLength = 256;
static_assert((kNameSlots & (kNameSlots - 1)) == 0);
static_assert(kMaxNames < kNameSlots);

// Frozen spike frames kept.
inline constexpr std::size_t kSpikeStoreFrames = 16;

} // namespace gte::Profiling
