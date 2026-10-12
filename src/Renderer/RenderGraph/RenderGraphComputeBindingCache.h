#pragma once

#include "../Compute/ComputeDescriptorSet.h"
#include "../GraphicsHandleRegistry.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Owns every VkDescriptorSet the compute-binding API (RenderGraphBuilder::
// PassBuilder::Bind*()) allocates - keyed by a pass's own stable name
// (same string-literal identity convention AddPass()/AddRenderPass() already
// require). Resolves ONCE per pass name, re-rewritten with fresh
// ComputeDescriptorWrite entries every call afterward - mirrors
// RenderGraphPersistentResourceCache's own "resolve once per stable
// identity, reuse forever" shape.
class RenderGraphComputeBindingCache {
public:
    explicit RenderGraphComputeBindingCache(Renderer& renderer) noexcept;

    // Allocates a VkDescriptorSet against `layout` the first time `passName`
    // is seen (from the engine's shared compute descriptor pool - see
    // Renderer::AllocateComputeDescriptorSet()), mints its opaque
    // DescriptorSetHandle once, then rewrites it against `writes` every
    // call. Logs and refuses (returns DescriptorSetHandle::Invalid) if
    // `layout` ever differs from what this name was first resolved against -
    // a pass's own descriptor set layout can never legitimately change
    // build-to-build. May throw std::runtime_error if the underlying
    // vkAllocateDescriptorSets call fails (first call for this name only).
    DescriptorSetHandle Resolve(
        const char* passName, VkDescriptorSetLayout layout, const std::vector<ComputeDescriptorWrite>& writes);

private:
    struct Entry {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        DescriptorSetHandle handle = DescriptorSetHandle::Invalid;
    };

    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    std::unordered_map<std::string, Entry> m_entries;
};

} // namespace gte::rg
