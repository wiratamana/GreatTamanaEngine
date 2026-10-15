#pragma once

#include <volk.h>

#include "../GraphicsHandleRegistry.h"
#include "../Vulkan/ReflectedSetLayouts.h"
#include "../Vulkan/ShaderReflection.h"
#include "ComputeDispatch.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte {

// RAII wrapper for compute VkPipeline and VkPipelineLayout.
// If layouts and push constants are not provided, it generates them via reflection.
class ComputePipeline {
public:
    ComputePipeline(VkDevice device, const std::string& shaderSpirvPath,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
        std::optional<VkPushConstantRange> pushConstantRange = std::nullopt);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    ComputePipeline(ComputePipeline&& other) noexcept;
    ComputePipeline& operator=(ComputePipeline&& other) noexcept;

    VkPipeline Native() const noexcept { return m_pipeline; }
    VkPipelineLayout Layout() const noexcept { return m_layout; }

    // Reflected metadata (null/0/{1,1,1} if built manually).
    VkDescriptorSetLayout ReflectedDescriptorSetLayout(std::uint32_t set = 0) const noexcept
    {
        return m_setLayouts.Native(set);
    }
    std::uint32_t PushConstantSize() const noexcept { return m_pushConstantSize; }
    Extent3D LocalGroupSize() const noexcept { return m_localSize; }

    // Every reflected binding of every set (empty if built manually).
    std::span<const ReflectedDescriptorBinding> ReflectedBindings() const noexcept { return m_reflectedBindings; }

    // Registers the reflected layout for `set` on first call. Invalid if `set` was not reflected.
    DescriptorSetLayoutHandle ReflectedDescriptorSetLayoutHandle(std::uint32_t set = 0) const noexcept
    {
        return m_setLayouts.Handle(set);
    }

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    ReflectedSetLayouts m_setLayouts;
    std::vector<ReflectedDescriptorBinding> m_reflectedBindings;
    std::uint32_t m_pushConstantSize = 0;
    Extent3D m_localSize{ 1, 1, 1 };
};

} // namespace gte
