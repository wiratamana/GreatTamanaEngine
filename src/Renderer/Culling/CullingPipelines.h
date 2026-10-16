#pragma once

#include "CullingTypes.h"
#include "../../Core/GraphicsHandles.h"

#include <volk.h>

#include <cstdint>
#include <memory>

namespace gte {

class ComputePipeline;
class Renderer;

namespace rg {
class CommandBuffer;
} // namespace rg

// Byte size of FrustumCull.comp's PushConstants block (6 vec4 planes + 2 uint).
inline constexpr std::uint32_t kCullingPushConstantSize = 104;

// Push constants of FrustumCull.comp.
struct CullingPushConstants {
    Plane planes[6];
    std::uint32_t instanceCount;
    std::uint32_t useCompaction;
};
static_assert(sizeof(CullingPushConstants) == kCullingPushConstantSize, "layout differs from FrustumCull.comp");

// The one shared descriptor-set layout and compute pipeline of src/Shaders/FrustumCull.comp.
// Built lazily on first use; the layout is borrowed from the pipeline's reflection.
class CullingPipelines {
public:
    CullingPipelines();
    ~CullingPipelines();

    CullingPipelines(const CullingPipelines&) = delete;
    CullingPipelines& operator=(const CullingPipelines&) = delete;

    // Not movable: held by reference from its owner once Vulkan resources exist.
    CullingPipelines(CullingPipelines&&) = delete;
    CullingPipelines& operator=(CullingPipelines&&) = delete;

    // Builds the pipeline on the first call; later calls are no-ops.
    void EnsureInitialized(Renderer& renderer);

    bool IsInitialized() const noexcept { return m_pipeline != nullptr; }

    // Binding 0 per-instance input, 1 output indirect commands, 2 atomic visible count.
    // Borrowed from the pipeline, which owns and destroys it.
    VkDescriptorSetLayout DescriptorSetLayout() const noexcept { return m_layout; }

    // Records one culling dispatch (a thread per instance) with `set` bound. Needs EnsureInitialized().
    void RecordDispatch(rg::CommandBuffer& cmd, DescriptorSetHandle set, const CullingPushConstants& push) const;

private:
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    std::unique_ptr<ComputePipeline> m_pipeline;
};

} // namespace gte
