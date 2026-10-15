#include "CullingPipelines.h"

#include "../Renderer.h"

namespace gte {

CullingPipelines::~CullingPipelines()
{
    // m_layout is borrowed from m_pipeline, which destroys it.
}

void CullingPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) {
        return;
    }

    // Layout and push-constant range come from the shader's reflection.
    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/FrustumCull.comp.spv"));
    m_layout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

} // namespace gte
