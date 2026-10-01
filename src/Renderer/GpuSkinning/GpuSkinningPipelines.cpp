#include "GpuSkinningPipelines.h"

#include "../Renderer.h"

namespace gte {

GpuSkinningPipelines::~GpuSkinningPipelines()
{
    // task_manager/better-render-pass-1 campaign, PHASE4
    // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md) -
    // m_positionNormalPipeline/m_positionNormalUvPipeline (both
    // ComputePipeline, RAII) now own and destroy
    // m_positionNormalLayout/m_positionNormalUvLayout themselves (both are
    // BORROWED from their own pipeline's own ReflectedDescriptorSetLayout(0)
    // - see ComputePipeline::Destroy()'s own m_ownedReflectedLayouts
    // cleanup). This destructor therefore does NOT call
    // vkDestroyDescriptorSetLayout() on either layout anymore - doing so
    // would be a genuine double-free of handles this class no longer owns.
    // See PHASE4_COMPLETION_REPORT.md for the full "destructor double-free
    // fix" writeup (first found/fixed for CullingPipelines in this same
    // migration batch).
}

void GpuSkinningPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) {
        return;
    }

    // task_manager/better-render-pass-1 campaign, PHASE4
    // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md) -
    // path-only CreateComputePipeline() calls: real SPIR-V reflection
    // (ShaderReflection.h, PHASE1/PHASE2) builds each descriptor-set layout
    // and push-constant range directly from each shader's own compiled
    // binding/push-constant metadata, instead of a hand-built
    // DescriptorSetLayoutBuilder layout + a manually restated
    // VkPushConstantRange shared (incorrectly implying a single range) by
    // both variants.

    // PositionNormal variant - bindings 0-3, per GpuSkinningTypes.h's own
    // documented table (see SkinVerticesPositionNormal.comp's own header
    // comment for the full per-binding reasoning).
    m_positionNormalPipeline.emplace(renderer.CreateComputePipeline("shaders/SkinVerticesPositionNormal.comp.spv"));
    m_positionNormalLayout = m_positionNormalPipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    // PositionNormalUv variant - bindings 0-4, adding the bind-pose UV
    // buffer at binding 4 (a Phase 2 addition on top of GpuSkinningTypes.h's
    // own 4-binding table - see SkinVerticesPositionNormalUv.comp's own
    // header comment for why this is additive, not a redefinition).
    m_positionNormalUvPipeline.emplace(
        renderer.CreateComputePipeline("shaders/SkinVerticesPositionNormalUv.comp.spv"));
    m_positionNormalUvLayout = m_positionNormalUvPipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

} // namespace gte
