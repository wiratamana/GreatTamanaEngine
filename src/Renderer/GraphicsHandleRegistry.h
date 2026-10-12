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

BufferObjectHandle RegisterBufferObject(VkBuffer value) noexcept;
VkBuffer Native(BufferObjectHandle handle) noexcept;
void UnregisterBufferObject(BufferObjectHandle handle) noexcept;

// Re-mints a SamplerHandle only when the underlying VkSampler actually
// changed since the last Resolve() call - a call site that re-resolves the
// SAME stable VkSampler every frame (the common case: a view's own render
// target sampler never changes unless that render target is itself resized
// or recreated) gets back the SAME handle instead of minting a brand-new,
// never-freed one every frame. VK_NULL_HANDLE always resolves to
// SamplerHandle::Invalid, never registered.
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
