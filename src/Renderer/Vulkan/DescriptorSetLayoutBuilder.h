#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace gte {

// Fluent VkDescriptorSetLayout builder: DescriptorSetLayoutBuilder(device).AddStorageBuffer(0).Build().
// Binding numbers must match the shader's `layout(binding = N)`; nothing checks it. Each Build()
// creates a new layout that the caller destroys.
class DescriptorSetLayoutBuilder {
public:
    explicit DescriptorSetLayoutBuilder(VkDevice device) noexcept
        : m_device(device)
    {
    }

    // `stageFlags` defaults to compute; OR in more stages for a shared binding.
    // `count` is the array size, almost always 1.
    DescriptorSetLayoutBuilder& AddStorageBuffer(
        std::uint32_t binding, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, std::uint32_t count = 1);
    DescriptorSetLayoutBuilder& AddStorageImage(
        std::uint32_t binding, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, std::uint32_t count = 1);
    DescriptorSetLayoutBuilder& AddCombinedImageSampler(
        std::uint32_t binding, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, std::uint32_t count = 1);

    DescriptorSetLayoutBuilder& AddUniformBuffer(
        std::uint32_t binding, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, std::uint32_t count = 1);

    // Creates a fresh layout from the bindings added so far; throws std::runtime_error on failure.
    // Does not consume the bindings, so it may be called again.
    VkDescriptorSetLayout Build() const;

private:
    DescriptorSetLayoutBuilder& AddBinding(
        std::uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, std::uint32_t count);

    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> m_bindings;
};

} // namespace gte
