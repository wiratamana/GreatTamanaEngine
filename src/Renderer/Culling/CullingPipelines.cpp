#include "CullingPipelines.h"

#include "../Compute/ComputePipeline.h"
#include "../Renderer.h"
#include "../RenderGraph/CommandBuffer.h"

#include <utility>

namespace gte {

CullingPipelines::CullingPipelines() = default;

// m_layout is borrowed from m_pipeline, which destroys it.
CullingPipelines::~CullingPipelines() = default;

void CullingPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) {
        return;
    }

    // Layout and push-constant range come from the shader's reflection.
    m_pipeline = std::make_unique<ComputePipeline>(renderer.CreateComputePipeline("shaders/FrustumCull.comp.spv"));
    m_layout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

void CullingPipelines::RecordDispatch(rg::CommandBuffer& cmd, DescriptorSetHandle set, const CullingPushConstants& push) const
{
    cmd.BindComputePipeline(*m_pipeline);
    cmd.BindDescriptorSet(set);
    cmd.SetPushConstants(push);
    cmd.DispatchOverSize(push.instanceCount, 1, 1);
}

} // namespace gte
