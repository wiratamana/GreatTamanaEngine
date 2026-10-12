#include "RenderGraphComputeBindingCache.h"

#include "../Renderer.h"
#include "../../Core/Logging.h"

#include <cassert>

namespace gte::rg {

RenderGraphComputeBindingCache::RenderGraphComputeBindingCache(Renderer& renderer) noexcept
    : m_renderer(&renderer)
    , m_device(renderer.GetVulkanContextInfo().device)
{
}

DescriptorSetHandle RenderGraphComputeBindingCache::Resolve(
    const char* passName, VkDescriptorSetLayout layout, const std::vector<ComputeDescriptorWrite>& writes)
{
    assert(passName != nullptr && passName[0] != '\0' &&
        "RenderGraphComputeBindingCache::Resolve requires a non-empty pass name");
    if (passName == nullptr || passName[0] == '\0' || layout == VK_NULL_HANDLE) {
        return DescriptorSetHandle::Invalid;
    }

    auto [it, inserted] = m_entries.try_emplace(passName);
    Entry& entry = it->second;
    if (inserted) {
        entry.descriptorSet = m_renderer->AllocateComputeDescriptorSet(layout).Native();
        entry.layout = layout;
        entry.handle = RegisterDescriptorSet(entry.descriptorSet);
    }

    assert(entry.layout == layout &&
        "RenderGraphComputeBindingCache::Resolve: this pass's descriptor set layout changed since it was "
        "first resolved - a pass's layout can never legitimately change build-to-build");
    if (entry.layout != layout) {
        GTE_LOG_ERROR("RenderGraphComputeBindingCache",
            "Resolve() called for pass \"" + std::string(passName)
                + "\" with a descriptor set layout different from its first resolve - refusing.");
        return DescriptorSetHandle::Invalid;
    }

    ComputeDescriptorSet(entry.descriptorSet, m_device).Rewrite(writes);
    return entry.handle;
}

} // namespace gte::rg
