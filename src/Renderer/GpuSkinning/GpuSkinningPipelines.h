#pragma once

#include "../ComputeDescriptorSet.h"
#include "../ComputePipeline.h"

#include <volk.h>

#include <cstdint>
#include <optional>

namespace gte {

class Renderer;

// ============================================================================
// GPU Vertex Skinning - Phase 2: Compute Kernel(s) - pipeline scaffolding.
// ============================================================================
// See task_manager/gpu_skinning/GPU_SKINNING_PHASE2_COMPUTE_KERNEL_STRATEGY_v1.md
// for the full design reasoning. This class is the C++-side counterpart to
// src/Shaders/SkinVerticesPositionNormal.comp and
// SkinVerticesPositionNormalUv.comp - it builds the two descriptor-set
// layouts (per GpuSkinningTypes.h's binding table, plus this phase's own
// binding-4 UV addition for the Uv variant - see that shader's own header
// comment) and the two ComputePipelines those shaders compile into.
//
// task_manager/better-render-pass-1 campaign, PHASE4
// (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md) -
// EnsureInitialized() now builds both ComputePipelines via a path-only
// Renderer::CreateComputePipeline() call (real SPIR-V reflection - see
// ComputePipeline.h's own PHASE2 class comment) instead of hand-building two
// DescriptorSetLayoutBuilder layouts plus a manually restated
// VkPushConstantRange. Both m_positionNormalLayout/m_positionNormalUvLayout
// are therefore now BORROWED from their own pipeline's own
// ReflectedDescriptorSetLayout(0) - owned and destroyed by that
// ComputePipeline itself, never by this class directly (see
// ~GpuSkinningPipelines() below).
//
// Deliberately does NOT decide dispatch math, descriptor-set ALLOCATION per
// model, or per-model buffer/mesh lifetime - that is Phase 4's job (the
// per-model GPU skinning resource cache). This class only ever answers "how
// do I build/hold the two shared, per-shader-variant pipeline objects every
// model's own skinning dispatch will bind against" - mirroring
// ComputePipeline.h's own class comment ("this class only ever answers
// 'compile this one .comp file into a real, bindable compute pipeline'").
//
// EnsureInitialized() is idempotent and lazy (built on first use, mirroring
// AssetPreviewMesh's own "build the pipeline once, reuse across every mesh
// asset selected afterwards" precedent, and ComputeBlurValidation's own
// EnsureInitialized() pattern exactly) - a caller that never actually uses
// GPU skinning never pays the cost of compiling/loading these two SPIR-V
// modules or allocating their descriptor-set layouts at all.
//
// Construct one of these and keep it alive for as long as ANY GPU-skinned
// model might be in use - Phase 4's per-model cache is expected to own the
// single instance of this class (see that phase's own strategy document).
class GpuSkinningPipelines {
public:
    GpuSkinningPipelines() = default;
    ~GpuSkinningPipelines();

    GpuSkinningPipelines(const GpuSkinningPipelines&) = delete;
    GpuSkinningPipelines& operator=(const GpuSkinningPipelines&) = delete;

    // Not move-enabled - this class is meant to be constructed once and
    // held by reference/pointer from wherever owns it (Phase 4's per-model
    // cache), exactly like GpuResourceFactory/Renderer themselves are never
    // moved once real Vulkan resources exist behind them.
    GpuSkinningPipelines(GpuSkinningPipelines&&) = delete;
    GpuSkinningPipelines& operator=(GpuSkinningPipelines&&) = delete;

    // Builds both reflection-based ComputePipelines the first time this is
    // called; every subsequent call is a no-op. Safe to call every frame
    // from a hot path that only sometimes needs GPU skinning (mirrors
    // ComputeBlurValidation::EnsureInitialized()'s own documented contract).
    void EnsureInitialized(Renderer& renderer);

    bool IsInitialized() const noexcept { return m_positionNormalPipeline.has_value(); }

    // Binding 0-3 only (bind pose / skin weights / bone matrices / output) -
    // see SkinVerticesPositionNormal.comp's own header comment for the full
    // per-binding reasoning. Used for an untextured model (VertexLayout::
    // PositionNormal / Mesh.vert/.frag's own GPU-skinned sibling). BORROWED
    // from m_positionNormalPipeline->ReflectedDescriptorSetLayout(0)
    // (better-render-pass-1 campaign, PHASE4) - owned/destroyed by that
    // pipeline itself, never by this class.
    VkDescriptorSetLayout PositionNormalDescriptorSetLayout() const noexcept { return m_positionNormalLayout; }
    const ComputePipeline& PositionNormalPipeline() const noexcept { return *m_positionNormalPipeline; }

    // Binding 0-4 (adds the bind-pose UV buffer at binding 4 - see
    // SkinVerticesPositionNormalUv.comp's own header comment for why this is
    // an ADDITIVE extension of GpuSkinningTypes.h's own 4-binding table,
    // used only by this Uv variant). Used for a textured model
    // (VertexLayout::PositionNormalUv / TexturedMesh.vert/.frag's own
    // GPU-skinned sibling). BORROWED from
    // m_positionNormalUvPipeline->ReflectedDescriptorSetLayout(0)
    // (better-render-pass-1 campaign, PHASE4) - owned/destroyed by that
    // pipeline itself, never by this class.
    VkDescriptorSetLayout PositionNormalUvDescriptorSetLayout() const noexcept { return m_positionNormalUvLayout; }
    const ComputePipeline& PositionNormalUvPipeline() const noexcept { return *m_positionNormalUvPipeline; }

private:
    VkDescriptorSetLayout m_positionNormalLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_positionNormalUvLayout = VK_NULL_HANDLE;

    std::optional<ComputePipeline> m_positionNormalPipeline;
    std::optional<ComputePipeline> m_positionNormalUvPipeline;
};

// MUST match both SkinVerticesPositionNormal.comp's and
// SkinVerticesPositionNormalUv.comp's own `layout(local_size_x = 256) in;`
// exactly - see ComputeDispatch.h's own header comment on why this pairing
// is a hand-maintained, per-shader convention, never enforced by the build
// system.
//
// task_manager/better-render-pass-1 campaign, PHASE4
// (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md) - both real
// PRODUCTION dispatch call sites (Core.cpp's offscreen "GpuSkinning"
// provider, Application/RenderPasses.cpp's AddGpuSkinningPasses() direct-
// render-to-swapchain fallback) no longer reference this constant at all -
// both migrated onto rg::CommandBuffer::DispatchOverSize(), which reads the
// bound ComputePipeline's own REFLECTED LocalGroupSize() instead (PHASE2/
// PHASE3). This constant is kept, NOT deleted, purely because
// src/Editor/GpuSkinningValidation.cpp's own one remaining real dispatch
// call site is a standalone Renderer::ImmediateSubmit() + raw vkCmd* call
// (never a RenderGraph PassContext, so rg::CommandBuffer does not apply
// there) and still references it directly - see that file's own call site.
inline constexpr std::uint32_t kSkinningLocalSizeX = 256;

} // namespace gte
