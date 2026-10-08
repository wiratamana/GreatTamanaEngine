#include "ShadowCompositeRenderer.h"

#include "../../Renderer/ComputeDescriptorSet.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"
#include "ShadowRenderPassTags.h"

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
        throw std::runtime_error("ShadowCompositeRenderer: failed to open shader file '" + path + "'.");
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
        throw std::runtime_error("ShadowCompositeRenderer: vkCreateShaderModule failed.");
    }
    return module;
}

// Must match Shaders/ShadowComposite.frag's push-constant block exactly (4 bytes).
struct PushConstants {
    float strength;
};
} // namespace

ShadowCompositeRenderer::~ShadowCompositeRenderer()
{
    Reset();
}

void ShadowCompositeRenderer::Reset()
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
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    m_descriptorSetsByView.clear(); // Every entry is pool-owned - never individually freed.
}

void ShadowCompositeRenderer::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline != VK_NULL_HANDLE) {
        return;
    }
    m_device = renderer.GetVulkanContextInfo().device;
    const VkDevice device = m_device;
    const VkFormat colorFormat = renderer.ColorFormat();

    // Binding 0 = sceneColor, binding 1 = shadowMask - fragment-stage
    // combined image samplers, see ShadowComposite.frag.
    DescriptorSetLayoutBuilder layoutBuilder(device);
    m_descriptorSetLayout = layoutBuilder.AddCombinedImageSampler(0, VK_SHADER_STAGE_FRAGMENT_BIT)
        .AddCombinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .Build();

    const std::vector<char> vertSpirv = ReadShaderFile("shaders/ShadowComposite.vert.spv");
    const std::vector<char> fragSpirv = ReadShaderFile("shaders/ShadowComposite.frag.spv");
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

        // No vertex input - ShadowComposite.vert synthesizes 3 triangle vertices from gl_VertexIndex.
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

        // No depth test/write - screen-space pass into its own private target.
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        // No blending - shader writes final RGB/A directly; RenderFeatureBlend.comp
        // blends this private target onto the screen via ScreenSpaceMask afterward.
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
            throw std::runtime_error("ShadowCompositeRenderer: vkCreatePipelineLayout failed.");
        }

        // No depth attachment - this pass's private target is color-only
        // (see RenderFeatureCompositor's own "color-only" private/accum
        // target convention); must NOT declare depthAttachmentFormat here,
        // since no WriteDepthStencilAttachment() is ever bound for it.
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

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
            throw std::runtime_error("ShadowCompositeRenderer: vkCreateGraphicsPipelines failed.");
        }
    } catch (...) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

VkDescriptorSet ShadowCompositeRenderer::EnsureViewDescriptorSet(Renderer& renderer, const char* viewKey)
{
    const auto existing = m_descriptorSetsByView.find(viewKey);
    if (existing != m_descriptorSetsByView.end()) {
        return existing->second;
    }
    const VkDescriptorSet descriptorSet = renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout);
    return m_descriptorSetsByView.emplace(viewKey, descriptorSet).first->second;
}

void ShadowCompositeRenderer::AddCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const char* viewKey, rg::TextureHandle privateTarget, VkExtent2D extent, rg::TextureHandle sceneColorHandle,
    VkSampler sceneColorSampler, rg::TextureHandle maskHandle, rg::TextureHandle sceneDepthHandle, float strength)
{
    EnsurePipeline(renderer);
    const VkDescriptorSet descriptorSet = EnsureViewDescriptorSet(renderer, viewKey);

    builder.AddRenderPass(
        "Shadow.Composite.Draw", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [privateTarget, sceneColorHandle, maskHandle, sceneDepthHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(sceneColorHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(maskHandle, rg::ResourceAccess::ShaderRead);
            // Declare-only: this pass never samples scene depth, but
            // RenderFeatureCompositor requires every PostComposite feature to
            // acknowledge the view's depth sub-resource it was handed.
            pass.ReadTexture(sceneDepthHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.WriteColorAttachment(privateTarget);
        },
        [this, descriptorSet, sceneColorHandle, sceneColorSampler, maskHandle, extent, strength](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture sceneColor = ctx.resolveTexture(sceneColorHandle);
            const rg::PassContext::ResolvedTexture mask = ctx.resolveTexture(maskHandle);

            ComputeDescriptorSet wrapped(descriptorSet);
            wrapped.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, sceneColor.view, sceneColorSampler),
                    ComputeDescriptorWrite::CombinedImageSampler(1, mask.view, mask.sampler),
                });

            PushConstants pushConstants{};
            pushConstants.strength = strength;

            auto cmd = ctx.Cmd();
            VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f };
            VkRect2D scissor{ { 0, 0 }, extent };
            vkCmdSetViewport(cmd.Native(), 0, 1, &viewport);
            vkCmdSetScissor(cmd.Native(), 0, 1, &scissor);
            vkCmdBindPipeline(cmd.Native(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
            vkCmdBindDescriptorSets(
                cmd.Native(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdPushConstants(
                cmd.Native(), m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
            vkCmdDraw(cmd.Native(), 3, 1, 0, 0);
        },
        rg::RenderPassDrawKind::DrawQuad, rg::RenderPassEvent::AfterEverything, kShadowPassTag.bit);
}

} // namespace gte
