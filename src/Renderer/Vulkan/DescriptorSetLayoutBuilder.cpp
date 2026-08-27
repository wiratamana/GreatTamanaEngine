#include "DescriptorSetLayoutBuilder.h"

#include <stdexcept>

namespace gte {

DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::AddBinding(
    std::uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, std::uint32_t count)
{
    VkDescriptorSetLayoutBinding entry{};
    entry.binding = binding;
    entry.descriptorType = type;
    entry.descriptorCount = count;
    entry.stageFlags = stageFlags;
    m_bindings.push_back(entry);
    return *this;
}

DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::AddStorageBuffer(
    std::uint32_t binding, VkShaderStageFlags stageFlags, std::uint32_t count)
{
    return AddBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stageFlags, count);
}

DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::AddStorageImage(
    std::uint32_t binding, VkShaderStageFlags stageFlags, std::uint32_t count)
{
    return AddBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stageFlags, count);
}

DescriptorSetLayoutBuilder& DescriptorSetLayoutBuilder::AddCombinedImageSampler(
    std::uint32_t binding, VkShaderStageFlags stageFlags, std::uint32_t count)
{
    return AddBinding(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stageFlags, count);
}

VkDescriptorSetLayout DescriptorSetLayoutBuilder::Build() const
{
    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<std::uint32_t>(m_bindings.size());
    createInfo.pBindings = m_bindings.empty() ? nullptr : m_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(m_device, &createInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetLayoutBuilder: vkCreateDescriptorSetLayout failed.");
    }
    return layout;
}

} // namespace gte
