#include "RenderGraphPersistentResourceCache.h"

#include "../Renderer.h"
#include "../../Core/Logging.h" // PHASE4 (editor-core-separation-27 campaign) - GTE_LOG_ERROR for
    // Resolve()'s own validation-refusal branches (empty owner/name, desc.hasDepth == true, a
    // same-frame double request, a format change, a pipelined-regime resize refusal). Safe to
    // include unconditionally regardless of GTE_ENABLE_EDITOR - see RenderGraph.cpp's own identical
    // precedent (this file's header comment).
#include "RenderGraph.h" // PHASE6 - this .cpp (never the header - see RenderGraphPersistentResourceCache.h's
    // own forward-declare comment) needs the actual ExecuteTimingMode ENUMERATOR VALUE
    // (PipelinedDeferredReadback) Resolve()'s own body compares against. Safe regardless of whether
    // PHASE7 has made RenderGraph.h include this header in return, since a .cpp file is never itself
    // included by anything else, so this can never participate in a header cycle.

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

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::Resolve(const char* owner, const char* name, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    assert(owner != nullptr && owner[0] != '\0'
        && "RenderGraphPersistentResourceCache::Resolve requires a non-empty owner");
    assert(name != nullptr && name[0] != '\0'
        && "RenderGraphPersistentResourceCache::Resolve requires a non-empty name");
    if (owner == nullptr || owner[0] == '\0' || name == nullptr || name[0] == '\0') {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "Resolve() called with a null/empty owner or name - refusing.");
        return std::nullopt;
    }

    assert(!desc.hasDepth
        && "RenderGraphPersistentResourceCache::Resolve: v1 is color-only, desc.hasDepth must be false");
    if (desc.hasDepth) {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "Resolve() called with desc.hasDepth == true for \"" + std::string(owner) + "::" + name
                + "\" - v1 is color-only, refusing.");
        return std::nullopt;
    }

    const std::string key = std::string(owner) + "::" + name;

    // Step 1 - insert an EMPTY placeholder first (this is the ONLY step
    // that copies the caller's owner/name arguments; every subsequent
    // step reads the key back OUT of the map).
    auto [it, inserted] = m_entries.try_emplace(key);

    // Section 5.3 - refuse a second request for the SAME identity within the
    // SAME real frame, regardless of which ExecuteTimingMode regime called
    // first. A brand-new entry's lastRequestedFrame defaults to 0, which
    // currentFrame (always >= 1 - see PHASE7's frame-counter seeding) can
    // never coincidentally equal, so this never misfires for the first-ever
    // request of a new identity.
    if (!inserted && it->second.lastRequestedFrame == currentFrame) {
        if (!it->second.hasLoggedDoubleRequest) {
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "\"" + key + "\" was requested twice within the same real frame (frame "
                    + std::to_string(currentFrame) + ") - refusing the second request. Two "
                      "ExecuteTimingMode regimes racing on the same shared VkImage would be a "
                      "genuine GPU data race - see BIG_STEP_3 Section 5.3.");
            it->second.hasLoggedDoubleRequest = true;
        }
        return std::nullopt;
    }
    it->second.lastRequestedFrame = currentFrame;
    it->second.lastUsedFrame = currentFrame; // Section 8's own eviction input - stamped every call, fast path included (PHASE8).

    // Step 2/3 - if a real RenderTexture does not exist here yet,
    // construct it, referencing the MAP'S OWN, now-stable key
    // (it->first.c_str()) as debugName - NEVER `owner`/`name`/`key`
    // directly (see this phase's own Step 2 "Situation" citation of
    // RenderTexture's own by-reference debugName hazard).
    if (!it->second.texture.has_value()) {
        try {
            it->second.texture.emplace(m_renderer->CreateRenderTexture(
                static_cast<int>(desc.width), static_cast<int>(desc.height), desc.format,
                it->first.c_str(), /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/false,
                /*allowDepthSampledAccess=*/false, /*createDepthCompanion=*/false));
            it->second.epoch = m_nextEntryEpoch++;
            it->second.desc = desc;
            it->second.ownKeyForDebugAssert = &it->first; // stable forever (TR4) - Section 4's debug-only misuse guard.
        } catch (...) {
            // Exception safety (Section 6.1): only erase if THIS call
            // inserted the key - an earlier call's own already-failed
            // placeholder is left in place for the NEXT call to retry
            // against the same, already-stable key.
            if (inserted) {
                m_entries.erase(it);
            }
            throw;
        }
    }

    // PHASE6 (Section 5.5/FR4/FR8) - a genuinely PRE-EXISTING entry
    // (never a brand-new one, hence !inserted) being re-requested with a
    // different format/width/height.
    if (!inserted) {
        if (desc.format != it->second.desc.format) {
            // A format (or, by extension, hasDepth) change on an EXISTING
            // entry is ALWAYS a caller bug, independent of regime - logged
            // loudly, never silently reinterpreted, never queued as a
            // resize.
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "\"" + key + "\" was re-requested with a DIFFERENT format than its existing entry - "
                  "this is always a caller bug, refusing this call.");
            return std::nullopt;
        }
        if (desc.width != it->second.desc.width || desc.height != it->second.desc.height) {
            if (timingMode == ExecuteTimingMode::PipelinedDeferredReadback) {
                // A resize request from the pipelined/Present regime is
                // refused immediately: keep the entry's CURRENT extent,
                // ignore desc.width/height for THIS call only - never
                // queued, never batched, since that regime must never
                // issue a vkDeviceWaitIdle() at all.
                GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                    "\"" + key + "\" resize requested from the PipelinedDeferredReadback regime - refusing "
                      "the resize (keeping the current extent) for this call only; a resize must be "
                      "requested from the SynchronousImmediateReadback regime.");
            } else {
                QueueResize(&it->second, desc.width, desc.height);
            }
            // Either way, THIS call's own returned texture/extent is still
            // the entry's CURRENT (old) one - never the newly-requested size.
        }
    }

    // Step 4 - hand back what GetOrCreatePersistentTexture() needs.
    ResolvedTexture result;
    result.texture = &it->second.texture.value();
    result.lastKnownLayout = it->second.lastKnownLayout;
    result.combinedKey = &it->first;
    result.entry = &it->second;
    result.entryEpoch = it->second.epoch;
    return result;
}

