#pragma once

#include <volk.h>

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
// RenderGraph awareness, no descriptor-set-layout building of its own (that
// is Phase 3's job, src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h), no
// dispatch math (Phase 4). This class only ever answers "compile this one
// .comp file into a real, bindable compute pipeline."
//
// One .comp file compiles to exactly one ComputePipeline - no shader
// permutation/variant system, no hot-reload, no shader reflection (binding
// numbers/push-constant layout are a documented, hand-maintained
// convention between the C++ caller and the GLSL source - see this
// campaign's own "What We Will NOT Do" sections).
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

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace gte
