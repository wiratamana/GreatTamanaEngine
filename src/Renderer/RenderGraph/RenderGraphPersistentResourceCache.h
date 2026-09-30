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

// PHASE4 (editor-core-separation-27 campaign) - the real
// RenderGraphPersistentResourceCache class, added into this SAME file per
// this phase's own .md (Step 2's "this phase EXTENDS that same file, never
// creates a second one"). Needs RenderGraphTypes.h (TextureDesc) and
// RenderTexture.h (the actual GPU resource an entry owns).
#include "RenderGraphTypes.h"
#include "../RenderTexture.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace gte {
class Renderer;
}

namespace gte::rg {

// One entry's full state. Declared with every field this WHOLE campaign
// eventually needs (PHASE5/6/8 progressively start reading/writing the
// fields THIS phase itself never touches) - see this class's own doc
// comment for why growing this struct's USE incrementally, without
// re-declaring it, is safe and intentional.
struct PersistentResourceCacheEntry {
    std::optional<RenderTexture> texture; // std::optional so Section 6.1's
        // two-phase recipe can insert an EMPTY placeholder first, then
        // construct the real RenderTexture referencing the map's own
        // now-stable key - never constructed inline in try_emplace() itself.
    TextureDesc desc; // the desc this entry was LAST successfully (re)built
        // with - PHASE6 compares a new request's desc.width/height against
        // this to detect a resize request.
    std::uint64_t epoch = 0; // PHASE4 - Section 4's fast-path safety net.
    std::uint64_t lastUsedFrame = 0; // PHASE5 - Section 8's eviction input.
    std::uint64_t lastRequestedFrame = 0; // PHASE5 - Section 5.3's double-
        // request guard input. Deliberately distinct from lastUsedFrame:
        // lastUsedFrame is what eviction reads (must survive across BOTH
        // regimes' calls this frame); lastRequestedFrame exists purely to
        // detect a SECOND request THIS SAME frame.
    bool hasLoggedDoubleRequest = false; // PHASE5 - "log exactly once ever
        // per identity", never once per frame forever.
    VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED; // PHASE8 -
        // Section 5.1's honest layout. PHASE4 never sets this to anything
        // other than its own default (UNDEFINED) - correct, since "nothing
        // to remember yet" IS the honest value for a brand-new entry.
};

class RenderGraphPersistentResourceCache {
public:
    explicit RenderGraphPersistentResourceCache(Renderer& renderer) noexcept
        : m_renderer(&renderer)
    {
    }

    // What a successful Resolve() call hands back - everything
    // GetOrCreatePersistentTexture() (PHASE8) needs to mint this frame's
    // TextureHandle. Never default-constructed/returned on failure - a
    // failed Resolve() is std::nullopt (a refusal already logged inside
    // this method) OR a thrown exception (a genuine RenderTexture
    // construction failure - Section 6.1 - propagated, never swallowed).
    struct ResolvedTexture {
        RenderTexture* texture = nullptr;
        VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        const std::string* combinedKey = nullptr; // stable for this entry's whole lifetime.
        PersistentResourceCacheEntry* entry = nullptr; // opaque handle for a future token (PHASE5).
        std::uint64_t entryEpoch = 0;
    };

    // PHASE4 scope: construction/ownership/exception-safety (Section 6,
    // 6.1, 6.2) + "color only" enforcement (Section 7). Returns
    // std::nullopt for a validation refusal (empty owner/name,
    // desc.hasDepth == true) - each refusal is asserted in debug builds
    // and GTE_LOG_ERROR'd in release, mirroring this codebase's "assert in
    // debug, log-and-refuse in release, never crash" discipline. May THROW
    // std::runtime_error if the underlying RenderTexture construction
    // genuinely fails (Section 6.1) - never caught/swallowed here.
    std::optional<ResolvedTexture> Resolve(const char* owner, const char* name, const TextureDesc& desc);

private:
    Renderer* m_renderer = nullptr;
    std::unordered_map<std::string, PersistentResourceCacheEntry> m_entries;
    std::uint64_t m_nextEntryEpoch = 1; // 0 is reserved, never a real epoch.
};

} // namespace gte::rg
