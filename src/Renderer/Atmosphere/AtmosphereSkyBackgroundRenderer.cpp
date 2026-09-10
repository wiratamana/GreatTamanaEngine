#include "AtmosphereSkyBackgroundRenderer.h"

#include "../ComputeDescriptorSet.h"
#include "../Renderer.h"
#include "../Vulkan/DescriptorSetLayoutBuilder.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

std::vector<char> ReadShaderFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("AtmosphereSkyBackgroundRenderer: failed to open shader file '" + path +
            "' - was it compiled? See cmake/CompileShaders.cmake.");
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
        throw std::runtime_error("AtmosphereSkyBackgroundRenderer: vkCreateShaderModule failed.");
    }
    return module;
}

bool DepthFormatHasStencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT;
}

// Fragment-stage push constants - MUST match
// Shaders/AtmosphereSkyBackground.frag's own `layout(push_constant)` block
// exactly (mat4 + 2 floats + 2 padding floats = 80 bytes).
struct PushConstants {
    float invViewProjection[16];
    float eyeHeightKm;
    float planetRadiusKm;
    float _pad0;
    float _pad1;
};

} // namespace

AtmosphereSkyBackgroundRenderer::~AtmosphereSkyBackgroundRenderer()
{
    Reset();
}

void AtmosphereSkyBackgroundRenderer::Reset()
{
    if (m_device != VK_NULL_HANDLE && m_pipeline != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    // m_descriptorSetLayout is destroyed AFTER the pipeline layout built
    // against it (same ordering GpuResourceFactory's own Destroy() uses).
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    // m_descriptorSet is allocated from Renderer's own shared compute
    // descriptor pool - never individually freed (mirrors every other
    // ComputeDescriptorSet in this engine, e.g. AtmosphereLutRenderer's own
    // descriptor sets).
    m_descriptorSet = VK_NULL_HANDLE;
}

void AtmosphereSkyBackgroundRenderer::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline != VK_NULL_HANDLE) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    const VkDevice device = m_device;
    const VkFormat colorFormat = renderer.ColorFormat();
    const VkFormat depthFormat = renderer.DepthFormat();

    // Binding 0 = the Sky-View LUT combined image sampler - see
    // Shaders/AtmosphereSkyBackground.frag exactly. Fragment stage only
    // (this pipeline's vertex stage reads no descriptors at all).
    DescriptorSetLayoutBuilder layoutBuilder(device);
    m_descriptorSetLayout =
        layoutBuilder.AddCombinedImageSampler(/*binding=*/0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();

    m_descriptorSet = renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout);

    const std::vector<char> vertSpirv = ReadShaderFile("shaders/AtmosphereSkyBackground.vert.spv");
    const std::vector<char> fragSpirv = ReadShaderFile("shaders/AtmosphereSkyBackground.frag.spv");

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

        // No vertex input at all - AtmosphereSkyBackground.vert synthesizes
        // its 3 full-screen-triangle vertices purely from gl_VertexIndex.
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // CONFIRMED depth convention (see this class's own header comment
        // and this phase's own strategy document's "Revision Notes"):
        // depth TEST enabled with VK_COMPARE_OP_EQUAL (not LESS - this pass
        // must survive ONLY at a pixel whose depth is still exactly the
        // frame's own clear value, 1.0), depth WRITE disabled (nothing is
        // ever meant to occlude the sky itself).
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Opaque overwrite - the sky IS the base layer for every pixel it
        // survives the depth test at (never blended with whatever might
        // already be there, since nothing real was ever drawn there).
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

        // Fragment-stage-only push constants - matches
        // Shaders/AtmosphereSkyBackground.frag's own layout(push_constant)
        // block exactly (mat4 invViewProjection + eyeHeightKm +
        // planetRadiusKm + 2 padding floats).
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("AtmosphereSkyBackgroundRenderer: vkCreatePipelineLayout failed.");
        }

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
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
            throw std::runtime_error("AtmosphereSkyBackgroundRenderer: vkCreateGraphicsPipelines failed.");
        }
    } catch (...) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

void AtmosphereSkyBackgroundRenderer::Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& viewProjection,
    VkImageView skyViewLutView, VkSampler skyViewLutSampler, float eyeHeightKm, float planetRadiusKm)
{
    m_device = renderer.GetVulkanContextInfo().device;

    Mat4 invViewProjection;
    if (!viewProjection.TryInverse(invViewProjection)) {
        return; // Degenerate camera matrix this frame - draw nothing rather than assert/crash.
    }

    EnsurePipeline(renderer);

    ComputeDescriptorSet descriptorSet(m_descriptorSet);
    descriptorSet.Rewrite(m_device,
        std::vector<ComputeDescriptorWrite>{
            ComputeDescriptorWrite::CombinedImageSampler(0, skyViewLutView, skyViewLutSampler),
        });


    PushConstants pushConstants{};
    std::memcpy(pushConstants.invViewProjection, invViewProjection.Data(), sizeof(pushConstants.invViewProjection));
    pushConstants.eyeHeightKm = eyeHeightKm;
    pushConstants.planetRadiusKm = planetRadiusKm;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace gte
