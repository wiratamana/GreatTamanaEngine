#include "Pipeline.h"

#include "MeshVertex.h"
#include "Vertex.h"
#include "Vulkan/ShaderModule.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gte {

namespace {

// True for a combined depth+stencil format.
bool DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT;
}

// Fills the binding/attribute descriptions for a layout; returns the binding count (0 for None, else 1).
std::uint32_t BuildVertexInputState(
    VertexLayout vertexLayout, VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes)
{
    switch (vertexLayout) {
    case VertexLayout::None:
        attributes.clear();
        return 0;
    case VertexLayout::PositionNormal:
    case VertexLayout::PositionNormalInstanced: {
        // Instanced shares PositionNormal's vertex input attributes; only the model matrix source differs.
        binding = MeshVertex::BindingDescription();
        const auto layoutAttributes = MeshVertex::AttributeDescriptions();
        attributes.assign(layoutAttributes.begin(), layoutAttributes.end());
        return 1;
    }
    case VertexLayout::PositionNormalUv: {
        binding = MeshVertexUv::BindingDescription();
        const auto layoutAttributes = MeshVertexUv::AttributeDescriptions();
        attributes.assign(layoutAttributes.begin(), layoutAttributes.end());
        return 1;
    }
    case VertexLayout::PositionColor:
    default: {
        binding = Vertex::BindingDescription();
        const auto layoutAttributes = Vertex::AttributeDescriptions();
        attributes.assign(layoutAttributes.begin(), layoutAttributes.end());
        return 1;
    }
    }
}

} // namespace

Pipeline::Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, VkDescriptorSetLayout materialSetLayout,
    const char* debugName, VkDescriptorSetLayout instanceBufferSetLayout, VkDescriptorSetLayout sceneServicesSetLayout)
    : Pipeline(device, std::span<const VkFormat>(&colorFormat, 1), depthFormat, vertexShaderSpirvPath,
          fragmentShaderSpirvPath, vertexLayout, materialSetLayout, debugName, instanceBufferSetLayout,
          sceneServicesSetLayout)
{
}

Pipeline::Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
    const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout,
    VkDescriptorSetLayout materialSetLayout, const char* debugName, VkDescriptorSetLayout instanceBufferSetLayout,
    VkDescriptorSetLayout sceneServicesSetLayout)
    : m_device(device)
    , m_debugName(debugName != nullptr ? debugName : std::string())
    , m_vertexLayout(vertexLayout)
{
    // The 8-attachment cap matches gte::rg::kMaxColorAttachments; debug-only like the other cap asserts.
    assert(colorFormats.size() <= kPipelineMaxColorAttachments
        && "Pipeline: colorFormats must be no larger than kPipelineMaxColorAttachments (8).");

    // Shader modules are only needed to build the pipeline; both are destroyed before returning, on success or failure.
    VkShaderModule vertModule = LoadShaderModule(device, vertexShaderSpirvPath);
    VkShaderModule fragModule = VK_NULL_HANDLE;
    try {
        fragModule = LoadShaderModule(device, fragmentShaderSpirvPath);
    } catch (...) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }
    try {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        // See BuildVertexInputState() above.
        VkVertexInputBindingDescription binding{};
        std::vector<VkVertexInputAttributeDescription> attributes;
        const std::uint32_t bindingCount = BuildVertexInputState(vertexLayout, binding, attributes);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = bindingCount;
        vertexInput.pVertexBindingDescriptions = bindingCount > 0 ? &binding : nullptr;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Viewport and scissor are set per draw (one Pipeline draws into targets of different sizes); only counts matter here.
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        // No backface culling.
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Depth test/write with LESS, matching the engine's [0,1] depth range (see PerspectiveFovLH_ZO).
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // One identical opaque blend state per color target (no blending, full RGBA write mask).
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size());
        for (VkPipelineColorBlendAttachmentState& state : colorBlendAttachments) {
            state.blendEnable = VK_FALSE;
            state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
        }

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = static_cast<std::uint32_t>(colorBlendAttachments.size());
        colorBlend.pAttachments = colorBlendAttachments.data();

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        // One push constant range: "model" Mat4 then "viewProj" Mat4, vertex stage. A set-0 combined image
        // sampler layout is added when `materialSetLayout` is set.
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 32; // two Mat4s (Math/Mat4.h) - column-major, matches GLSL mat4 layout.

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        // materialSetLayout and instanceBufferSetLayout both occupy set 0 and are never used together.
        m_hasSceneServicesSet = (sceneServicesSetLayout != VK_NULL_HANDLE);
        m_hasMaterialSet = (materialSetLayout != VK_NULL_HANDLE);

        assert(!(materialSetLayout != VK_NULL_HANDLE && instanceBufferSetLayout != VK_NULL_HANDLE)
            && "Pipeline: materialSetLayout and instanceBufferSetLayout are mutually exclusive - no Pipeline needs "
               "both at once.");

        const VkDescriptorSetLayout setZeroLayout =
            materialSetLayout != VK_NULL_HANDLE ? materialSetLayout : instanceBufferSetLayout;

        // Set layouts must be contiguous: with a scene-services set 1 and nothing at set 0, a zero-binding
        // filler layout owns set 0. `setLayouts` must outlive vkCreatePipelineLayout() below.
        std::vector<VkDescriptorSetLayout> setLayouts;
        if (sceneServicesSetLayout != VK_NULL_HANDLE) {
            if (setZeroLayout != VK_NULL_HANDLE) {
                setLayouts = { setZeroLayout, sceneServicesSetLayout };
            } else {
                VkDescriptorSetLayoutCreateInfo fillerInfo{};
                fillerInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                fillerInfo.bindingCount = 0;
                if (vkCreateDescriptorSetLayout(device, &fillerInfo, nullptr, &m_syntheticSetZeroLayout) !=
                    VK_SUCCESS) {
                    throw std::runtime_error(
                        "Pipeline: vkCreateDescriptorSetLayout (synthetic set-0 filler) failed.");
                }
                setLayouts = { m_syntheticSetZeroLayout, sceneServicesSetLayout };
            }
        } else if (setZeroLayout != VK_NULL_HANDLE) {
            setLayouts = { setZeroLayout };
        }

        if (!setLayouts.empty()) {
            layoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
        }

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
            throw std::runtime_error("Pipeline: vkCreatePipelineLayout failed.");
        }

        // Dynamic rendering: the pipeline is built against the exact color and depth formats it draws into.
        // The stencil format is set only for a combined depth+stencil format, as the spec requires.
        const bool depthHasStencil = DepthFormatHasStencil(depthFormat);
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = depthFormat;
        renderingInfo.stencilAttachmentFormat = depthHasStencil ? depthFormat : VK_FORMAT_UNDEFINED;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(std::size(stages));
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_layout;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
            throw std::runtime_error("Pipeline: vkCreateGraphicsPipelines failed.");
        }
    } catch (...) {
        // The synthetic set-0 filler layout may already exist; no member owns it yet, so destroy it here.
        if (m_syntheticSetZeroLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_syntheticSetZeroLayout, nullptr);
            m_syntheticSetZeroLayout = VK_NULL_HANDLE;
        }
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

