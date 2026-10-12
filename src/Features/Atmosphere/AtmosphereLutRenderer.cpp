#include "AtmosphereLutRenderer.h"
#include "../../Renderer/Internal/AtmosphereLutRendererImpl.h"

#include "AtmosphereParameters.h"
#include "../../Game/Lighting/DirectionalLightResolver.h"

namespace gte {

AtmosphereLutRenderer::AtmosphereLutRenderer() : m_impl(std::make_unique<Impl>()) {}
AtmosphereLutRenderer::~AtmosphereLutRenderer() = default;

rg::TextureHandle AtmosphereLutRenderer::AddTransmittanceLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, rg::RenderPassToggleRegistry* toggleRegistry)
{
    return m_impl->AddTransmittanceLutPass(builder, renderer, params, toggleRegistry);
}

rg::TextureHandle AtmosphereLutRenderer::AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    return m_impl->AddMultiScatteringLutPass(builder, renderer, params, transmittanceLutHandle, toggleRegistry);
}

rg::TextureHandle AtmosphereLutRenderer::AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
    const char* outputTextureName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry)
{
    return m_impl->AddSkyViewLutPass(builder, renderer, params, frameUniforms, transmittanceLutHandle,
        multiScatteringLutHandle, outputTextureName, viewScope, toggleRegistry);
}

rg::VolumeTextureHandle AtmosphereLutRenderer::AddAerialPerspectiveVolumePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
    const char* outputVolumeName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry)
{
    return m_impl->AddAerialPerspectiveVolumePass(builder, renderer, params, frameUniforms, transmittanceLutHandle,
        multiScatteringLutHandle, outputVolumeName, viewScope, toggleRegistry);
}

void AtmosphereLutRenderer::DrawSkyBackground(Renderer& renderer, rg::CommandBuffer& cmd,
    const rg::RenderGraph& renderGraph, const Mat4& viewProjection, const AtmosphereParametersGpu& params,
    const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure)
{
    m_impl->DrawSkyBackground(renderer, cmd, renderGraph, viewProjection, params, frameUniforms, skyViewLutName,
        skyExposure);
}

rg::TextureHandle AtmosphereLutRenderer::AddAerialPerspectiveCompositePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, rg::TextureHandle sourceColorHandle, SamplerHandle sourceColorSampler,
    rg::TextureHandle sourceDepthHandle, ImageViewHandle sourceDepthView, SamplerHandle sourceDepthSampler,
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    const Mat4& invViewProjection, Vec3 cameraWorldPosition, float aerialPerspectiveStrength, float maxDistanceKm,
    float depthExponent, Extent2D extent, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    return m_impl->AddAerialPerspectiveCompositePass(builder, renderer, sourceColorHandle, sourceColorSampler,
        sourceDepthHandle, sourceDepthView, sourceDepthSampler, aerialPerspectiveVolumeHandle,
        aerialPerspectiveVolumeName, invViewProjection, cameraWorldPosition, aerialPerspectiveStrength, maxDistanceKm,
        depthExponent, extent, outputTextureName, viewScope, toggleRegistry);
}

RenderTexture* AtmosphereLutRenderer::CompositedOutput(const char* outputTextureName) noexcept
{
    return m_impl->CompositedOutput(outputTextureName);
}

void AtmosphereLutRenderer::FinalizeAerialPerspectiveCompositeForSampling(
    rg::CommandBuffer& cmd, const char* outputTextureName)
{
    m_impl->FinalizeAerialPerspectiveCompositeForSampling(cmd, outputTextureName);
}

int AtmosphereLutRenderer::AerialPerspectiveVolumeDepth(const char* aerialPerspectiveVolumeName) const noexcept
{
    return m_impl->AerialPerspectiveVolumeDepth(aerialPerspectiveVolumeName);
}

rg::TextureHandle AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    std::uint32_t debugSliceIndex, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    return m_impl->AddAerialPerspectiveVolumeDebugSlicePass(builder, renderer, aerialPerspectiveVolumeHandle,
        aerialPerspectiveVolumeName, debugSliceIndex, outputTextureName, viewScope, toggleRegistry);
}

Renderer::CapturedRawPixels AtmosphereLutRenderer::CaptureAerialPerspectiveVolumeSliceImmediate(Renderer& renderer,
    const char* aerialPerspectiveVolumeName, std::uint32_t sliceIndex, const char* outputTextureName)
{
    return m_impl->CaptureAerialPerspectiveVolumeSliceImmediate(
        renderer, aerialPerspectiveVolumeName, sliceIndex, outputTextureName);
}

AtmosphereFrameUniforms ResolveAtmosphereFrameUniforms(Registry& registry, Vec3 eyeWorldPosition)
{
    AtmosphereFrameUniforms uniforms;

    const AtmosphereParametersGpu earthParameters = MakeDefaultEarthAtmosphereParameters();
    const Vec3 eyeAtmosphereKm = WorldPositionToAtmosphereSpaceKm(eyeWorldPosition);
    const float heightAboveGroundKm = eyeAtmosphereKm.y > 0.0f ? eyeAtmosphereKm.y : 0.0f;

    // Planet-centered frame (Length(positionKm) == planetRadiusKm +
    // heightAboveGroundKm) - the camera's horizontal world position is
    // deliberately not folded in here; every LUT pass in this engine only
    // ever cares about height above the ground, never full 3D world position.
    uniforms.cameraPositionAtmosphere = Vec3(0.0f, earthParameters.planetRadiusKm + heightAboveGroundKm, 0.0f);

    // Sun direction/color/illuminance come from a real ECS DirectionalLight
    // entity, falling back to a hardcoded placeholder whenever the Registry
    // has no active DirectionalLight at all.
    const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(registry);
    uniforms.sunDirection = sun.directionTowardSun;
    uniforms.sunIlluminance = sun.sunIlluminance;

    return uniforms;
}

} // namespace gte
