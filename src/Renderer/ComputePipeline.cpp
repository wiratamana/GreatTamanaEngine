#include "ComputePipeline.h"

#include "Vulkan/DescriptorSetLayoutBuilder.h"
#include "Vulkan/ShaderModule.h"
#include "Vulkan/ShaderReflection.h"

#include <stdexcept>
#include <utility>

namespace gte {

ComputePipeline::ComputePipeline(VkDevice device, const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts, std::optional<VkPushConstantRange> pushConstantRange)
    : m_device(device)
{
    // task_manager/better-render-pass-1 campaign, PHASE2
    // (PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md) - distinguish
    // "caller omitted both" (REFLECT shaderSpirvPath to build the
    // layout(s)/range) from "caller supplied at least one explicitly"
    // (MANUAL path, byte-for-byte the pre-PHASE2 behavior) purely from the
    // values already received - a new enum parameter was deliberately
    // rejected for this, see that doc's own Step 2 "RESOLVED" note.
    const bool useReflection = descriptorSetLayouts.empty() && !pushConstantRange.has_value();

    std::vector<VkDescriptorSetLayout> resolvedSetLayouts = descriptorSetLayouts;
    std::optional<VkPushConstantRange> resolvedPushConstantRange = pushConstantRange;

    // Cleans up any reflected VkDescriptorSetLayout(s) already built so far
    // - needed on every throwing path below the point they're created,
    // since a throwing constructor never runs this object's own destructor
    // (the object is never considered constructed).
    auto destroyOwnedReflectedLayouts = [&]() noexcept {
        for (VkDescriptorSetLayout layout : m_ownedReflectedLayouts) {
            if (layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, layout, nullptr);
            }
        }
        m_ownedReflectedLayouts.clear();
        m_ownedReflectedSetNumbers.clear();
    };

    if (useReflection) {
        try {
            const ShaderReflectionResult reflection = ReflectComputeShader(shaderSpirvPath);

            const std::vector<DescriptorBindingSetGroup> setGroups =
                GroupDescriptorBindingsBySet(reflection.descriptorBindings);
            resolvedSetLayouts.reserve(setGroups.size());
            m_ownedReflectedLayouts.reserve(setGroups.size());
            m_ownedReflectedSetNumbers.reserve(setGroups.size());

            for (const DescriptorBindingSetGroup& group : setGroups) {
                DescriptorSetLayoutBuilder layoutBuilder(device);
                for (const ReflectedDescriptorBinding& binding : group.bindings) {
                    switch (binding.type) {
                        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                            layoutBuilder.AddStorageBuffer(binding.binding, VK_SHADER_STAGE_COMPUTE_BIT, binding.count);
                            break;
                        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                            layoutBuilder.AddStorageImage(binding.binding, VK_SHADER_STAGE_COMPUTE_BIT, binding.count);
                            break;
                        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                            layoutBuilder.AddCombinedImageSampler(
                                binding.binding, VK_SHADER_STAGE_COMPUTE_BIT, binding.count);
                            break;
                        default:
                            // Unreachable: ReflectComputeShader() (PHASE1,
                            // ShaderReflection.cpp's own MapDescriptorType())
                            // already throws for any descriptor type other
                            // than these three - this check deliberately
                            // lives in exactly ONE place (there, not here),
                            // this is only a defensive, documented
                            // hard-fail tail.
                            throw std::runtime_error(
                                "ComputePipeline: reflected an unsupported descriptor type for '" + shaderSpirvPath +
                                "' - this should be unreachable (ReflectComputeShader() already validates this).");
                    }
                }

                const VkDescriptorSetLayout layout = layoutBuilder.Build();
                m_ownedReflectedLayouts.push_back(layout);
                m_ownedReflectedSetNumbers.push_back(group.set);
                resolvedSetLayouts.push_back(layout);
            }

            if (reflection.hasPushConstantRange) {
                VkPushConstantRange range{};
                range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                range.offset = reflection.pushConstantRange.offset;
                range.size = reflection.pushConstantRange.size;
                resolvedPushConstantRange = range;
                m_pushConstantSize = reflection.pushConstantRange.size;
            }

            m_localSize.width = reflection.localSize.x;
            m_localSize.height = reflection.localSize.y;
            m_localSize.depth = reflection.localSize.z;
        } catch (...) {
            destroyOwnedReflectedLayouts();
            throw;
        }
    }

    // Only needed transiently, to build the VkPipeline below - destroyed
    // before this constructor returns (success or failure), same
    // convention as Pipeline's own vertex/fragment shader modules.
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
        destroyOwnedReflectedLayouts();
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
    , m_ownedReflectedLayouts(std::move(other.m_ownedReflectedLayouts))
    , m_ownedReflectedSetNumbers(std::move(other.m_ownedReflectedSetNumbers))
    , m_pushConstantSize(std::exchange(other.m_pushConstantSize, 0))
    , m_localSize(std::exchange(other.m_localSize, Extent3D{ 1, 1, 1 }))
{
    other.m_ownedReflectedLayouts.clear();
    other.m_ownedReflectedSetNumbers.clear();
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
        m_ownedReflectedLayouts = std::move(other.m_ownedReflectedLayouts);
        m_ownedReflectedSetNumbers = std::move(other.m_ownedReflectedSetNumbers);
        m_pushConstantSize = std::exchange(other.m_pushConstantSize, 0);
        m_localSize = std::exchange(other.m_localSize, Extent3D{ 1, 1, 1 });
        other.m_ownedReflectedLayouts.clear();
        other.m_ownedReflectedSetNumbers.clear();
    }
    return *this;
}

VkDescriptorSetLayout ComputePipeline::ReflectedDescriptorSetLayout(std::uint32_t set) const noexcept
{
    for (std::size_t i = 0; i < m_ownedReflectedSetNumbers.size(); ++i) {
        if (m_ownedReflectedSetNumbers[i] == set) {
            return m_ownedReflectedLayouts[i];
        }
    }
    return VK_NULL_HANDLE;
}

void ComputePipeline::Destroy() noexcept
{
    for (VkDescriptorSetLayout layout : m_ownedReflectedLayouts) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
        }
    }
    m_ownedReflectedLayouts.clear();
    m_ownedReflectedSetNumbers.clear();

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
