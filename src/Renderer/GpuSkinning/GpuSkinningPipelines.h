#pragma once

#include "../Compute/ComputeDescriptorSet.h"
#include "../Compute/ComputePipeline.h"
#include "../RenderGraph/GpuBuffer.h"

#include <volk.h>

#include <cstdint>
#include <optional>

namespace gte {

class Renderer;

// The two GPU-skinning compute pipelines (untextured, and textured with a UV buffer at binding 4), built lazily.
// Keep one alive while any GPU-skinned model is in use.
class GpuSkinningPipelines {
public:
    GpuSkinningPipelines() = default;
    ~GpuSkinningPipelines();

    GpuSkinningPipelines(const GpuSkinningPipelines&) = delete;
    GpuSkinningPipelines& operator=(const GpuSkinningPipelines&) = delete;
    GpuSkinningPipelines(GpuSkinningPipelines&&) = delete;
    GpuSkinningPipelines& operator=(GpuSkinningPipelines&&) = delete;

    // Builds both pipelines on the first call; later calls do nothing.
    void EnsureInitialized(Renderer& renderer);

    bool IsInitialized() const noexcept { return m_positionNormalPipeline.has_value(); }

    // Bindings 0-3: bind pose, skin weights, bone matrices, output. Borrowed from the pipeline.
    VkDescriptorSetLayout PositionNormalDescriptorSetLayout() const noexcept { return m_positionNormalLayout; }
    const ComputePipeline& PositionNormalPipeline() const noexcept { return *m_positionNormalPipeline; }

    // Bindings 0-4: the same plus the UV buffer. Borrowed from the pipeline.
    VkDescriptorSetLayout PositionNormalUvDescriptorSetLayout() const noexcept { return m_positionNormalUvLayout; }
    const ComputePipeline& PositionNormalUvPipeline() const noexcept { return *m_positionNormalUvPipeline; }

    // Writes one model group's buffers into a set made from the matching layout; `uv` is set only for the textured variant.
    static void BindBuffers(ComputeDescriptorSet& set, const rg::GpuBuffer& bindPose, const rg::GpuBuffer& skinWeights,
        const rg::GpuBuffer& boneMatrices, const rg::GpuBuffer& output, const rg::GpuBuffer* uv);

private:
    VkDescriptorSetLayout m_positionNormalLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_positionNormalUvLayout = VK_NULL_HANDLE;

    std::optional<ComputePipeline> m_positionNormalPipeline;
    std::optional<ComputePipeline> m_positionNormalUvPipeline;
};

// Must match `local_size_x` of both skinning shaders. Only the editor's raw skinning validation still reads it.
inline constexpr std::uint32_t kSkinningLocalSizeX = 256;

} // namespace gte
