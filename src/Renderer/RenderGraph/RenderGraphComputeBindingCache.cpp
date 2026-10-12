#include "RenderGraphComputeBindingCache.h"

#include "../Renderer.h"
#include "../../Core/Logging.h"

#include <cassert>

namespace gte::rg {

namespace {

// Combines a pass name with its ViewScope into one map key - see this
// class's own header comment for exactly why the ViewScope half is required
// (two legitimately-distinct per-view declarations of the same pass name
// must never collapse onto one shared VkDescriptorSet).
std::string CombineKey(const char* passName, ViewScope viewScope)
{
    return std::string(passName) + "#" + std::to_string(static_cast<unsigned>(viewScope));
}

} // namespace

RenderGraphComputeBindingCache::RenderGraphComputeBindingCache(Renderer& renderer) noexcept
    : m_renderer(&renderer)
    , m_device(renderer.GetVulkanContextInfo().device)
{
}

DescriptorSetHandle RenderGraphComputeBindingCache::Resolve(
    const char* passName, ViewScope viewScope, VkDescriptorSetLayout layout, const std::vector<ComputeDescriptorWrite>& writes)
{
    assert(passName != nullptr && passName[0] != '\0' &&
        "RenderGraphComputeBindingCache::Resolve requires a non-empty pass name");
    if (passName == nullptr || passName[0] == '\0' || layout == VK_NULL_HANDLE) {
        return DescriptorSetHandle::Invalid;
    }

    const std::string key = CombineKey(passName, viewScope);
    auto [it, inserted] = m_entries.try_emplace(key);
    Entry& entry = it->second;
    if (inserted) {
        entry.descriptorSet = m_renderer->AllocateComputeDescriptorSet(layout).Native();
        entry.layout = layout;
        entry.handle = RegisterDescriptorSet(entry.descriptorSet);
    }

    assert(entry.layout == layout &&
        "RenderGraphComputeBindingCache::Resolve: this pass identity's descriptor set layout changed since it "
        "was first resolved - a pass's layout can never legitimately change build-to-build");
    if (entry.layout != layout) {
        GTE_LOG_ERROR("RenderGraphComputeBindingCache",
            "Resolve() called for pass \"" + key
                + "\" with a descriptor set layout different from its first resolve - refusing.");
        return DescriptorSetHandle::Invalid;
    }

    ComputeDescriptorSet(entry.descriptorSet, m_device).Rewrite(writes);
    return entry.handle;
}

} // namespace gte::rg
