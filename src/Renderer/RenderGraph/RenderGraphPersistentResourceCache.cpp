#include "RenderGraphPersistentResourceCache.h"

#include "../Renderer.h"
#include "../VulkanFormatInfo.h"
#include "../Vulkan/FormatCapabilities.h"
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
{
}

std::string RenderGraphPersistentResourceCache::CombineKey(const char* owner, const char* name)
{
    if (owner == nullptr || owner[0] == '\0') {
        return std::string(name);
    }
    return std::string(owner) + "::" + name;
}

PersistentResourceCacheEntry* RenderGraphPersistentResourceCache::EntryFor(PersistentSlotRef slot) noexcept
{
    return m_slots.IsLive(slot) ? m_slotEntries[slot.index] : nullptr;
}

const PersistentResourceCacheEntry* RenderGraphPersistentResourceCache::EntryFor(PersistentSlotRef slot) const noexcept
{
    return m_slots.IsLive(slot) ? m_slotEntries[slot.index] : nullptr;
}

void RenderGraphPersistentResourceCache::BindSlot(PersistentResourceCacheEntry& entry)
{
    // Grow first so nothing can throw once the slot is acquired.
    if (m_slotEntries.size() < m_slots.SlotCount() + 1) {
        m_slotEntries.resize(m_slots.SlotCount() + 1, nullptr);
    }
    const PersistentSlotRef slot = m_slots.Acquire();
    entry.slot = slot;
    m_slotEntries[slot.index] = &entry;
}

void RenderGraphPersistentResourceCache::ReleaseSlot(PersistentSlotRef slot)
{
    if (m_slots.IsLive(slot)) {
        m_slotEntries[slot.index] = nullptr;
        m_slots.Release(slot);
    }
}

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::Resolve(const char* owner, const char* name, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    assert(owner != nullptr && "Resolve requires a non-null owner (pass \"\" to opt out of prefixing)");
    assert(name != nullptr && name[0] != '\0' && "Resolve requires a non-empty name");
    if (owner == nullptr || name == nullptr || name[0] == '\0') {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache", "Resolve() called with a null owner or a null/empty name - refusing.");
        return std::nullopt;
    }

    const std::string key = CombineKey(owner, name);

    assert(!desc.hasDepth && "Resolve is color-only, desc.hasDepth must be false");
    if (desc.hasDepth) {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "Resolve() called with desc.hasDepth == true for \"" + key + "\" - color-only, refusing.");
        return std::nullopt;
    }

    // A storage usage on a format the device cannot store to would make the image create throw every frame.
    if (HasFlag(desc.usage, TextureUsage::Storage)
        && !SupportsStorageImageUsage(m_renderer->GetVulkanContextInfo().physicalDevice, ToVkFormat(desc.format))) {
        if (BoundedOnceSet<kRefusalLogCapacity>::ShouldLog(m_refusalsLogged.Note(key))) {
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "\"" + key + "\" requests storage usage, but this device cannot use its format as a storage image - refusing.");
        }
        return std::nullopt;
    }

    auto [it, inserted] = m_entries.try_emplace(key);
    // The texture's debug name points at the map's own stable key, so the entry is inserted first.
    if (!it->second.texture.has_value()) {
        try {
            it->second.texture.emplace(m_renderer->CreateRenderTexture(static_cast<int>(desc.width),
                static_cast<int>(desc.height), desc.format, it->first.c_str(), /*depthDebugName=*/nullptr,
                HasFlag(desc.usage, TextureUsage::Storage), /*allowDepthSampledAccess=*/false,
                /*createDepthCompanion=*/false));
            it->second.desc = desc;
            it->second.ownKeyForDebugAssert = &it->first;
            BindSlot(it->second);
        } catch (...) {
            ReleaseSlot(it->second.slot);
            if (inserted) {
                m_entries.erase(it);
            }
            throw;
        }
    }

    return ResolveAgainstEntry(it->second, desc, currentFrame, timingMode);
}

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::ResolveFast(
    PersistentSlotRef slot, const TextureDesc& desc, std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    PersistentResourceCacheEntry* entry = EntryFor(slot);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return ResolveAgainstEntry(*entry, desc, currentFrame, timingMode);
}

