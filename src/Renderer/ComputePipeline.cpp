#include "ComputePipeline.h"

#include "Vulkan/ShaderModule.h"

#include <stdexcept>
#include <utility>

namespace gte {

ComputePipeline::ComputePipeline(VkDevice device, const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts, std::optional<VkPushConstantRange> pushConstantRange)
    : m_device(device)
{
    // Only needed transiently, to build the VkPipeline below - destroyed
    // before this constructor returns (success or failure), same
    // convention as Pipeline's own vertex/fragment shader modules.
    VkShaderModule computeModule = LoadShaderModule(device, shaderSpirvPath);

    try {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<std::uint32_t>(descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = descriptorSetLayouts.empty() ? nullptr : descriptorSetLayouts.data();
        if (pushConstantRange.has_value()) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange.value();
        }

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
            throw std::runtime_error("ComputePipeline: vkCreatePipelineLayout failed.");
        }

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = computeModule;
        stage.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stage;
        pipelineInfo.layout = m_layout;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
            throw std::runtime_error("ComputePipeline: vkCreateComputePipelines failed.");
        }
    } catch (...) {
        vkDestroyShaderModule(device, computeModule, nullptr);
        throw;
    }

    vkDestroyShaderModule(device, computeModule, nullptr);
}

ComputePipeline::~ComputePipeline()
{
    Destroy();
}

ComputePipeline::ComputePipeline(ComputePipeline&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_layout(std::exchange(other.m_layout, VK_NULL_HANDLE))
    , m_pipeline(std::exchange(other.m_pipeline, VK_NULL_HANDLE))
{
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
    }
    return *this;
}

void ComputePipeline::Destroy() noexcept
{
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

} // namespace gte
