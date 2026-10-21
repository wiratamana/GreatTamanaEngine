#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

struct DescriptorWrite {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    // Buffer descriptor fields
    VkBuffer buffer = VK_NULL_HANDLE;
    uint64_t bufferOffset = 0;
    uint64_t bufferRange = VK_WHOLE_SIZE;

    // Generational engine handle of the buffer behind `buffer`; 0 for every other write. Two writes with
    // the same VkBuffer value and a different identity are different resources.
    std::uint64_t identity = 0;

    DescriptorWrite& WithIdentity(std::uint64_t value) noexcept
    {
        identity = value;
        return *this;
    }

    // Image descriptor fields
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler sampler = VK_NULL_HANDLE;

    // Factory helpers
    static DescriptorWrite StorageBuffer(
        std::uint32_t binding, VkBuffer buffer, uint64_t offset = 0, uint64_t range = VK_WHOLE_SIZE);

    static DescriptorWrite UniformBuffer(
        std::uint32_t binding, VkBuffer buffer, uint64_t offset = 0, uint64_t range = VK_WHOLE_SIZE);

    static DescriptorWrite StorageImage(std::uint32_t binding, VkImageView imageView);

    static DescriptorWrite CombinedImageSampler(
        std::uint32_t binding, VkImageView imageView, VkSampler sampler);

    friend bool operator==(const DescriptorWrite&, const DescriptorWrite&) = default;
};

class DescriptorSet {
public:
    DescriptorSet() = default;
    DescriptorSet(VkDescriptorSet descriptorSet, VkDevice device) noexcept
        : m_descriptorSet(descriptorSet)
        , m_device(device)
    {
    }

    VkDescriptorSet Native() const noexcept { return m_descriptorSet; }
    bool IsValid() const noexcept { return m_descriptorSet != VK_NULL_HANDLE; }

    void Rewrite(const std::vector<DescriptorWrite>& writes) const;

private:
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
};

} // namespace gte