std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::ResolveAgainstEntry(PersistentResourceCacheEntry& entry, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    assert(entry.texture.has_value() && entry.ownKeyForDebugAssert != nullptr
        && "ResolveAgainstEntry requires a fully constructed entry");

    const std::string& key = *entry.ownKeyForDebugAssert;

    // Two requests in one frame would race on the same image.
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

    // A format or usage change needs a full image recreation, so it is refused.
    if (desc.format != entry.desc.format || desc.usage != entry.desc.usage) {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "\"" + key + "\" was re-requested with a DIFFERENT format or usage than its existing entry - "
              "refusing this call.");
        return std::nullopt;
    }

    if (desc.width != entry.desc.width || desc.height != entry.desc.height) {
        if (timingMode == ExecuteTimingMode::PipelinedDeferredReadback) {
            // The old image may still be in flight, so that regime cannot retire it safely.
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "\"" + key + "\" resize requested from the PipelinedDeferredReadback regime - refusing the resize.");
        } else if (desc.width != entry.failedResizeWidth || desc.height != entry.failedResizeHeight) {
            QueueResize(entry.slot, desc.width, desc.height);
        }
    }

    ResolvedTexture result;
    result.texture = &entry.texture.value();
    result.lastKnownLayout = entry.lastKnownLayout;
    result.combinedKey = entry.ownKeyForDebugAssert;
    result.slot = entry.slot;
    return result;
}

void RenderGraphPersistentResourceCache::RecordFinalLayout(PersistentSlotRef slot, VkImageLayout layout)
{
    if (PersistentResourceCacheEntry* entry = EntryFor(slot)) {
        entry->lastKnownLayout = layout;
    }
}

void RenderGraphPersistentResourceCache::BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames)
{
    m_retired.clear(); // The previous frame has been submitted and waited on by now.
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (IsStaleCacheEntry(it->second.lastUsedFrame, currentFrame, staleThresholdFrames)) {
            ReleaseSlot(it->second.slot);
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
        return 0; // Evicted on the next BeginFrame().
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
    PersistentSlotRef slot, const char* owner, const char* name) const
{
    const PersistentResourceCacheEntry* entry = EntryFor(slot);
    if (entry == nullptr || entry->ownKeyForDebugAssert == nullptr) {
        return true; // Nothing to compare against.
    }
    return *entry->ownKeyForDebugAssert == CombineKey(owner, name);
}
#endif

void RenderGraphPersistentResourceCache::QueueResize(
    PersistentSlotRef slot, std::uint32_t newWidth, std::uint32_t newHeight)
{
    for (PendingResize& pending : m_pendingResizes) {
        if (pending.slot == slot) {
            pending.newWidth = newWidth;
            pending.newHeight = newHeight;
            return;
        }
    }
    m_pendingResizes.push_back(PendingResize{ slot, newWidth, newHeight });
}

const RenderTexture* RenderGraphPersistentResourceCache::TextureFor(PersistentSlotRef slot) const noexcept
{
    const PersistentResourceCacheEntry* entry = EntryFor(slot);
    return (entry != nullptr && entry->texture.has_value()) ? &*entry->texture : nullptr;
}

std::vector<PersistentSlotRef> RenderGraphPersistentResourceCache::FlushPendingResizes()
{
    std::vector<PersistentSlotRef> resized;
    if (m_pendingResizes.empty()) {
        return resized;
    }

    resized.reserve(m_pendingResizes.size());
    m_retired.reserve(m_retired.size() + m_pendingResizes.size());
    for (const PendingResize& pending : m_pendingResizes) {
        PersistentResourceCacheEntry* entry = EntryFor(pending.slot);
        if (entry == nullptr || !entry->texture.has_value()) {
            continue; // Evicted since it was queued.
        }
        try {
            // Create first so a failure leaves the old texture in place.
            RenderTexture fresh = m_renderer->CreateRenderTexture(static_cast<int>(pending.newWidth),
                static_cast<int>(pending.newHeight), entry->desc.format, entry->ownKeyForDebugAssert->c_str(),
                /*depthDebugName=*/nullptr, HasFlag(entry->desc.usage, TextureUsage::Storage),
                /*allowDepthSampledAccess=*/false, /*createDepthCompanion=*/false);
            m_retired.push_back(std::move(*entry->texture)); // Capacity reserved above, move is noexcept.
            entry->texture.emplace(std::move(fresh));
        } catch (const std::exception& e) {
            GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
                "Resize of \"" + *entry->ownKeyForDebugAssert + "\" to " + std::to_string(pending.newWidth) + "x"
                    + std::to_string(pending.newHeight) + " failed, keeping the old texture: " + e.what());
            entry->failedResizeWidth = pending.newWidth;
            entry->failedResizeHeight = pending.newHeight;
            continue;
        }
        entry->desc.width = pending.newWidth;
        entry->desc.height = pending.newHeight;
        entry->lastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED; // A recreated image starts undefined.
        resized.push_back(pending.slot);
    }
    m_pendingResizes.clear();
    return resized;
}

} // namespace gte::rg
