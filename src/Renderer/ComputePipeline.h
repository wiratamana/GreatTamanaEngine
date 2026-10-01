#pragma once

#include <volk.h>

#include "ComputeDispatch.h" // Extent3D - PHASE2 (better-render-pass-1), reflected local group size.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// RAII wrapper around a VkPipeline bound to VK_PIPELINE_BIND_POINT_COMPUTE,
// plus its own VkPipelineLayout - the compute sibling of Pipeline (see
// Pipeline.h), built for Phase 2 of the compute-shader campaign (see
// COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md). Owns both for its
// entire lifetime: created in the constructor, destroyed in the destructor.
// Construct via Renderer::CreateComputePipeline()/GpuResourceFactory::
// CreateComputePipeline() rather than directly, same convention as
// Pipeline/Buffer/RenderTexture/Mesh.
//
// Deliberately independent of any specific compute workload - no
// RenderGraph awareness, no dispatch math (Phase 4). This class only ever
// answers "compile this one .comp file into a real, bindable compute
// pipeline."
//
// One .comp file compiles to exactly one ComputePipeline - no shader
// permutation/variant system, no hot-reload.
//
// `descriptorSetLayouts` is plural (unlike Pipeline's single, optional
// `materialSetLayout`) because a compute shader's storage buffers/images
// will very often live in a dedicated set distinct from any material set -
// see COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md for how a
// caller builds one via DescriptorSetLayoutBuilder. May be empty (a compute
// shader that only uses push constants, if that's ever needed).
//
// `pushConstantRange` is a plain, caller-supplied VkPushConstantRange
// (rather than this engine's fixed 128-byte graphics convention - see
// Pipeline.h's own class comment) since compute shaders' per-dispatch
// parameters vary far more per-shader than graphics' fixed model/viewProj
// pair - document each concrete shader's own push-constant layout as a
// local convention (a comment above that .comp file's own
// `layout(push_constant)` block), not a shared engine-wide struct.
// std::nullopt (the default) means "no push constants at all" for this
// pipeline.
//
// task_manager/better-render-pass-1 campaign, PHASE2
// (PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md) - when BOTH
// `descriptorSetLayouts` is empty AND `pushConstantRange` is std::nullopt
// (i.e. the caller passed nothing beyond `shaderSpirvPath` - the common
// case for every NEWLY-MIGRATED call site), this constructor reflects
// `shaderSpirvPath` itself (src/Renderer/Vulkan/ShaderReflection.h, PHASE1)
// and BUILDS the descriptor-set layout(s)/push-constant range from that
// reflection data instead of leaving them empty - see
// ReflectedDescriptorSetLayout()/PushConstantSize()/LocalGroupSize() below.
// The existing, fully-manual path (the caller supplies a non-empty
// `descriptorSetLayouts` and/or a real `pushConstantRange`) behaves exactly
// as it always has - byte-for-byte unchanged, zero reflection performed -
// this is the documented escape hatch for any exotic shader reflection
// cannot correctly express. See PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md,
// Step 2's own "RESOLVED" note for why this is decided purely from the
// VALUES already passed rather than a new enum parameter.
class ComputePipeline {
public:
    ComputePipeline(VkDevice device, const std::string& shaderSpirvPath,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
        std::optional<VkPushConstantRange> pushConstantRange = std::nullopt);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    ComputePipeline(ComputePipeline&& other) noexcept;
    ComputePipeline& operator=(ComputePipeline&& other) noexcept;

    VkPipeline Native() const noexcept { return m_pipeline; }
    VkPipelineLayout Layout() const noexcept { return m_layout; }

    // Only meaningful for a pipeline built via the REFLECTION path (see the
    // class comment above) - a manually-built pipeline has no reflected
    // metadata to report, so these three accessors return VK_NULL_HANDLE/0/
    // {1,1,1} respectively for it (never falsely synthesized data).
    //
    // Returns the owned VkDescriptorSetLayout reflection built for GLSL
    // `set` number `set` - VK_NULL_HANDLE if no reflected binding declared
    // that set number, or if this pipeline was built manually. Still owned
    // by THIS ComputePipeline (destroyed in Destroy()) - a caller may use
    // the returned handle (e.g. to build a matching VkDescriptorSet) but
    // must never destroy it itself.
    VkDescriptorSetLayout ReflectedDescriptorSetLayout(std::uint32_t set = 0) const noexcept;
    std::uint32_t PushConstantSize() const noexcept { return m_pushConstantSize; }
    Extent3D LocalGroupSize() const noexcept { return m_localSize; }

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // Reflection-path-only state - stays empty/zero for a manually-built
    // pipeline (see the class comment above). m_ownedReflectedSetNumbers[i]
    // is the real GLSL `set` number backing m_ownedReflectedLayouts[i] - the
    // two vectors are always the same size and index in lockstep. These
    // layouts are BUILT by this ComputePipeline (the caller never supplied
    // one in the reflection path), so, unlike the manual path's
    // caller-owned layouts, THIS class owns them and destroys them in
    // Destroy().
    std::vector<VkDescriptorSetLayout> m_ownedReflectedLayouts;
    std::vector<std::uint32_t> m_ownedReflectedSetNumbers;
    std::uint32_t m_pushConstantSize = 0;
    Extent3D m_localSize{ 1, 1, 1 };
};

} // namespace gte
