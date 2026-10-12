#include "RenderGraphPersistentResourceCache.h"

#include "../Renderer.h"
#include "../../Core/Logging.h"
#include "RenderGraph.h"

#include <cassert>

namespace gte::rg {

bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept
{
    return (currentFrame - lastUsedFrame) > staleThresholdFrames;
}

RenderGraphPersistentResourceCache::RenderGraphPersistentResourceCache(Renderer& renderer) noexcept
    : m_renderer(&renderer)
    , m_device(renderer.GetVulkanContextInfo().device)
{
}

std::string RenderGraphPersistentResourceCache::CombineKey(const char* owner, const char* name)
{
    // Empty owner keeps `name` as the identity verbatim - see Resolve()'s own doc comment.
    if (owner == nullptr || owner[0] == '\0') {
        return std::string(name);
    }
    return std::string(owner) + "::" + name;
}

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::Resolve(const char* owner, const char* name, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    assert(owner != nullptr
        && "RenderGraphPersistentResourceCache::Resolve requires a non-null owner (pass \"\" to opt out of prefixing)");
    assert(name != nullptr && name[0] != '\0'
        && "RenderGraphPersistentResourceCache::Resolve requires a non-empty name");
    if (owner == nullptr || name == nullptr || name[0] == '\0') {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "Resolve() called with a null owner or a null/empty name - refusing.");
        return std::nullopt;
    }

    const std::string key = CombineKey(owner, name);

    assert(!desc.hasDepth && "RenderGraphPersistentResourceCache::Resolve is color-only, desc.hasDepth must be false");
    if (desc.hasDepth) {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "Resolve() called with desc.hasDepth == true for \"" + key + "\" - color-only, refusing.");
        return std::nullopt;
    }

    auto [it, inserted] = m_entries.try_emplace(key);

    // Two-phase: insert an empty placeholder first, then construct the real
    // RenderTexture referencing the map's own now-stable key as its debug name.
    if (!it->second.texture.has_value()) {
        try {
            it->second.texture.emplace(m_renderer->CreateRenderTexture(static_cast<int>(desc.width),
                static_cast<int>(desc.height), desc.format, it->first.c_str(), /*depthDebugName=*/nullptr,
                HasFlag(desc.usage, TextureUsage::Storage), /*allowDepthSampledAccess=*/false,
                /*createDepthCompanion=*/false));
            it->second.epoch = m_nextEntryEpoch++;
            it->second.desc = desc;
            it->second.ownKeyForDebugAssert = &it->first;
        } catch (...) {
            // Only erase if THIS call inserted the key - an earlier call's
            // own already-failed placeholder is left for the next retry.
            if (inserted) {
                m_entries.erase(it);
            }
            throw;
        }
    }

    return ResolveAgainstEntry(it->second, desc, currentFrame, timingMode);
}

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::ResolveFast(PersistentResourceCacheEntry* entry, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    assert(entry != nullptr && "RenderGraphPersistentResourceCache::ResolveFast requires a non-null entry");
    return ResolveAgainstEntry(*entry, desc, currentFrame, timingMode);
}

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::ResolveAgainstEntry(PersistentResourceCacheEntry& entry, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    assert(entry.texture.has_value()
        && "RenderGraphPersistentResourceCache::ResolveAgainstEntry requires an already-constructed entry");
    assert(entry.ownKeyForDebugAssert != nullptr
        && "RenderGraphPersistentResourceCache::ResolveAgainstEntry requires entry.ownKeyForDebugAssert to already be set");

    const std::string& key = *entry.ownKeyForDebugAssert;

    // Refuse a second request for the same identity within the same real
    // frame - two ExecuteTimingMode regimes racing on the same VkImage would
    // be a genuine GPU data race.
    if (entry.lastRequestedFrame == currentFrame) {
        if (!entry.hasLoggedDoubleRequest) {
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "\"" + key + "\" was requested twice within the same real frame (frame "
                    + std::to_string(currentFrame) + ") - refusing the second request.");
            entry.hasLoggedDoubleRequest = true;
        }
        return std::nullopt;
    }

    entry.lastRequestedFrame = currentFrame;
    entry.lastUsedFrame = currentFrame;

    // A format or usage (e.g. adding/removing storage-image access) change
    // on an existing entry always needs a full VkImage recreation, never a
    // resize - treated as a caller bug, same as a width/height change would
    // be if silently reinterpreted.
    if (desc.format != entry.desc.format || desc.usage != entry.desc.usage) {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "\"" + key + "\" was re-requested with a DIFFERENT format or usage than its existing entry - "
              "refusing this call.");
        return std::nullopt;
    }

    if (desc.width != entry.desc.width || desc.height != entry.desc.height) {
        if (timingMode == ExecuteTimingMode::PipelinedDeferredReadback) {
            // That regime must never trigger FlushPendingResizes()'s own
            // vkDeviceWaitIdle() - refuse the resize, keep the current extent.
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "\"" + key + "\" resize requested from the PipelinedDeferredReadback regime - refusing the resize.");
        } else {
            QueueResize(&entry, desc.width, desc.height);
        }
    }

    ResolvedTexture result;
    result.texture = &entry.texture.value();
    result.lastKnownLayout = entry.lastKnownLayout;
    result.combinedKey = entry.ownKeyForDebugAssert;
    result.entry = &entry;
    result.entryEpoch = entry.epoch;
    return result;
}

