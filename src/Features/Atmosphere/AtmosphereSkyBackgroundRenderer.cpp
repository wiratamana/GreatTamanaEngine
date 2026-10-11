#include "AtmosphereSkyBackgroundRenderer.h"

#include "../../Core/PipelineDesc.h"
#include "../../Renderer/ComputeDescriptorSet.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

std::vector<std::byte> ReadShaderFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("AtmosphereSkyBackgroundRenderer: failed to open shader file '" + path +
            "' - was it compiled? See cmake/CompileShaders.cmake.");
    }
    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<std::byte> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    return buffer;
}

// Fragment-stage push constants - MUST match
// Shaders/AtmosphereSkyBackground.frag's own `layout(push_constant)` block
// exactly (mat4 + 3 floats + 1 padding float = 80 bytes).
struct PushConstants {
    float invViewProjection[16];
    float eyeHeightKm;
    float planetRadiusKm;
    float skyExposure;
    float _pad0;
};

} // namespace

AtmosphereSkyBackgroundRenderer::~AtmosphereSkyBackgroundRenderer()
{
    Reset();
}

void AtmosphereSkyBackgroundRenderer::Reset()
{
    if (m_device != VK_NULL_HANDLE && m_pipeline) {
        vkDeviceWaitIdle(m_device);
    }
    if (m_renderer) {
        m_renderer->DestroyDescriptorSetLayout(m_setLayoutHandle);
    }
    m_setLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    m_pipeline.reset();

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    // m_descriptorSet is allocated from Renderer's own shared compute
    // descriptor pool - never individually freed.
    m_descriptorSet = VK_NULL_HANDLE;
}

void AtmosphereSkyBackgroundRenderer::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline) {
        return;
    }
    m_device = renderer.GetVulkanContextInfo().device;
    m_renderer = &renderer;

    // Binding 0 = the Sky-View LUT combined image sampler - see
    // Shaders/AtmosphereSkyBackground.frag exactly. Fragment stage only.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_descriptorSetLayout =
        layoutBuilder.AddCombinedImageSampler(/*binding=*/0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();
    m_descriptorSet = renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout);

    // Opaque-handle twin, identically defined, fed to the pipeline builder -
    // pipeline-layout-compatible with the real set allocated above.
    constexpr DescriptorBindingDesc kBindings[] = {
        { .binding = 0, .kind = DescriptorKind::CombinedImageSampler, .stages = ShaderStageMask::Fragment },
    };
    m_setLayoutHandle = renderer.CreateDescriptorSetLayout(kBindings);

    // CONFIRMED depth convention: depth TEST enabled with CompareOp::Equal
    // (not Less - this pass must survive ONLY at a pixel whose depth is
    // still exactly the frame's own clear value, 1.0), depth WRITE disabled
    // (nothing is ever meant to occlude the sky itself). Opaque blend - the
    // sky IS the base layer for every pixel it survives the depth test at.
    PipelineDesc desc = PipelinePreset::DepthEqualNoWrite(renderer.ColorPixelFormat(), renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::None; // Full-screen triangle from gl_VertexIndex - no vertex input.
    desc.cullMode = CullMode::None;
    desc.layout.extraSetLayouts = std::span{ &m_setLayoutHandle, 1 };
    desc.layout.pushConstantByteSize = sizeof(PushConstants);
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/AtmosphereSkyBackground.vert.spv");
    const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/AtmosphereSkyBackground.frag.spv");
    m_pipeline = std::make_unique<Pipeline>(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
}

void AtmosphereSkyBackgroundRenderer::Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& viewProjection,
    VkImageView skyViewLutView, VkSampler skyViewLutSampler, float eyeHeightKm, float planetRadiusKm,
    float skyExposure)
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
    pushConstants.skyExposure = skyExposure;

    const VkPipeline pipeline = m_pipeline->Native();
    const VkPipelineLayout layout = m_pipeline->Layout();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace gte
