#include "ShadowMaskRenderer.h"

#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"

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

    // Binding 0 = params (storage buffer), 1 = shadowDepth, 2 = sceneDepth,
    // 3 = maskOutput (r8 storage image) - reflected from ShadowMask.comp.
    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/ShadowMask.comp.spv"));
}

ShadowMaskRenderer::ViewState& ShadowMaskRenderer::EnsureViewState(
    Renderer& renderer, const char* outputName, Extent2D extent)
{
    const auto existing = m_viewStates.find(outputName);
    if (existing != m_viewStates.end()) {
        return existing->second;
    }

    ViewState state;
    state.descriptorSet =
        ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_pipeline->ReflectedDescriptorSetLayout(0)));
    state.paramsBuffer.emplace(renderer.CreateBuffer(sizeof(ShadowMaskParamsGpu), BufferUsage::StorageBuffer,
        BufferMemoryUsage::CpuToGpu, "ShadowMaskParams"));

    const int width = extent.width > 0 ? static_cast<int>(extent.width) : 1;
    const int height = extent.height > 0 ? static_cast<int>(extent.height) : 1;
    // r8, single channel, 0 = lit, 1 = shadowed.
    state.output.emplace(renderer.CreateRenderTexture(width, height, PixelFormat::R8_UNorm, outputName,
        /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/false,
        /*createDepthCompanion=*/false, /*createColorImage=*/true));

    const auto insertedPair = m_viewStates.emplace(outputName, std::move(state));
    return insertedPair.first->second;
}

rg::TextureHandle ShadowMaskRenderer::AddMaskPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const ShadowMaskInputs& inputs, rg::TextureHandle shadowMap, rg::TextureHandle sceneDepth)
{
    if (!shadowMap.IsValid()) {
        return rg::TextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Shadow");

    EnsurePipelineBuilt(renderer);
    ViewState& viewState = EnsureViewState(renderer, inputs.outputName, inputs.viewExtent);

    // The view can resize after its first frame (docking, panel resize) - recreate the output to match.
    const Extent2D currentExtent = viewState.output->Extent();
    if (currentExtent.width != inputs.viewExtent.width || currentExtent.height != inputs.viewExtent.height) {
        renderer.WaitForGpuIdle();
        viewState.output->Resize(static_cast<int>(inputs.viewExtent.width), static_cast<int>(inputs.viewExtent.height));
    }

    ShadowMaskParamsGpu params{};
    std::memcpy(params.invViewProjection, inputs.invViewProjection.Data(), sizeof(params.invViewProjection));
    std::memcpy(params.lightViewProjection, inputs.lightViewProjection.Data(), sizeof(params.lightViewProjection));
    params.biasAndStrengthAndTexel[0] = inputs.depthBias;
    params.biasAndStrengthAndTexel[2] =
        inputs.shadowMapResolution > 0 ? 1.0f / static_cast<float>(inputs.shadowMapResolution) : 0.0f;
    viewState.paramsBuffer->Upload(&params, sizeof(params));

    const rg::TextureHandle output = builder.ImportTexture(inputs.outputName, viewState.output->Target(),
        ImageLayout::Undefined, viewState.output->SamplerObjectHandle());

    builder.AddRenderPass(
        "Shadow.Mask.Dispatch", rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [shadowMap, sceneDepth, output](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(shadowMap, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.ReadTexture(sceneDepth, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.WriteTexture(output, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &viewState, shadowMap, sceneDepth, output, viewExtent = inputs.viewExtent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedDepthTexture shadowDepth = ctx.resolveDepthTexture(shadowMap);
            const rg::PassContext::ResolvedDepthTexture sceneDepthView = ctx.resolveDepthTexture(sceneDepth);
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(output);
            assert(shadowDepth.view != VK_NULL_HANDLE && "shadow map resolved with no depth view.");
            assert(sceneDepthView.view != VK_NULL_HANDLE && "scene depth resolved with no depth view.");

            viewState.descriptorSet.Rewrite(
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, viewState.paramsBuffer->Native()),
                    ComputeDescriptorWrite::CombinedImageSampler(1, shadowDepth.view, shadowDepth.sampler),
                    ComputeDescriptorWrite::CombinedImageSampler(2, sceneDepthView.view, sceneDepthView.sampler),
                    ComputeDescriptorWrite::StorageImage(3, dest.view),
                });

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*m_pipeline);
            cmd.BindDescriptorSet(viewState.descriptorSet.Native());
            cmd.DispatchOverSize(viewExtent.width, viewExtent.height, 1);
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques);

    return output;
}

} // namespace gte
