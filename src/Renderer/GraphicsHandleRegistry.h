#pragma once
#include <volk.h>

#include "../Core/GraphicsHandles.h"

namespace gte {

// Renderer-internal only - the ONLY place DescriptorSetHandle/SamplerHandle/
// ImageViewHandle/DescriptorSetLayoutHandle (Core/GraphicsHandles.h) convert
// to/from a real Vk* value. Every Register*() mints a fresh, generation-
// checked handle wrapping `value`; every Native() returns VK_NULL_HANDLE for
// an invalid/stale/Invalid handle rather than asserting - mirrors
// ResourcePool::TryGet()'s existing "safe to query a dead handle" guarantee.
DescriptorSetHandle RegisterDescriptorSet(VkDescriptorSet value) noexcept;
VkDescriptorSet Native(DescriptorSetHandle handle) noexcept;
void UnregisterDescriptorSet(DescriptorSetHandle handle) noexcept;

SamplerHandle RegisterSampler(VkSampler value) noexcept;
VkSampler Native(SamplerHandle handle) noexcept;
void UnregisterSampler(SamplerHandle handle) noexcept;

ImageViewHandle RegisterImageView(VkImageView value) noexcept;
VkImageView Native(ImageViewHandle handle) noexcept;
void UnregisterImageView(ImageViewHandle handle) noexcept;

DescriptorSetLayoutHandle RegisterDescriptorSetLayout(VkDescriptorSetLayout value) noexcept;
VkDescriptorSetLayout Native(DescriptorSetLayoutHandle handle) noexcept;
void UnregisterDescriptorSetLayout(DescriptorSetLayoutHandle handle) noexcept;

} // namespace gte
