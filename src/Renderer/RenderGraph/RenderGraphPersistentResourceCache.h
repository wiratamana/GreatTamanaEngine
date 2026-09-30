#pragma once

#include <cstdint>

// editor-core-separation-27 campaign (BIG STEP 3 of 4 -
// BIG_STEP_3_PERSISTENT_RESOURCE_CACHE_HONEST_LAYOUT_HISTORY_REV2_2026-09-30.txt).
// PHASE2 of this campaign creates this file containing ONLY
// PersistentTextureCacheToken/IsStaleCacheEntry() - PHASE4 ADDS the real
// RenderGraphPersistentResourceCache class into this SAME file (never a
// second header), plus PHASE5/PHASE6 extend it further. See
// task_manager/editor-core-separation-27/PHASE0_MASTER_STRATEGY.md.

namespace gte::rg {

// Opaque outside RenderGraphPersistentResourceCache.cpp - see PHASE4.
struct PersistentResourceCacheEntry;

// Source document Section 4 - a small, POD, caller-owned token. Default-
// constructed as "never resolved yet" (entry == nullptr). Safe to keep,
// copy, and reuse across any number of frames.
struct PersistentTextureCacheToken {
    PersistentResourceCacheEntry* entry = nullptr;
    std::uint64_t entryEpoch = 0;
};

// Source document Section 8 - pure, Tier-1-testable. True iff `lastUsedFrame`
// is more than `staleThresholdFrames` frames behind `currentFrame`.
bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept;

} // namespace gte::rg
