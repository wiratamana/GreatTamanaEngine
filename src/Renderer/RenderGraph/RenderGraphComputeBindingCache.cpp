#include "RenderGraphComputeBindingCache.h"

#include "../Renderer.h"
#include "../../Core/Logging.h"
#include "RenderGraphPersistentResourceCache.h"

#include <exception>
#include <string>
#include <string_view>

namespace gte::rg {

// A set unused this long is freed; every frame in flight is done with it by then.
static_assert(RenderGraphComputeBindingCache::kDefaultStaleFrames > kGpuFramesInFlight);

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
    m_renderer->FreeDescriptorSet(entry.descriptorSet);
}

DescriptorSetHandle RenderGraphComputeBindingCache::Resolve(const char* passName, ViewScope viewScope,
    VkDescriptorSetLayout layout, const std::vector<DescriptorWrite>& writes, std::uint64_t instance, std::uint32_t slot)
{
    if (passName == nullptr || passName[0] == '\0' || layout == VK_NULL_HANDLE) {
        return DescriptorSetHandle::Invalid;
    }

    const ComputeBindingKeyView lookup{ std::string_view(passName), viewScope, layout, instance, slot };
    auto it = m_entries.find(lookup);
    if (it == m_entries.end()) {
        if (!m_failingKeys.ShouldAttempt(lookup, m_currentFrame)) {
            return DescriptorSetHandle::Invalid;
        }
        try {
            Entry entry;
            entry.descriptorSet = m_renderer->AllocateDescriptorSet(layout).Native();
            entry.handle = RegisterDescriptorSet(entry.descriptorSet);
            entry.lastUsedFrame = m_currentFrame;
            try {
                it = m_entries.emplace(ComputeBindingKey{ std::string(passName), viewScope, layout, instance, slot }, entry).first;
            } catch (...) {
                FreeEntry(entry);
                throw;
            }
        } catch (const std::exception& e) {
            const ComputeBindingKey key{ std::string(passName), viewScope, layout, instance, slot };
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
    if (DescriptorWritesNeedRewrite(entry.lastWrites, writes)) {
        DescriptorSet(entry.descriptorSet, m_device).Rewrite(writes);
        entry.lastWrites = writes;
    }
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

// Frees descriptor sets when destroyed, so the retire queue frees them once no frame in flight reads them.
struct RenderGraphComputeBindingCache::SetsToFree {
    SetsToFree(Renderer* owner, std::vector<VkDescriptorSet> list) noexcept
        : renderer(owner)
        , sets(std::move(list))
    {
    }
    SetsToFree(SetsToFree&& other) noexcept
        : renderer(other.renderer)
        , sets(std::move(other.sets))
    {
        other.sets.clear();
    }
    SetsToFree& operator=(SetsToFree&&) = delete;
    ~SetsToFree()
    {
        for (const VkDescriptorSet set : sets) {
            renderer->FreeDescriptorSet(set);
        }
    }

    Renderer* renderer;
    std::vector<VkDescriptorSet> sets;
};

void RenderGraphComputeBindingCache::Release(VkDescriptorSetLayout layout)
{
    std::vector<VkDescriptorSet> released;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->first.layout == layout) {
            if (it->second.handle != DescriptorSetHandle::Invalid) {
                UnregisterDescriptorSet(it->second.handle);
            }
            released.push_back(it->second.descriptorSet);
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    if (!released.empty()) {
        m_renderer->Retire(SetsToFree(m_renderer, std::move(released)));
    }
    m_failingKeys.EraseIf([layout](const ComputeBindingKey& key) { return key.layout == layout; });
}

} // namespace gte::rg
