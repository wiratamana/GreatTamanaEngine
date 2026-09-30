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
RenderGraphPersistentResourceCache::Resolve(const char* owner, const char* name, const TextureDesc& desc)
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

} // namespace gte::rg
