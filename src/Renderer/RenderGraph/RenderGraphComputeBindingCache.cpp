#include "RenderGraphComputeBindingCache.h"

#include "../Renderer.h"
#include "../../Core/Logging.h"
#include "RenderGraphPersistentResourceCache.h"

#include <exception>
#include <string>
#include <string_view>

namespace gte::rg {

RenderGraphComputeBindingCache::RenderGraphComputeBindingCache(Renderer& renderer) noexcept
    : m_renderer(&renderer)
    , m_device(renderer.GetVulkanContextInfo().device)
{
}

void RenderGraphComputeBindingCache::FreeEntry(const Entry& entry) noexcept
{
    if (entry.handle != DescriptorSetHandle::Invalid) {
        UnregisterDescriptorSet(entry.handle);
    }
    m_renderer->FreeComputeDescriptorSet(entry.descriptorSet);
}

DescriptorSetHandle RenderGraphComputeBindingCache::Resolve(
    const char* passName, ViewScope viewScope, VkDescriptorSetLayout layout, const std::vector<ComputeDescriptorWrite>& writes)
{
    if (passName == nullptr || passName[0] == '\0' || layout == VK_NULL_HANDLE) {
        return DescriptorSetHandle::Invalid;
    }

    const ComputeBindingKeyView lookup{ std::string_view(passName), viewScope, layout };
    auto it = m_entries.find(lookup);
    if (it == m_entries.end()) {
        if (!m_failingKeys.ShouldAttempt(lookup, m_currentFrame)) {
            return DescriptorSetHandle::Invalid;
        }
        try {
            Entry entry;
            entry.descriptorSet = m_renderer->AllocateComputeDescriptorSet(layout).Native();
            entry.handle = RegisterDescriptorSet(entry.descriptorSet);
            entry.lastUsedFrame = m_currentFrame;
            try {
                it = m_entries.emplace(ComputeBindingKey{ std::string(passName), viewScope, layout }, entry).first;
            } catch (...) {
                FreeEntry(entry);
                throw;
            }
        } catch (const std::exception& e) {
            const ComputeBindingKey key{ std::string(passName), viewScope, layout };
            if (m_failingKeys.OnFailure(key, m_currentFrame) == BuildRetryGate::FailureKind::First) {
                GTE_LOG_ERROR("RenderGraphComputeBindingCache", std::string("Pass \"") + passName + "\": " + e.what());
            }
            return DescriptorSetHandle::Invalid;
        }
        if (m_failingKeys.OnSuccess(lookup)) {
            GTE_LOG_INFO("RenderGraphComputeBindingCache", std::string("Pass \"") + passName + "\": descriptor set allocation recovered.");
        }
    }

    Entry& entry = it->second;
    entry.lastUsedFrame = m_currentFrame;
    ComputeDescriptorSet(entry.descriptorSet, m_device).Rewrite(writes);
    return entry.handle;
}

void RenderGraphComputeBindingCache::BeginFrame(std::uint64_t currentFrame, std::uint64_t staleFrames)
{
    m_currentFrame = currentFrame;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (IsStaleCacheEntry(it->second.lastUsedFrame, currentFrame, staleFrames)) {
            FreeEntry(it->second);
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}

void RenderGraphComputeBindingCache::Release(VkDescriptorSetLayout layout)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->first.layout == layout) {
            FreeEntry(it->second);
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    m_failingKeys.EraseIf([layout](const ComputeBindingKey& key) { return key.layout == layout; });
}

} // namespace gte::rg
