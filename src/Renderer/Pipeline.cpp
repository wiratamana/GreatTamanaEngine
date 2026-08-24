#include "Pipeline.h"

#include "MeshVertex.h"
#include "Vertex.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gte {

namespace {

std::vector<char> ReadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "Pipeline: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
    }

    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& spirv)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size();
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(spirv.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Pipeline: vkCreateShaderModule failed.");
    }
    return module;
}

// True for a combined depth+stencil format - see VulkanDevice::
// PickDepthFormat() (which always prefers a depth-only format when the
// device supports one) and DepthBuffer::HasStencilComponent() (the same
// check, applied to an actual live DepthBuffer rather than a bare format).
bool DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT;
}

} // namespace

Pipeline::Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout)
    : m_device(device)
{
    const std::vector<char> vertSpirv = ReadFile(vertexShaderSpirvPath);
    const std::vector<char> fragSpirv = ReadFile(fragmentShaderSpirvPath);

    // Shader modules are only needed transiently, to build the VkPipeline
    // below - both are destroyed before this constructor returns (success
    // or failure), regardless of what vkCreateGraphicsPipelines does with
    // them.
    VkShaderModule vertModule = CreateShaderModule(device, vertSpirv);
    VkShaderModule fragModule = VK_NULL_HANDLE;
    try {
        fragModule = CreateShaderModule(device, fragSpirv);
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

        // Which binding/attribute description to build against - see
        // VertexLayout's own comment in Pipeline.h. Both structs happen to
        // produce the same SHAPE (one binding, two vec3 attributes) but
        // with different per-attribute semantics/offsets - selected here,
        // once, rather than duplicating this whole constructor per layout.
        const VkVertexInputBindingDescription binding = vertexLayout == VertexLayout::PositionNormal
            ? MeshVertex::BindingDescription()
            : Vertex::BindingDescription();
        const std::array<VkVertexInputAttributeDescription, 2> attributes = vertexLayout == VertexLayout::PositionNormal
            ? MeshVertex::AttributeDescriptions()
            : Vertex::AttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Actual viewport/scissor rectangles are supplied per-draw via
        // vkCmdSetViewport/vkCmdSetScissor (see FrameRecorder::RecordFrame)
        // since this same Pipeline can draw into targets of different sizes
        // (the swapchain vs. an Editor RenderTexture) - only the counts
        // matter here.
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        // No backface culling yet - avoids having to reason about winding
        // order for one hardcoded triangle. Revisit once real meshes with a
        // consistent winding convention exist.
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Standard "closer to the camera wins" depth test/write - see the
        // class comment in Pipeline.h for why this is unconditional (every
        // render target this pipeline draws into is always paired with a
        // real DepthBuffer now). VK_COMPARE_OP_LESS matches this engine's
        // [0,1] (near=0, far=1) depth range - see Math/Mat4.h's
        // PerspectiveFovLH_ZO.
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorBlendAttachment;

        const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        // One push constant range: a "model" Mat4 immediately followed by a
        // "viewProj" Mat4, vertex stage only - see the class comment in
        // Pipeline.h and Shaders/Triangle.vert's matching
        // `layout(push_constant)` block. Still no descriptor sets.
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 32; // two Mat4s (Math/Mat4.h) - column-major, matches GLSL mat4 layout.

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
            throw std::runtime_error("Pipeline: vkCreatePipelineLayout failed.");
        }

        // Dynamic rendering (no VkRenderPass/VkFramebuffer) - this pipeline
        // must be built against the exact color AND depth format it will
        // actually draw into. See AGENTS.md ("Render Target Format
        // Matching"). stencilAttachmentFormat is only set for a combined
        // depth+stencil format (DepthFormatHasStencil() above) - this
        // engine has no stencil use today, but the spec requires this field
        // to match the image's actual format whenever it also carries a
        // stencil aspect.
        const bool depthHasStencil = DepthFormatHasStencil(depthFormat);
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
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
{
}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
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
}

} // namespace gte
