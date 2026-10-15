#include "ShadowMaskRenderer.h"

#include "../../Core/PixelFormat.h"
#include "../../Renderer/RenderGraph/ComputePass.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"

namespace gte {

ShadowMaskParamsGpu MakeShadowMaskParams(const ShadowMaskInputs& inputs) noexcept
{
    ShadowMaskParamsGpu params;
    params.invViewProjection = inputs.invViewProjection;
    params.lightViewProjection = inputs.lightViewProjection;
    params.depthBias = inputs.depthBias;
    params.shadowTexelSize =
        inputs.shadowMapResolution > 0 ? 1.0f / static_cast<float>(inputs.shadowMapResolution) : 0.0f;
    return params;
}

rg::TextureHandle ShadowMaskRenderer::AddMaskPass(rg::RenderGraphBuilder& builder, const ShadowMaskInputs& inputs,
    rg::TextureHandle shadowMap, rg::TextureHandle sceneDepth)
{
    // Bind-based passes declared once per view must not use the Shared scope.
    if (!shadowMap.IsValid() || inputs.viewScope == rg::ViewScope::Shared) {
        return rg::TextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Shadow");

    ViewState& view = m_views.For(inputs.viewScope);
    const rg::TextureHandle mask =
        view.mask.Acquire(builder, inputs.outputName, inputs.viewExtent, PixelFormat::R8_UNorm);
    if (!mask.IsValid()) {
        return rg::TextureHandle{};
    }

    const bool declared =
        rg::ComputePass(builder, m_shader, "Shadow.Mask.Dispatch", inputs.viewScope, rg::RenderPassEvent::AfterOpaques)
            .Params(view.params, MakeShadowMaskParams(inputs))
            .Bind("shadowDepth", rg::Depth(shadowMap))
            .Bind("sceneDepth", rg::Depth(sceneDepth))
            .Bind("maskOutput", mask)
            .Dispatch(inputs.viewExtent);
    // An undeclared pass leaves the mask unwritten: report it as missing.
    return declared ? mask : rg::TextureHandle{};
}

} // namespace gte
