#include "GpuSkinningPipelines.h"

#include "../Renderer.h"
#include "../RenderGraph/GpuBufferInternal.h"

#include <vector>

namespace gte {

// The pipelines own the reflected layouts; this class only borrows them.
GpuSkinningPipelines::~GpuSkinningPipelines() = default;

void GpuSkinningPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) {
        return;
    }

    m_positionNormalPipeline.emplace(renderer.CreateComputePipeline("shaders/SkinVerticesPositionNormal.comp.spv"));
    m_positionNormalLayout = m_positionNormalPipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    m_positionNormalUvPipeline.emplace(
        renderer.CreateComputePipeline("shaders/SkinVerticesPositionNormalUv.comp.spv"));
    m_positionNormalUvLayout = m_positionNormalUvPipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

void GpuSkinningPipelines::BindBuffers(ComputeDescriptorSet& set, const rg::GpuBuffer& bindPose,
    const rg::GpuBuffer& skinWeights, const rg::GpuBuffer& boneMatrices, const rg::GpuBuffer& output,
    const rg::GpuBuffer* uv)
{
    using rg::GpuBufferAccess;
    std::vector<ComputeDescriptorWrite> writes;
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(0, GpuBufferAccess::Underlying(bindPose).Native()));
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(1, GpuBufferAccess::Underlying(skinWeights).Native()));
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(2, GpuBufferAccess::Underlying(boneMatrices).Native()));
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(3, GpuBufferAccess::Underlying(output).Native()));
    if (uv != nullptr) {
        writes.push_back(ComputeDescriptorWrite::StorageBuffer(4, GpuBufferAccess::Underlying(*uv).Native()));
    }
    set.Rewrite(writes);
}

} // namespace gte
