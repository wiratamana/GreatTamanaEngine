#include "ComputeDescriptorSet.h"

#include <cassert>

namespace gte {

ComputeDescriptorWrite ComputeDescriptorWrite::StorageBuffer(
    std::uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
    ComputeDescriptorWrite write;
    write.binding = binding;
    write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.buffer = buffer;
    write.bufferOffset = offset;
    write.bufferRange = range;
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

void ComputeDescriptorSet::Rewrite(VkDevice device, const std::vector<ComputeDescriptorWrite>& writes) const
{
    assert(m_descriptorSet != VK_NULL_HANDLE && "ComputeDescriptorSet::Rewrite() called on an invalid descriptor set");
    if (writes.empty()) {
        return;
    }

    // Buffer/image info structs must stay alive until vkUpdateDescriptorSets()
    // is actually called below - held in parallel vectors, reserved up
    // front so pointers taken into them (while building each
    // VkWriteDescriptorSet) are never invalidated by a mid-loop
    // reallocation.
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

        if (write.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = write.buffer;
            bufferInfo.offset = write.bufferOffset;
            bufferInfo.range = write.bufferRange;
            bufferInfos.push_back(bufferInfo);
            vkWrite.pBufferInfo = &bufferInfos.back();
        } else {
            // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE or
            // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER - both are plain
            // image-info writes, differing only in imageLayout/whether a
            // sampler is set (see the two matching factory helpers above).
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
        device, static_cast<std::uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

} // namespace gte
