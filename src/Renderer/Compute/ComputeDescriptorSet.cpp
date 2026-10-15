#include "ComputeDescriptorSet.h"

#include <cassert>

namespace gte {

ComputeDescriptorWrite ComputeDescriptorWrite::StorageBuffer(
    std::uint32_t binding, VkBuffer buffer, uint64_t offset, uint64_t range)
{
    ComputeDescriptorWrite write;
    write.binding = binding;
    write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.buffer = buffer;
    write.bufferOffset = offset;
    write.bufferRange = range;
    return write;
}

ComputeDescriptorWrite ComputeDescriptorWrite::UniformBuffer(
    std::uint32_t binding, VkBuffer buffer, uint64_t offset, uint64_t range)
{
    ComputeDescriptorWrite write = StorageBuffer(binding, buffer, offset, range);
    write.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    return write;
}

ComputeDescriptorWrite ComputeDescriptorWrite::StorageImage(std::uint32_t binding, VkImageView imageView)
{
    ComputeDescriptorWrite write;
    write.binding = binding;
    write.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.imageView = imageView;
    write.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    return write;
}

ComputeDescriptorWrite ComputeDescriptorWrite::CombinedImageSampler(
    std::uint32_t binding, VkImageView imageView, VkSampler sampler)
{
    ComputeDescriptorWrite write;
    write.binding = binding;
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.imageView = imageView;
    write.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    write.sampler = sampler;
    return write;
}

void ComputeDescriptorSet::Rewrite(const std::vector<ComputeDescriptorWrite>& writes) const
{
    assert(m_descriptorSet != VK_NULL_HANDLE && "ComputeDescriptorSet::Rewrite() called on an invalid descriptor set");
    if (writes.empty()) {
        return;
    }

    // Info structs must stay alive until vkUpdateDescriptorSets(); reserve so pointers into them stay valid.
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    bufferInfos.reserve(writes.size());
    imageInfos.reserve(writes.size());

    std::vector<VkWriteDescriptorSet> descriptorWrites;
    descriptorWrites.reserve(writes.size());

    for (const ComputeDescriptorWrite& write : writes) {
        VkWriteDescriptorSet vkWrite{};
        vkWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vkWrite.dstSet = m_descriptorSet;
        vkWrite.dstBinding = write.binding;
        vkWrite.descriptorCount = 1;
        vkWrite.descriptorType = write.type;

        if (write.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || write.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = write.buffer;
            bufferInfo.offset = static_cast<VkDeviceSize>(write.bufferOffset);
            bufferInfo.range = static_cast<VkDeviceSize>(write.bufferRange);
            bufferInfos.push_back(bufferInfo);
            vkWrite.pBufferInfo = &bufferInfos.back();
        } else {
            // Storage image or combined image sampler: a plain image-info write.
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = write.imageView;
            imageInfo.imageLayout = write.imageLayout;
            imageInfo.sampler = write.sampler;
            imageInfos.push_back(imageInfo);
            vkWrite.pImageInfo = &imageInfos.back();
        }

        descriptorWrites.push_back(vkWrite);
    }

    vkUpdateDescriptorSets(
        m_device, static_cast<std::uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

} // namespace gte
