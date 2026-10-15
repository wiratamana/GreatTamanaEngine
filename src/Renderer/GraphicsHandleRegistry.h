#pragma once
#include <volk.h>

#include "../Core/GraphicsHandles.h"

namespace gte {

// Renderer-internal: the only place the opaque graphics handles convert to and from Vk* values.
// Register*() mints a generation-checked handle; Native() returns VK_NULL_HANDLE for an invalid or stale one.
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

BufferObjectHandle RegisterBufferObject(VkBuffer value) noexcept;
VkBuffer Native(BufferObjectHandle handle) noexcept;
void UnregisterBufferObject(BufferObjectHandle handle) noexcept;

// Re-mints a SamplerHandle only when the VkSampler changed since the last Resolve(), so a stable sampler
// resolved every frame keeps one handle. VK_NULL_HANDLE resolves to SamplerHandle::Invalid.
struct CachedSamplerHandle {
    VkSampler native = VK_NULL_HANDLE;
    SamplerHandle handle = SamplerHandle::Invalid;

    SamplerHandle Resolve(VkSampler current) noexcept
    {
        if (current == native && handle != SamplerHandle::Invalid) {
            return handle;
        }
        if (handle != SamplerHandle::Invalid) {
            UnregisterSampler(handle);
        }
        native = current;
        handle = (current != VK_NULL_HANDLE) ? RegisterSampler(current) : SamplerHandle::Invalid;
        return handle;
    }
};

// ImageView sibling of CachedSamplerHandle above - identical reasoning.
struct CachedImageViewHandle {
    VkImageView native = VK_NULL_HANDLE;
    ImageViewHandle handle = ImageViewHandle::Invalid;

    ImageViewHandle Resolve(VkImageView current) noexcept
    {
        if (current == native && handle != ImageViewHandle::Invalid) {
            return handle;
        }
        if (handle != ImageViewHandle::Invalid) {
            UnregisterImageView(handle);
        }
        native = current;
        handle = (current != VK_NULL_HANDLE) ? RegisterImageView(current) : ImageViewHandle::Invalid;
        return handle;
    }
};

// Buffer sibling of CachedSamplerHandle above - identical reasoning.
struct CachedBufferObjectHandle {
    VkBuffer native = VK_NULL_HANDLE;
    BufferObjectHandle handle = BufferObjectHandle::Invalid;

    BufferObjectHandle Resolve(VkBuffer current) noexcept
    {
        if (current == native && handle != BufferObjectHandle::Invalid) {
            return handle;
        }
        if (handle != BufferObjectHandle::Invalid) {
            UnregisterBufferObject(handle);
        }
        native = current;
        handle = (current != VK_NULL_HANDLE) ? RegisterBufferObject(current) : BufferObjectHandle::Invalid;
        return handle;
    }
};


} // namespace gte
