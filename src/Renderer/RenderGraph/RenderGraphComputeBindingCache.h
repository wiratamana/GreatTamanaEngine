#pragma once

#include "../Compute/ComputeDescriptorSet.h"
#include "../GraphicsHandleRegistry.h"
#include "RenderGraphHandles.h" // ViewScope - see Resolve()'s own doc comment below.

#include <string>
#include <unordered_map>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Owns every VkDescriptorSet the compute-binding API (RenderGraphBuilder::
// PassBuilder::Bind*()) allocates - keyed by a pass's own stable name
// COMBINED WITH its ViewScope (same string-literal identity convention
// AddPass()/AddRenderPass() already require for the name half). The
// ViewScope half of the key is required because a pass declared once per
// view (e.g. "AtmosphereAerialPerspectiveCompositePass", called once for
// GameView and once for SceneView, same literal name both times) would
// otherwise collapse onto ONE shared VkDescriptorSet - a genuine
// cross-view data race: Vulkan does not snapshot descriptor contents at
// vkCmdBindDescriptorSets time, so the SECOND view's rewrite would
// silently corrupt the FIRST view's already-recorded dispatch before the
// GPU ever executes it. Keying by (name, viewScope) gives every
// legitimately-distinct per-view declaration of the same pass name its
// own real descriptor set, while a Shared-scope pass (declared once per
// frame, period) still resolves to exactly one, same as before. Resolves
// ONCE per (name, viewScope) identity, re-rewritten with fresh
// ComputeDescriptorWrite entries every call afterward - mirrors
// RenderGraphPersistentResourceCache's own "resolve once per stable
// identity, reuse forever" shape.
class RenderGraphComputeBindingCache {
public:
    explicit RenderGraphComputeBindingCache(Renderer& renderer) noexcept;

    // Allocates a VkDescriptorSet against `layout` the first time this
    // (passName, viewScope) pair is seen (from the engine's shared compute
    // descriptor pool - see Renderer::AllocateComputeDescriptorSet()),
    // mints its opaque DescriptorSetHandle once, then rewrites it against
    // `writes` every call. Logs and refuses (returns DescriptorSetHandle::
    // Invalid) if `layout` ever differs from what this identity was first
    // resolved against - a pass's own descriptor set layout can never
    // legitimately change build-to-build. May throw std::runtime_error if
    // the underlying vkAllocateDescriptorSets call fails (first call for
    // this identity only).
    DescriptorSetHandle Resolve(const char* passName, ViewScope viewScope, VkDescriptorSetLayout layout,
        const std::vector<ComputeDescriptorWrite>& writes);

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
