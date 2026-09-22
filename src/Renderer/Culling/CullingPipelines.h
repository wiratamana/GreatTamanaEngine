#pragma once

#include "../ComputeDescriptorSet.h"
#include "../ComputePipeline.h"

#include <volk.h>

#include <cstdint>
#include <optional>

namespace gte {

class Renderer;

// ============================================================================
// GPU-Driven Frustum Culling + Indirect Draw (render-pass-5) - PHASE3: the
// real culling compute shader + pipeline plumbing.
// ============================================================================
// See task_manager/render-pass-5/PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md
// for the full design reasoning. This class is the C++-side counterpart to
// src/Shaders/FrustumCull.comp - it builds the ONE shared descriptor-set
// layout (3 storage buffers, per FrustumCull.comp's own binding-table
// header comment) and the ONE shared ComputePipeline that shader compiles
// into. Mirrors src/Renderer/GpuSkinning/GpuSkinningPipelines.h EXACTLY (see
// that file for the identical class shape this one copies) - confirmed
// against the real, shipped source during this campaign's own dedicated
// PHASE3 pre-implementation strategy-document double-check
// (PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md).
//
// Deliberately does NOT decide dispatch math, descriptor-set ALLOCATION per
// batch, or per-batch buffer/resource lifetime - that is PHASE4's job (the
// per-batch GPU-driven-batch resource cache). This class only ever answers
// "how do I build/hold the one shared, per-shader pipeline object every
// batch's own culling dispatch will bind against" - mirroring
// ComputePipeline.h's own class comment.
//
// EnsureInitialized() is idempotent and lazy (built on first use, mirroring
// GpuSkinningPipelines::EnsureInitialized()/ComputeBlurValidation's own
// EnsureInitialized() pattern exactly) - a caller that never actually uses
// GPU-driven batching never pays the cost of compiling/loading this one
// SPIR-V module or allocating its descriptor-set layout at all.
//
// Construct one of these and keep it alive for as long as ANY GPU-driven
// batch might be in use - PHASE4's per-batch resource cache is expected to
// own the single instance of this class (see that phase's own strategy
// document), exactly like GpuSkinningPipelines is owned by GPU Skinning's
// own per-model resource cache.
class CullingPipelines {
public:
    CullingPipelines() = default;
    ~CullingPipelines();

    CullingPipelines(const CullingPipelines&) = delete;
    CullingPipelines& operator=(const CullingPipelines&) = delete;

    // Not move-enabled - this class is meant to be constructed once and
    // held by reference/pointer from wherever owns it (PHASE4's per-batch
    // cache), exactly like GpuSkinningPipelines/GpuResourceFactory/Renderer
    // themselves are never moved once real Vulkan resources exist behind
    // them.
    CullingPipelines(CullingPipelines&&) = delete;
    CullingPipelines& operator=(CullingPipelines&&) = delete;

    // Builds the descriptor-set layout and the ComputePipeline the first
    // time this is called; every subsequent call is a no-op. Safe to call
    // every frame from a hot path that only sometimes needs GPU-driven
    // culling (mirrors GpuSkinningPipelines::EnsureInitialized()'s own
    // documented contract).
    void EnsureInitialized(Renderer& renderer);

    bool IsInitialized() const noexcept { return m_pipeline.has_value(); }

    // Binding 0 (per-instance input) / binding 1 (output indirect-command
    // array) / binding 2 (atomic visible-count buffer) - see
    // Shaders/FrustumCull.comp's own header comment for the full
    // per-binding reasoning.
    VkDescriptorSetLayout DescriptorSetLayout() const noexcept { return m_layout; }
    const ComputePipeline& Pipeline() const noexcept { return *m_pipeline; }

private:
    VkDevice m_device = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;

    std::optional<ComputePipeline> m_pipeline;
};

// MUST match Shaders/FrustumCull.comp's own `layout(local_size_x = 256) in;`
// exactly - see ComputeDispatch.h's own header comment on why this pairing
// is a hand-maintained, per-shader convention, never enforced by the build
// system. Named and exposed here (rather than buried inside a .cpp) so
// PHASE4/5's future dispatch call site(s) can reference it directly instead
// of re-declaring a duplicate magic constant of their own - mirrors
// GpuSkinningPipelines.h's own kSkinningLocalSizeX precedent exactly.
inline constexpr std::uint32_t kCullingLocalSizeX = 256;

// The exact byte size of Shaders/FrustumCull.comp's own PushConstants block
// (6 vec4 planes + 2 uint = 96 + 4 + 4 = 104 bytes) - named here so every
// caller building a VkPushConstantRange against CullingPipelines::Pipeline()
// agrees on the same value, mirroring this file's own kCullingLocalSizeX
// precedent (a hand-maintained, GLSL-vs-C++ pairing, never tool-enforced).
inline constexpr std::uint32_t kCullingPushConstantSize = 104;

} // namespace gte
