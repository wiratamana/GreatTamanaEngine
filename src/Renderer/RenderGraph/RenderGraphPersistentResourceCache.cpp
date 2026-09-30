#include "RenderGraphPersistentResourceCache.h"

#include "../Renderer.h"
#include "../../Core/Logging.h" // PHASE4 (editor-core-separation-27 campaign) - GTE_LOG_ERROR for
    // Resolve()'s own validation-refusal branches (empty owner/name, desc.hasDepth == true). Safe to
    // include unconditionally regardless of GTE_ENABLE_EDITOR - see RenderGraph.cpp's own identical
    // precedent (this file's header comment).

#include <cassert>

namespace gte::rg {

bool IsStaleCacheEntry(
    std::uint64_t lastUsedFrame, std::uint64_t currentFrame, std::uint64_t staleThresholdFrames) noexcept
{
    return (currentFrame - lastUsedFrame) > staleThresholdFrames;
}

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::Resolve(
    const char* owner, const char* name, const TextureDesc& desc, std::uint64_t currentFrame)
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

} // namespace gte::rg
