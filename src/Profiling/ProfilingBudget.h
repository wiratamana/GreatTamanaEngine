#pragma once

#include "ScopeEvent.h"

#include <cstddef>

namespace gte::Profiling {

// Per-frame caps. Overflow is counted, never silent.
inline constexpr std::size_t kMaxScopeEventsPerFrame = 2048;
inline constexpr std::size_t kMaxWorkerEventsPerFrame = 512;
inline constexpr std::size_t kMaxGpuRecordsPerFrame = 512; // plus one overflow bucket

// Lock-free worker ring.
inline constexpr std::size_t kWorkerRingSlots = 1024;
inline constexpr std::size_t kWorkerRingSlotBytes = 32; // sequence word + 24 byte event
static_assert((kWorkerRingSlots & (kWorkerRingSlots - 1)) == 0, "ring size must be a power of two");

// Name table.
inline constexpr std::size_t kNameArenaBytes = 64 * 1024;
inline constexpr std::size_t kNameSlots = 4096; // hash slots, power of two
inline constexpr std::size_t kMaxNames = 3072;  // keeps the hash table at 75% load at most
inline constexpr std::size_t kMaxNameLength = 256;
static_assert((kNameSlots & (kNameSlots - 1)) == 0);
static_assert(kMaxNames < kNameSlots);

// Retained history.
inline constexpr std::size_t kDetailRingFrames = 120;
inline constexpr std::size_t kSpikeStoreFrames = 16;
inline constexpr std::size_t kSummaryRingFrames = 600;

// Byte sizes used by the memory budget.
inline constexpr std::size_t kScopeEventBytes = sizeof(ScopeEvent);
inline constexpr std::size_t kWorkerEventBytes = 24;
inline constexpr std::size_t kGpuRecordBytes = 48;
inline constexpr std::size_t kFrameHeaderBytes = 512;
inline constexpr std::size_t kSummaryRowBytes = 192;

inline constexpr std::size_t kFrameSlotBytes = kMaxScopeEventsPerFrame * kScopeEventBytes
    + kMaxWorkerEventsPerFrame * kWorkerEventBytes + (kMaxGpuRecordsPerFrame + 1) * kGpuRecordBytes
    + kFrameHeaderBytes;

inline constexpr std::size_t kNameTableBytes = kNameArenaBytes + kNameSlots * 8 + (kMaxNames + 1) * 8;
inline constexpr std::size_t kInProgressBytes = 144 * 1024; // open frame plus pending pipelined reports
inline constexpr std::size_t kPublishBytes = 420 * 1024;    // double-buffered snapshot

inline constexpr std::size_t kTotalBudgetBytes = kFrameSlotBytes * (kDetailRingFrames + kSpikeStoreFrames)
    + kInProgressBytes + kSummaryRowBytes * kSummaryRingFrames + kNameTableBytes
    + kWorkerRingSlots * kWorkerRingSlotBytes + kPublishBytes;

static_assert(kTotalBudgetBytes <= 16u * 1024u * 1024u, "profiler memory budget exceeded");

} // namespace gte::Profiling
