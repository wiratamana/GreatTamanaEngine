#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

// One entry in a ComputeDescriptorSet::Rewrite() call - describes a single
// binding's CURRENT physical resource to write into that descriptor set.
// Exactly one of the buffer fields or the image fields is meaningful,
// depending on `type` - see the three static factory helpers below, which
// are the intended way to build one of these (rather than filling out this
// struct's fields by hand and risking the "which group of fields applies"
// question getting answered wrong).
//
// See COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md for the full
// design reasoning - in particular why a descriptor set must be re-written
// (never assumed stable) whenever the physical resource behind a declared
// render-graph handle may have changed identity frame-to-frame
// (RenderGraphResourcePool may legitimately hand back a different
// underlying VkBuffer/VkImageView across frames).
struct ComputeDescriptorWrite {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    // Meaningful only when type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER - the
    // Vulkan object behind BOTH RWStructuredBuffer and StructuredBuffer
    // (see COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md - the
    // read-only-vs-read-write distinction is a GLSL/render-graph-level
    // concept, never a different descriptor type here).
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize bufferOffset = 0;
    VkDeviceSize bufferRange = VK_WHOLE_SIZE;

    // Meaningful only when type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE or
    // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER.
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Meaningful only when type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    // - a storage image binding has no sampler at all (imageLoad/imageStore
    // in GLSL never sample).
    VkSampler sampler = VK_NULL_HANDLE;

    // An RWStructuredBuffer/StructuredBuffer binding - `buffer` bound as
    // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER. `offset`/`range` default to the
    // whole buffer.
    static ComputeDescriptorWrite StorageBuffer(
        std::uint32_t binding, VkBuffer buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE);

    // An RWTexture binding - `imageView` bound as
    // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL (the only
    // layout a storage image can be read/written through in core Vulkan -
    // see COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md and
    // COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md).
    static ComputeDescriptorWrite StorageImage(std::uint32_t binding, VkImageView imageView);

    // A plain, read-only Texture binding - `imageView`/`sampler` bound as
    // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL - identical to how a
    // fragment shader already samples a MaterialTexture today.
    static ComputeDescriptorWrite CombinedImageSampler(
        std::uint32_t binding, VkImageView imageView, VkSampler sampler);
};

// A small, explicit value type wrapping a single VkDescriptorSet allocated
// against a compute-shaped VkDescriptorSetLayout (see
// Vulkan/DescriptorSetLayoutBuilder.h and GpuResourceFactory::
// AllocateComputeDescriptorSet()) - NOT a RAII owner: a descriptor set
// allocated from a shared pool is never individually freed, only the whole
// pool at once (exactly like MaterialTexture's own VkDescriptorSet - see
// MaterialTexture.h), so this struct owns no Vulkan handle to release.
//
// Call Rewrite() once per frame (or whenever the physical resource behind
// a declared render-graph handle may have changed identity - see
// RenderGraphResourcePool's own pooling behavior) with the CURRENT set of
// buffer/image handles to bind - this issues exactly one
// vkUpdateDescriptorSets() call covering every entry supplied. Cheap (one
// driver call, no persistent allocation beyond a few small, throwaway
// std::vectors) and always correct, at the cost of being slightly
// wasteful when the underlying resource happens not to have changed
// frame-to-frame - see
// COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md's own reasoning
// for why this is an acceptable, deliberate trade-off (matching this
// engine's existing "correctness over micro-optimization until proven
// necessary" discipline).
class ComputeDescriptorSet {
public:
    ComputeDescriptorSet() = default;
    explicit ComputeDescriptorSet(VkDescriptorSet descriptorSet) noexcept
        : m_descriptorSet(descriptorSet)
    {
    }

    VkDescriptorSet Native() const noexcept { return m_descriptorSet; }
    bool IsValid() const noexcept { return m_descriptorSet != VK_NULL_HANDLE; }

    // Writes every entry in `writes` into this descriptor set via a single
    // vkUpdateDescriptorSets() call. Safe (and expected) to call every
    // frame - see the class comment above for why this is deliberately not
    // optimized to skip unchanged bindings. Asserts (debug builds) that
    // this descriptor set is actually valid; a no-op for an empty `writes`.
    void Rewrite(VkDevice device, const std::vector<ComputeDescriptorWrite>& writes) const;

private:
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
};

} // namespace gte
