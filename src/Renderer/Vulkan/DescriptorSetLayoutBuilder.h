#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

// Small, fluent helper for building a VkDescriptorSetLayout out of an
// arbitrary combination of the resource kinds a compute shader (or any
// other descriptor-set consumer) needs - see
// COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md. Exists purely to
// avoid every future compute-shader author hand-writing the same
// VkDescriptorSetLayoutCreateInfo boilerplate GpuResourceFactory's own
// constructor already writes once, by hand, for the single-binding
// material descriptor-set-layout (see GpuResourceFactory::
// MaterialDescriptorSetLayout()).
//
// Usage:
//   DescriptorSetLayoutBuilder builder(device);
//   VkDescriptorSetLayout layout = builder
//       .AddStorageBuffer(/*binding=*/0)
//       .AddStorageBuffer(/*binding=*/1)
//       .AddStorageImage(/*binding=*/2)
//       .Build();
//
// BINDING-NUMBER CONVENTION (a project convention, not tool-enforced - this
// campaign deliberately refuses shader reflection, see
// COMPUTE_SHADER_MASTER_STRATEGY_v2.md's own "What We Will NOT Do"):
// bindings are assigned in DECLARATION ORDER, matching the order a
// shader's own GLSL bindings are numbered, read top-to-bottom as:
//   (1) StructuredBuffer/read-only buffer inputs first,
//   (2) RWStructuredBuffer/read-write buffers next,
//   (3) Texture/read-only sampled inputs next,
//   (4) RWTexture/storage-image outputs last.
// Every concrete compute shader's own file-level comment block should
// restate its concrete binding numbers explicitly (mirroring how
// Shaders/TexturedMesh.vert/.frag already documents its own descriptor set
// 0 / binding 0 convention today) - this builder does not, and cannot,
// verify a caller's C++ bindings actually match a .comp file's own GLSL
// `layout(binding = ...)` declarations.
//
// Build() creates a brand NEW VkDescriptorSetLayout every time it's called
// - the caller owns the result and is responsible for destroying it
// (vkDestroyDescriptorSetLayout) once every ComputePipeline/descriptor set
// built against it is gone, exactly like any other VkDescriptorSetLayout
// this engine creates (see GpuResourceFactory's own m_materialSetLayout,
// destroyed in its Destroy()).
class DescriptorSetLayoutBuilder {
public:
    explicit DescriptorSetLayoutBuilder(VkDevice device) noexcept
        : m_device(device)
    {
    }

    // `stageFlags` defaults to VK_SHADER_STAGE_COMPUTE_BIT since this
    // builder exists primarily for compute descriptor sets - pass a
    // different mask (or OR several stages together) for a binding shared
    // with another shader stage. `count` (default 1) is an array binding
    // size - almost always 1 for the compute workloads this campaign
    // introduces (see this campaign's own "no bindless" refusal).
    DescriptorSetLayoutBuilder& AddStorageBuffer(
        std::uint32_t binding, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, std::uint32_t count = 1);
    DescriptorSetLayoutBuilder& AddStorageImage(
        std::uint32_t binding, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, std::uint32_t count = 1);
    DescriptorSetLayoutBuilder& AddCombinedImageSampler(
        std::uint32_t binding, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, std::uint32_t count = 1);

    // Builds and returns a fresh VkDescriptorSetLayout from every binding
    // added so far - throws std::runtime_error if vkCreateDescriptorSetLayout
    // fails. Safe to call more than once (e.g. to build two identical
    // layouts) - each call is independent and creates its own new handle;
    // this method does not mutate/consume the accumulated bindings.
    VkDescriptorSetLayout Build() const;

private:
    DescriptorSetLayoutBuilder& AddBinding(
        std::uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, std::uint32_t count);

    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> m_bindings;
};

} // namespace gte
