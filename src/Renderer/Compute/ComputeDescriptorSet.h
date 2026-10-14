#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

struct ComputeDescriptorWrite {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    // Buffer descriptor fields
    VkBuffer buffer = VK_NULL_HANDLE;
    uint64_t bufferOffset = 0;
    uint64_t bufferRange = VK_WHOLE_SIZE;

    // Image descriptor fields
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler sampler = VK_NULL_HANDLE;

    // Factory helpers
    static ComputeDescriptorWrite StorageBuffer(
        std::uint32_t binding, VkBuffer buffer, uint64_t offset = 0, uint64_t range = VK_WHOLE_SIZE);

    static ComputeDescriptorWrite StorageImage(std::uint32_t binding, VkImageView imageView);

    static ComputeDescriptorWrite CombinedImageSampler(
        std::uint32_t binding, VkImageView imageView, VkSampler sampler);
};

class ComputeDescriptorSet {
public:
    ComputeDescriptorSet() = default;
    ComputeDescriptorSet(VkDescriptorSet descriptorSet, VkDevice device) noexcept
        : m_descriptorSet(descriptorSet)
        , m_device(device)
    {
    }

    VkDescriptorSet Native() const noexcept { return m_descriptorSet; }
    bool IsValid() const noexcept { return m_descriptorSet != VK_NULL_HANDLE; }

    void Rewrite(const std::vector<ComputeDescriptorWrite>& writes) const;

private:
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
};

} // namespace gte
