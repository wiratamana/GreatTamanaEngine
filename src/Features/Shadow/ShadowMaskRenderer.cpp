#include "ShadowMaskRenderer.h"

#include "../../Renderer/Renderer.h"

#include <cassert>
#include <cstring>

namespace gte {

namespace {
// Must match Shaders/ShadowMask.comp's std430 block exactly (mat4 + mat4 + vec4 = 272 bytes).
struct ShadowMaskParamsGpu {
    float invViewProjection[16];
    float lightViewProjection[16];
    float biasAndStrengthAndTexel[4]; // x=depthBias, y=unused, z=1/mapResolution, w=pad.
};
} // namespace

void ShadowMaskRenderer::EnsurePipelineBuilt(Renderer& renderer)
{
    if (m_pipeline.has_value()) {
        return;
    }
    m_device = renderer.GetVulkanContextInfo().device;

    // Binding 0 = params (storage buffer), 1 = shadowDepth, 2 = sceneDepth,
    // 3 = maskOutput (r8 storage image) - reflected from ShadowMask.comp.
    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/ShadowMask.comp.spv"));
}

ShadowMaskRenderer::ViewState& ShadowMaskRenderer::EnsureViewState(
    Renderer& renderer, const char* outputName, VkExtent2D extent)
{
    const auto existing = m_viewStates.find(outputName);
    if (existing != m_viewStates.end()) {
        return existing->second;
    }

    ViewState state;
    state.descriptorSet =
        ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_pipeline->ReflectedDescriptorSetLayout(0)));
    state.paramsBuffer.emplace(renderer.CreateBuffer(sizeof(ShadowMaskParamsGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        BufferMemoryUsage::CpuToGpu, "ShadowMaskParams"));

    const int width = extent.width > 0 ? static_cast<int>(extent.width) : 1;
    const int height = extent.height > 0 ? static_cast<int>(extent.height) : 1;
    // r8, single channel, 0 = lit, 1 = shadowed.
    state.output.emplace(renderer.CreateRenderTexture(width, height, VK_FORMAT_R8_UNORM, outputName,
        /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/false,
        /*createDepthCompanion=*/false, /*createColorImage=*/true));

    const auto insertedPair = m_viewStates.emplace(outputName, std::move(state));
    return insertedPair.first->second;
}

rg::TextureHandle ShadowMaskRenderer::AddMaskPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const char* outputName, VkExtent2D viewExtent, const Mat4& invViewProjection, const Mat4& lightViewProjection,
    float depthBias, std::uint32_t shadowMapResolution, rg::TextureHandle shadowMapHandle,
    VkSampler shadowMapDepthSampler, rg::TextureHandle sceneDepthHandle, VkImageView sceneDepthImageView,
    VkSampler sceneDepthSampler)
{
    if (!shadowMapHandle.IsValid()) {
        return rg::TextureHandle{};
    }

    EnsurePipelineBuilt(renderer);
    ViewState& viewState = EnsureViewState(renderer, outputName, viewExtent);

    ShadowMaskParamsGpu params{};
    std::memcpy(params.invViewProjection, invViewProjection.Data(), sizeof(params.invViewProjection));
    std::memcpy(params.lightViewProjection, lightViewProjection.Data(), sizeof(params.lightViewProjection));
    params.biasAndStrengthAndTexel[0] = depthBias;
    params.biasAndStrengthAndTexel[1] = 0.0f;
    params.biasAndStrengthAndTexel[2] = shadowMapResolution > 0 ? 1.0f / static_cast<float>(shadowMapResolution) : 0.0f;
    params.biasAndStrengthAndTexel[3] = 0.0f;
    viewState.paramsBuffer->Upload(&params, sizeof(params));

    const rg::TextureHandle outputHandle = builder.ImportTexture(
        outputName, viewState.output->Target(), VK_IMAGE_LAYOUT_UNDEFINED, viewState.output->Sampler());

    builder.AddRenderPass(
        "Shadow.Mask.Dispatch", rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [shadowMapHandle, sceneDepthHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(shadowMapHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.ReadTexture(sceneDepthHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &viewState, shadowMapHandle, shadowMapDepthSampler, sceneDepthImageView, sceneDepthSampler,
            outputHandle, viewExtent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedDepthTexture shadowDepth = ctx.resolveDepthTexture(shadowMapHandle);
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            // Non-null here confirms ImportTexture() above supplied a real
            // depth sampler - assert directly, never silently fall back.
            assert(shadowDepth.view != VK_NULL_HANDLE && "shadow map resolved with no depth view.");

            viewState.descriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, viewState.paramsBuffer->Native()),
                    ComputeDescriptorWrite::CombinedImageSampler(1, shadowDepth.view, shadowMapDepthSampler),
                    ComputeDescriptorWrite::CombinedImageSampler(2, sceneDepthImageView, sceneDepthSampler),
                    ComputeDescriptorWrite::StorageImage(3, dest.view),
                });

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*m_pipeline);
            cmd.BindDescriptorSet(viewState.descriptorSet.Native());
            cmd.DispatchOverSize(viewExtent.width, viewExtent.height, 1);
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);

    return outputHandle;
}

} // namespace gte
