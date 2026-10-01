#include "CullingPipelines.h"

#include "../Renderer.h"

namespace gte {

CullingPipelines::~CullingPipelines()
{
    // task_manager/better-render-pass-1 campaign, PHASE4
    // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md) -
    // m_pipeline (a ComputePipeline, RAII) now owns and destroys m_layout
    // itself (it is BORROWED from m_pipeline->ReflectedDescriptorSetLayout(0)
    // - see ComputePipeline::Destroy()'s own m_ownedReflectedLayouts
    // cleanup). This destructor therefore does NOT call
    // vkDestroyDescriptorSetLayout() on m_layout anymore - doing so would be
    // a genuine double-free of a handle this class no longer owns. See
    // PHASE4_COMPLETION_REPORT.md for the full "destructor double-free fix"
    // writeup this migration batch's other migrated classes all repeat.
}

void CullingPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) {
        return;
    }

    // task_manager/better-render-pass-1 campaign, PHASE4
    // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md) - a
    // path-only CreateComputePipeline() call: real SPIR-V reflection
    // (ShaderReflection.h, PHASE1/PHASE2) builds the descriptor-set layout
    // and the push-constant range directly from Shaders/FrustumCull.comp.spv's
    // own compiled binding/push-constant metadata, instead of a hand-built
    // DescriptorSetLayoutBuilder layout + a manually restated
    // VkPushConstantRange.
    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/FrustumCull.comp.spv"));
    m_layout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

} // namespace gte