void RenderGraphPersistentResourceCache::BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (IsStaleCacheEntry(it->second.lastUsedFrame, currentFrame, staleThresholdFrames)) {
            it = m_entries.erase(it); // destroys the RenderTexture (its destructor runs here).
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
        return 0; // already past due (should be evicted on the NEXT BeginFrame() call).
    }
    return kPersistentResourceStaleThresholdFrames - elapsed;
}

std::optional<std::uint64_t> RenderGraphPersistentResourceCache::FramesUntilEviction(
    const char* owner, const char* name, std::uint64_t currentFrame) const
{
    return FramesUntilEviction(std::string(owner) + "::" + name, currentFrame);
}

#ifndef NDEBUG
bool RenderGraphPersistentResourceCache::DebugTokenIdentityMatches(
    const PersistentResourceCacheEntry* entry, const char* owner, const char* name) const
{
    if (entry == nullptr || entry->ownKeyForDebugAssert == nullptr) {
        return true; // nothing to compare against - never a false failure.
    }
    return *entry->ownKeyForDebugAssert == (std::string(owner) + "::" + name);
}
#endif

void RenderGraphPersistentResourceCache::QueueResize(
    PersistentResourceCacheEntry* entry, std::uint32_t newWidth, std::uint32_t newHeight)
{
    // Last request THIS frame wins for the SAME entry - matches "single
    // builder, single frame" reasoning used elsewhere in this engine.
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
    // Exactly ONE combined stall for the WHOLE batch, no matter how many
    // entries need resizing this frame (Section 5.5/FR8) - mirrors
    // RenderFeatureCompositor::EnsureTextureSized()'s own direct
    // vkDeviceWaitIdle(VkDevice) call, NEVER Renderer::WaitForGpuIdle()
    // (see this class's own constructor doc comment for why that wrapper
    // is wrong here).
    vkDeviceWaitIdle(m_device);
    for (const PendingResize& pending : m_pendingResizes) {
        pending.entry->texture->Resize(
            static_cast<int>(pending.newWidth), static_cast<int>(pending.newHeight));
        pending.entry->desc.width = pending.newWidth;
        pending.entry->desc.height = pending.newHeight;
        // Section 5.1 - a resize atomically resets the remembered layout:
        // a freshly vmaCreateImage()'d VkImage is always
        // VK_IMAGE_LAYOUT_UNDEFINED (see RenderTexture::Create()'s own
        // imageInfo.initialLayout).
        pending.entry->lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    m_pendingResizes.clear();
}

} // namespace gte::rg
