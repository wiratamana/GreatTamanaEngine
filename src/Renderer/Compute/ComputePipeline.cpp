#include "ComputePipeline.h"

#include "../Vulkan/ShaderModule.h"

#include <stdexcept>
#include <utility>

namespace gte {

ComputePipeline::ComputePipeline(VkDevice device, const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts, std::optional<VkPushConstantRange> pushConstantRange)
    : m_device(device)
{
    // No layouts or push constants passed: reflect them from the SPIR-V.
    const bool useReflection = descriptorSetLayouts.empty() && !pushConstantRange.has_value();

    std::vector<VkDescriptorSetLayout> resolvedSetLayouts = descriptorSetLayouts;
    std::optional<VkPushConstantRange> resolvedPushConstantRange = pushConstantRange;

    if (useReflection) {
        const ShaderStageFile computeStage{ VK_SHADER_STAGE_COMPUTE_BIT, shaderSpirvPath };
        ShaderReflectionResult reflection = ReflectShader(std::span<const ShaderStageFile>(&computeStage, 1));

        m_setLayouts = ReflectedSetLayouts(device, reflection.descriptorBindings, shaderSpirvPath);
        resolvedSetLayouts.assign(m_setLayouts.All().begin(), m_setLayouts.All().end());

        if (reflection.hasPushConstantRange) {
            VkPushConstantRange range{};
            range.stageFlags = reflection.pushConstantStages;
            range.offset = reflection.pushConstantRange.offset;
            range.size = reflection.pushConstantRange.size;
            resolvedPushConstantRange = range;
            m_pushConstantSize = reflection.pushConstantRange.size;
        }

        m_localSize.width = reflection.localSize.x;
        m_localSize.height = reflection.localSize.y;
        m_localSize.depth = reflection.localSize.z;
        m_reflectedBindings = std::move(reflection.descriptorBindings);
    }

    VkShaderModule computeModule = LoadShaderModule(device, shaderSpirvPath);

    try {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<std::uint32_t>(resolvedSetLayouts.size());
        layoutInfo.pSetLayouts = resolvedSetLayouts.empty() ? nullptr : resolvedSetLayouts.data();
        if (resolvedPushConstantRange.has_value()) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &resolvedPushConstantRange.value();
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
    , m_setLayouts(std::move(other.m_setLayouts))
    , m_reflectedBindings(std::move(other.m_reflectedBindings))
    , m_pushConstantSize(std::exchange(other.m_pushConstantSize, 0))
    , m_localSize(std::exchange(other.m_localSize, Extent3D{ 1, 1, 1 }))
{
    other.m_reflectedBindings.clear();
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
        m_setLayouts = std::move(other.m_setLayouts);
        m_reflectedBindings = std::move(other.m_reflectedBindings);
        other.m_reflectedBindings.clear();
        m_pushConstantSize = std::exchange(other.m_pushConstantSize, 0);
        m_localSize = std::exchange(other.m_localSize, Extent3D{ 1, 1, 1 });
    }
    return *this;
}

void ComputePipeline::Destroy() noexcept
{
    m_setLayouts = ReflectedSetLayouts{};
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