void RenderGraphPersistentResourceCache::RecordFinalLayout(const char* key, VkImageLayout layout)
{
    if (key == nullptr) {
        return;
    }
    const auto it = m_entries.find(std::string(key));
    if (it == m_entries.end()) {
        return; // Should not happen in steady state - defensive only.
    }
    it->second.lastKnownLayout = layout;
}

void RenderGraphPersistentResourceCache::BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (IsStaleCacheEntry(it->second.lastUsedFrame, currentFrame, staleThresholdFrames)) {
            it = m_entries.erase(it); // Destroys the RenderTexture.
        } else {
            ++it;
        }
    }
}

std::optional<std::uint64_t> RenderGraphPersistentResourceCache::FramesUntilEviction(
    const std::string& combinedIdentity, std::uint64_t currentFrame) const
{
    const auto it = m_entries.find(combinedIdentity);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    const std::uint64_t elapsed = currentFrame - it->second.lastUsedFrame;
    if (elapsed >= kPersistentResourceStaleThresholdFrames) {
        return 0; // Already past due - should be evicted on the next BeginFrame() call.
    }
    return kPersistentResourceStaleThresholdFrames - elapsed;
}

std::optional<std::uint64_t> RenderGraphPersistentResourceCache::FramesUntilEviction(
    const char* owner, const char* name, std::uint64_t currentFrame) const
{
    return FramesUntilEviction(CombineKey(owner, name), currentFrame);
}

#ifndef NDEBUG
bool RenderGraphPersistentResourceCache::DebugTokenIdentityMatches(
    const PersistentResourceCacheEntry* entry, const char* owner, const char* name) const
{
    if (entry == nullptr || entry->ownKeyForDebugAssert == nullptr) {
        return true; // Nothing to compare against - never a false failure.
    }
    return *entry->ownKeyForDebugAssert == CombineKey(owner, name);
}
#endif

void RenderGraphPersistentResourceCache::QueueResize(
    PersistentResourceCacheEntry* entry, std::uint32_t newWidth, std::uint32_t newHeight)
{
    // Last request this frame wins for the same entry.
    for (PendingResize& pending : m_pendingResizes) {
        if (pending.entry == entry) {
            pending.newWidth = newWidth;
            pending.newHeight = newHeight;
            return;
        }
    }
    m_pendingResizes.push_back(PendingResize{ entry, newWidth, newHeight });
}

void RenderGraphPersistentResourceCache::FlushPendingResizes()
{
    if (m_pendingResizes.empty()) {
        return;
    }

    // Exactly one combined stall for the whole batch, no matter how many entries resize this frame.
    vkDeviceWaitIdle(m_device);
    for (const PendingResize& pending : m_pendingResizes) {
        pending.entry->texture->Resize(static_cast<int>(pending.newWidth), static_cast<int>(pending.newHeight));
        pending.entry->desc.width = pending.newWidth;
        pending.entry->desc.height = pending.newHeight;
        // A freshly recreated VkImage is always VK_IMAGE_LAYOUT_UNDEFINED again.
        pending.entry->lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    m_pendingResizes.clear();
}

} // namespace gte::rg
