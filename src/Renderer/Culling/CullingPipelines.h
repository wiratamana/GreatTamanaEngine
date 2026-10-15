#pragma once

#include "../Compute/ComputeDescriptorSet.h"
#include "../Compute/ComputePipeline.h"

#include <volk.h>

#include <cstdint>
#include <optional>

namespace gte {

class Renderer;

// The one shared descriptor-set layout and compute pipeline of src/Shaders/FrustumCull.comp.
// Built lazily on first use; the layout is borrowed from the pipeline's reflection.
class CullingPipelines {
public:
    CullingPipelines() = default;
    ~CullingPipelines();

    CullingPipelines(const CullingPipelines&) = delete;
    CullingPipelines& operator=(const CullingPipelines&) = delete;

    // Not movable: held by reference from its owner once Vulkan resources exist.
    CullingPipelines(CullingPipelines&&) = delete;
    CullingPipelines& operator=(CullingPipelines&&) = delete;

    // Builds the pipeline on the first call; later calls are no-ops.
    void EnsureInitialized(Renderer& renderer);

    bool IsInitialized() const noexcept { return m_pipeline.has_value(); }

    // Binding 0 per-instance input, 1 output indirect commands, 2 atomic visible count.
    // Borrowed from the pipeline, which owns and destroys it.
    VkDescriptorSetLayout DescriptorSetLayout() const noexcept { return m_layout; }
    const ComputePipeline& Pipeline() const noexcept { return *m_pipeline; }

private:
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;

    std::optional<ComputePipeline> m_pipeline;
};

// Byte size of FrustumCull.comp's PushConstants block (6 vec4 planes + 2 uint).
// Kept so the C++ push struct can static_assert against it at compile time.
inline constexpr std::uint32_t kCullingPushConstantSize = 104;

} // namespace gte
