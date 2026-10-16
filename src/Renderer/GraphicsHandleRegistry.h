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

// Owns a VkDescriptorSetLayout plus its registry handle; the handle dies before the layout.
class OwnedDescriptorSetLayout {
public:
    OwnedDescriptorSetLayout() noexcept = default;
    OwnedDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout)
        : m_device(device), m_layout(layout)
    {
        if (m_layout != VK_NULL_HANDLE) {
            m_handle = RegisterDescriptorSetLayout(m_layout);
        }
    }
    ~OwnedDescriptorSetLayout() { Reset(); }

    OwnedDescriptorSetLayout(const OwnedDescriptorSetLayout&) = delete;
    OwnedDescriptorSetLayout& operator=(const OwnedDescriptorSetLayout&) = delete;
    OwnedDescriptorSetLayout(OwnedDescriptorSetLayout&& other) noexcept
        : m_device(other.m_device), m_layout(other.m_layout), m_handle(other.m_handle)
    {
        other.m_layout = VK_NULL_HANDLE;
        other.m_handle = DescriptorSetLayoutHandle::Invalid;
    }
    OwnedDescriptorSetLayout& operator=(OwnedDescriptorSetLayout&& other) noexcept
    {
        if (this != &other) {
            Reset();
            m_device = other.m_device;
            m_layout = other.m_layout;
            m_handle = other.m_handle;
            other.m_layout = VK_NULL_HANDLE;
            other.m_handle = DescriptorSetLayoutHandle::Invalid;
        }
        return *this;
    }

    VkDescriptorSetLayout Native() const noexcept { return m_layout; }
    DescriptorSetLayoutHandle Handle() const noexcept { return m_handle; }

private:
    void Reset() noexcept
    {
        if (m_handle != DescriptorSetLayoutHandle::Invalid) {
            UnregisterDescriptorSetLayout(m_handle);
            m_handle = DescriptorSetLayoutHandle::Invalid;
        }
        if (m_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
        }
    }

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    DescriptorSetLayoutHandle m_handle = DescriptorSetLayoutHandle::Invalid;
};

} // namespace gte