Pipeline::Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
    std::span<const std::byte> vertexSpirv, std::span<const std::byte> fragmentSpirv, VertexLayout vertexLayout,
    VkPrimitiveTopology topology, VkCullModeFlags cullMode, bool depthTest, bool depthWrite, VkCompareOp depthCompare,
    VkPipelineColorBlendAttachmentState blendState, std::span<const VkDescriptorSetLayout> setLayouts,
    VkShaderStageFlags pushConstantStages, std::uint32_t pushConstantByteSize, const char* debugName)
    : m_device(device)
    , m_debugName(debugName != nullptr ? debugName : std::string())
    , m_vertexLayout(vertexLayout)
{
    assert(colorFormats.size() <= kPipelineMaxColorAttachments
        && "Pipeline: colorFormats must be no larger than kPipelineMaxColorAttachments (8).");

    VkShaderModule vertModule = LoadShaderModule(device, vertexSpirv);
    VkShaderModule fragModule = VK_NULL_HANDLE;
    try {
        fragModule = LoadShaderModule(device, fragmentSpirv);
    } catch (...) {
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }
    try {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        std::vector<VkVertexInputAttributeDescription> attributes;
        const std::uint32_t bindingCount = BuildVertexInputState(vertexLayout, binding, attributes);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = bindingCount;
        vertexInput.pVertexBindingDescriptions = bindingCount > 0 ? &binding : nullptr;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = topology;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = cullMode;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = depthCompare;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // One identical blend state per color target; the single BlendMode applies to every attachment.
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size(), blendState);

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = static_cast<std::uint32_t>(colorBlendAttachments.size());
        colorBlend.pAttachments = colorBlendAttachments.data();

        // Dynamic depth bias too, so a bias change needs no new pipeline.
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = pushConstantStages;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (pushConstantByteSize > 0) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;
        }
        if (!setLayouts.empty()) {
            layoutInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
        }

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
            throw std::runtime_error("Pipeline: vkCreatePipelineLayout failed.");
        }

        const bool depthHasStencil = DepthFormatHasStencil(depthFormat);
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = depthFormat;
        renderingInfo.stencilAttachmentFormat = depthHasStencil ? depthFormat : VK_FORMAT_UNDEFINED;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(std::size(stages));
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_layout;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
            throw std::runtime_error("Pipeline: vkCreateGraphicsPipelines failed.");
        }
    } catch (...) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

Pipeline::~Pipeline()
{
    Destroy();
}

Pipeline::Pipeline(Pipeline&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_layout(std::exchange(other.m_layout, VK_NULL_HANDLE))
    , m_pipeline(std::exchange(other.m_pipeline, VK_NULL_HANDLE))
    , m_debugName(std::move(other.m_debugName))
    , m_vertexLayout(other.m_vertexLayout)
    , m_hasSceneServicesSet(other.m_hasSceneServicesSet)
    , m_hasMaterialSet(other.m_hasMaterialSet)
    , m_syntheticSetZeroLayout(std::exchange(other.m_syntheticSetZeroLayout, VK_NULL_HANDLE))
{
}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
        m_debugName = std::move(other.m_debugName);
        m_vertexLayout = other.m_vertexLayout;
        m_hasSceneServicesSet = other.m_hasSceneServicesSet;
        m_hasMaterialSet = other.m_hasMaterialSet;
        m_syntheticSetZeroLayout = std::exchange(other.m_syntheticSetZeroLayout, VK_NULL_HANDLE);
    }
    return *this;
}

void Pipeline::Destroy() noexcept
{
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    if (m_syntheticSetZeroLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_syntheticSetZeroLayout, nullptr);
        m_syntheticSetZeroLayout = VK_NULL_HANDLE;
    }
}

} // namespace gte
