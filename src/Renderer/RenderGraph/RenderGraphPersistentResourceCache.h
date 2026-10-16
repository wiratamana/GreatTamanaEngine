#pragma once

#include "BoundedOnceSet.h"
#include "PersistentSlotTable.h"
#include "RenderGraphTypes.h"
#include "../RenderTexture.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Defined in RenderGraph.h.
enum class ExecuteTimingMode : std::uint8_t;

// Caller-owned handle to one cache entry. Safe to copy, keep for any number of frames,
// and use after the entry was evicted (it just reports dead).
struct PersistentTextureCacheToken {
    PersistentSlotRef slot;
    bool refusalLogged = false; // Caller-side refusals log once per token.
};

// True iff `lastUsedFrame` is more than `staleThresholdFrames` frames behind `currentFrame`.
bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept;

struct PersistentResourceCacheEntry {
    std::optional<RenderTexture> texture;
    TextureDesc desc;                // Desc of the last successful (re)build.
    PersistentSlotRef slot;          // Liveness handle for tokens.
    std::uint64_t lastUsedFrame = 0; // Eviction input.
    // Frame of the last request; "none yet" until the first one, so frame 0 is a valid first request.
    std::uint64_t lastRequestedFrame = std::numeric_limits<std::uint64_t>::max();
    bool hasLoggedDoubleRequest = false;
    VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Layout at the end of the last used frame.
    const std::string* ownKeyForDebugAssert = nullptr;         // Points at this entry's own map key.
    std::uint32_t failedResizeWidth = 0;  // Size of the last failed resize; not retried until it changes.
    std::uint32_t failedResizeHeight = 0;
};

// Frames an identity may go unrequested before BeginFrame() evicts it.
inline constexpr std::uint64_t kPersistentResourceStaleThresholdFrames = 300;

// Distinct refused identities remembered so each refusal is logged once.
inline constexpr std::size_t kRefusalLogCapacity = 64;

// Owns cross-frame named color textures; creates, resizes and evicts them on demand.
class RenderGraphPersistentResourceCache {
public:
    explicit RenderGraphPersistentResourceCache(Renderer& renderer) noexcept;

    struct ResolvedTexture {
        RenderTexture* texture = nullptr;
        VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        const std::string* combinedKey = nullptr; // Stable while the entry is alive.
        PersistentSlotRef slot;
    };

    // Resolves (creating on first use) the color texture for (owner, name); an empty owner keeps `name` as is.
    // Refuses (nullopt, logged) a format/usage change or an unsupported storage format; throws if creation fails.
    std::optional<ResolvedTexture> Resolve(const char* owner, const char* name, const TextureDesc& desc,
        std::uint64_t currentFrame, ExecuteTimingMode timingMode);

    // Frees retired textures and evicts every entry unused for more than staleThresholdFrames.
    // Call once per real frame, after the previous frame's work has finished on the GPU.
    void BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames);

    // Frames left before an identity is evicted, or nullopt if it does not exist.
    std::optional<std::uint64_t> FramesUntilEviction(
        const std::string& combinedIdentity, std::uint64_t currentFrame) const;
    std::optional<std::uint64_t> FramesUntilEviction(
        const char* owner, const char* name, std::uint64_t currentFrame) const;

#ifndef NDEBUG
    // Debug misuse guard: a live token must keep resolving to the same (owner, name).
    bool DebugTokenIdentityMatches(PersistentSlotRef slot, const char* owner, const char* name) const;
#endif

    bool IsTokenLive(const PersistentTextureCacheToken& token) const noexcept { return m_slots.IsLive(token.slot); }

    // Same result as Resolve() without the key build and map lookup. Returns nullopt if the
    // token is dead or the request is refused.
    std::optional<ResolvedTexture> ResolveFast(
        PersistentSlotRef slot, const TextureDesc& desc, std::uint64_t currentFrame, ExecuteTimingMode timingMode);

    // Stores the layout the texture was left in at the end of the frame. No-op for a dead slot.
    void RecordFinalLayout(PersistentSlotRef slot, VkImageLayout layout);

    // Recreates queued textures at their new size; old ones are retired (the open command buffer may use them).
    // A failed create keeps the old texture. Returns the recreated slots.
    std::vector<PersistentSlotRef> FlushPendingResizes();

    // The live texture behind a slot, or nullptr if the slot is dead.
    const RenderTexture* TextureFor(PersistentSlotRef slot) const noexcept;

private:
    struct PendingResize {
        PersistentSlotRef slot;
        std::uint32_t newWidth = 0;
        std::uint32_t newHeight = 0;
    };

    static std::string CombineKey(const char* owner, const char* name);

    PersistentResourceCacheEntry* EntryFor(PersistentSlotRef slot) noexcept;
    const PersistentResourceCacheEntry* EntryFor(PersistentSlotRef slot) const noexcept;

    // Gives `entry` a live slot and records the slot -> entry mapping.
    void BindSlot(PersistentResourceCacheEntry& entry);
    void ReleaseSlot(PersistentSlotRef slot);

    // The last request in a frame wins for the same slot.
    void QueueResize(PersistentSlotRef slot, std::uint32_t newWidth, std::uint32_t newHeight);

    std::optional<ResolvedTexture> ResolveAgainstEntry(PersistentResourceCacheEntry& entry, const TextureDesc& desc,
        std::uint64_t currentFrame, ExecuteTimingMode timingMode);

    Renderer* m_renderer = nullptr;
    std::unordered_map<std::string, PersistentResourceCacheEntry> m_entries; // Node-stable.
    PersistentSlotTable m_slots;
    std::vector<PersistentResourceCacheEntry*> m_slotEntries; // Indexed by slot index.
    std::vector<PendingResize> m_pendingResizes;
    std::vector<RenderTexture> m_retired; // Freed at the next BeginFrame().
    BoundedOnceSet<kRefusalLogCapacity> m_refusalsLogged; // Identities whose refusal was already logged.
};

} // namespace gte::rg
