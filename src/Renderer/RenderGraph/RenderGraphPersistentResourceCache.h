#pragma once

#include <cstdint>

namespace gte::rg {

// Forward-declared to avoid a header cycle with RenderGraph.h (which
// defines the real enumerator values); only RenderGraphPersistentResourceCache.cpp
// needs those values.
enum class ExecuteTimingMode : std::uint8_t;

// Opaque outside RenderGraphPersistentResourceCache.cpp.
struct PersistentResourceCacheEntry;

// Caller-owned, POD fast-path handle into one cache entry. Default-constructed
// means "never resolved yet". Safe to copy and reuse across any number of frames.
struct PersistentTextureCacheToken {
    PersistentResourceCacheEntry* entry = nullptr;
    std::uint64_t entryEpoch = 0;
};

// True iff `lastUsedFrame` is more than `staleThresholdFrames` frames behind `currentFrame`.
bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept;

} // namespace gte::rg

#include "RenderGraphTypes.h"
#include "../RenderTexture.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// One cached identity's full state.
struct PersistentResourceCacheEntry {
    // std::optional so a failed first construction can be retried without a
    // dangling/partial RenderTexture ever being observable.
    std::optional<RenderTexture> texture;
    // The desc this entry was last successfully (re)built with - compared
    // against a new request to detect a format/usage mismatch or a resize.
    TextureDesc desc;
    std::uint64_t epoch = 0; // Token fast-path liveness check (IsTokenLive()).
    std::uint64_t lastUsedFrame = 0; // Eviction input (BeginFrame()).
    // Distinct from lastUsedFrame: only used to detect a second request for
    // the SAME identity within the SAME real frame.
    std::uint64_t lastRequestedFrame = 0;
    bool hasLoggedDoubleRequest = false; // Log a double-request refusal once per identity, not once per frame.
    // Honest last-known layout, recorded at the end of every real frame this
    // entry was used (RecordFinalLayout()) - VK_IMAGE_LAYOUT_UNDEFINED for a
    // brand-new entry, or right after a resize recreates its VkImage.
    VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Points at this entry's own key inside m_entries - stable forever
    // (std::unordered_map node stability), used only by debug-misuse asserts.
    const std::string* ownKeyForDebugAssert = nullptr;
};

// Default frame-count an identity may go unrequested before BeginFrame() evicts it.
inline constexpr std::uint64_t kPersistentResourceStaleThresholdFrames = 300;

// Owns every cross-frame, named/owned GPU color texture requested via
// RenderGraphBuilder::GetOrCreatePersistentTexture() - allocates, resizes,
// and evicts them automatically so a pass author never has to hand-manage
// a texture's lifetime as a class member.
class RenderGraphPersistentResourceCache {
public:
    explicit RenderGraphPersistentResourceCache(Renderer& renderer) noexcept;

    // Everything GetOrCreatePersistentTexture() needs to mint this frame's TextureHandle.
    struct ResolvedTexture {
        RenderTexture* texture = nullptr;
        VkImageLayout lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        const std::string* combinedKey = nullptr; // stable for this entry's whole lifetime.
        PersistentResourceCacheEntry* entry = nullptr; // opaque handle for a PersistentTextureCacheToken.
        std::uint64_t entryEpoch = 0;
    };

    // Resolves (creating on first use) the texture identified by (owner, name).
    // `owner` may be empty ("") to keep a pre-existing literal `name` as the
    // combined identity verbatim (no "<owner>::" prefix) - use this ONLY when
    // an external consumer (HTTP capture, a validation tool) already depends
    // on that exact name; every other caller must pass a real owner to avoid
    // colliding with an unrelated feature's identically-named resource.
    // `name` must always be non-empty. `desc.hasDepth` must be false (color
    // only). A format or usage change against an EXISTING entry is always a
    // caller bug (refused); a width/height change queues a resize instead of
    // resizing inline. Returns std::nullopt for any refusal (already logged).
    // May throw std::runtime_error if the underlying RenderTexture construction fails.
    std::optional<ResolvedTexture> Resolve(const char* owner, const char* name, const TextureDesc& desc,
        std::uint64_t currentFrame, ExecuteTimingMode timingMode);

    // Evicts every entry unused for more than staleThresholdFrames - call once per real frame.
    void BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames);

    // How many frames remain before an identity is evicted, or nullopt if it doesn't exist.
    std::optional<std::uint64_t> FramesUntilEviction(
        const std::string& combinedIdentity, std::uint64_t currentFrame) const;
    std::optional<std::uint64_t> FramesUntilEviction(
        const char* owner, const char* name, std::uint64_t currentFrame) const;

#ifndef NDEBUG
    // Debug-only misuse guard: a token must never be reused against a different (owner, name).
    bool DebugTokenIdentityMatches(const PersistentResourceCacheEntry* entry, const char* owner, const char* name) const;
#endif

    // Whether `entry`/`entryEpoch` still refers to a live cache entry - the token fast-path gate.
    bool IsTokenLive(const PersistentResourceCacheEntry* entry, std::uint64_t entryEpoch) const noexcept
    {
        return entry != nullptr && entry->epoch == entryEpoch && entry->epoch != 0;
    }

    // Token-based fast path: identical result to Resolve(), skipping the
    // owned-string build and hash-map lookup. Caller must have already
    // confirmed IsTokenLive(entry, entryEpoch).
    std::optional<ResolvedTexture> ResolveFast(
        PersistentResourceCacheEntry* entry, const TextureDesc& desc, std::uint64_t currentFrame,
        ExecuteTimingMode timingMode);

    // Records the real VkImageLayout a persistent texture's color image was
    // left in at the end of this real frame - a safe no-op if `key` is null
    // or no longer matches a live entry.
    void RecordFinalLayout(const char* key, VkImageLayout layout);

    // Applies every resize queued by Resolve()/ResolveFast() this frame
    // behind exactly one combined vkDeviceWaitIdle() - a no-op when nothing is pending.
    void FlushPendingResizes();

private:
    struct PendingResize {
        PersistentResourceCacheEntry* entry = nullptr;
        std::uint32_t newWidth = 0;
        std::uint32_t newHeight = 0;
    };

    // Builds the combined map key for (owner, name) - owner may be empty, see Resolve().
    static std::string CombineKey(const char* owner, const char* name);

    // Last request this frame wins for the same entry.
    void QueueResize(PersistentResourceCacheEntry* entry, std::uint32_t newWidth, std::uint32_t newHeight);

    // Shared tail of Resolve()/ResolveFast(): double-request refusal, age
    // stamping, format/usage/resize handling, and the final ResolvedTexture.
    std::optional<ResolvedTexture> ResolveAgainstEntry(PersistentResourceCacheEntry& entry, const TextureDesc& desc,
        std::uint64_t currentFrame, ExecuteTimingMode timingMode);

    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE; // Cached once for FlushPendingResizes()'s own vkDeviceWaitIdle().
    std::unordered_map<std::string, PersistentResourceCacheEntry> m_entries;
    std::uint64_t m_nextEntryEpoch = 1; // 0 is reserved, never a real epoch.
    std::vector<PendingResize> m_pendingResizes;
};

} // namespace gte::rg
