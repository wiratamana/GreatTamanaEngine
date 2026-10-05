#include "AtmospherePassSequence.h"

#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"

namespace gte {

AtmosphereSharedLutHandles AddAtmosphereSharedLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, const AtmosphereParametersGpu& atmosphereParameters,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    AtmosphereSharedLutHandles handles;
    handles.transmittanceLutHandle =
        atmosphereLutRenderer.AddTransmittanceLutPass(builder, renderer, atmosphereParameters, toggleRegistry);
    if (!handles.transmittanceLutHandle.IsValid()) {
        return handles; // multiScatteringLutHandle stays default-constructed (invalid) too - nothing to feed it.
    }
    handles.multiScatteringLutHandle = atmosphereLutRenderer.AddMultiScatteringLutPass(
        builder, renderer, atmosphereParameters, handles.transmittanceLutHandle, toggleRegistry);
    return handles;
}

AtmosphereViewLutHandles AddAtmosphereViewLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, Registry& registry,
    const AtmosphereParametersGpu& atmosphereParameters, const AtmosphereSettings& atmosphereSettings,
    const AtmosphereSharedLutHandles& sharedLuts, Vec3 eyeWorldPosition, const Mat4& viewProjection,
    const char* skyViewLutName, const char* aerialPerspectiveVolumeName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    AtmosphereViewLutHandles result;
    result.frameUniforms = ResolveAtmosphereFrameUniforms(registry, eyeWorldPosition);
    result.frameUniforms.invViewProjection = viewProjection.Inverse();
    result.frameUniforms.aerialPerspectiveMaxDistanceKm = atmosphereSettings.aerialPerspectiveMaxDistanceKm;
    result.frameUniforms.aerialPerspectiveDepthExponent = atmosphereSettings.aerialPerspectiveDepthExponent;
    result.frameUniforms.aerialPerspectiveSamplesPerSliceAsFloat =
        static_cast<float>(atmosphereSettings.aerialPerspectiveSamplesPerSlice);
    result.frameUniforms.aerialPerspectiveScatteringExaggeration =
        atmosphereSettings.aerialPerspectiveScatteringExaggeration;

    if (!sharedLuts.transmittanceLutHandle.IsValid() || !sharedLuts.multiScatteringLutHandle.IsValid()) {
        return result; // skyViewLutHandle/aerialPerspectiveVolumeHandle stay invalid - nothing valid to feed them.
    }

    result.skyViewLutHandle = atmosphereLutRenderer.AddSkyViewLutPass(builder, renderer, atmosphereParameters,
        result.frameUniforms, sharedLuts.transmittanceLutHandle, sharedLuts.multiScatteringLutHandle, skyViewLutName,
        viewScope, toggleRegistry);

    result.aerialPerspectiveVolumeHandle = atmosphereLutRenderer.AddAerialPerspectiveVolumePass(builder, renderer,
        atmosphereParameters, result.frameUniforms, sharedLuts.transmittanceLutHandle,
        sharedLuts.multiScatteringLutHandle, aerialPerspectiveVolumeName, viewScope, toggleRegistry);

    return result;
}

std::function<void(VkCommandBuffer)> MakeRecordSkyBackgroundCallback(AtmosphereLutRenderer& atmosphereLutRenderer,
    Renderer& renderer, const Mat4& viewProjection, const AtmosphereParametersGpu& atmosphereParameters,
    const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure)
{
    return [&atmosphereLutRenderer, &renderer, viewProjection, atmosphereParameters, frameUniforms, skyViewLutName,
               skyExposure](VkCommandBuffer cmd) {
        atmosphereLutRenderer.DrawSkyBackground(renderer, cmd, viewProjection, atmosphereParameters, frameUniforms,
            skyViewLutName, skyExposure);
    };
}

rg::TextureHandle AddAtmosphereCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, rg::TextureHandle sourceColorHandle, VkSampler sourceColorSampler,
    rg::TextureHandle sourceDepthHandle, VkImageView sourceDepthView, VkSampler sourceDepthSampler,
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    const AtmosphereFrameUniforms& frameUniforms, Vec3 eyeWorldPosition, float aerialPerspectiveStrength,
    float maxDistanceKm, float depthExponent, VkExtent2D extent, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    return atmosphereLutRenderer.AddAerialPerspectiveCompositePass(builder, renderer, sourceColorHandle,
        sourceColorSampler, sourceDepthHandle, sourceDepthView, sourceDepthSampler, aerialPerspectiveVolumeHandle,
        aerialPerspectiveVolumeName, frameUniforms.invViewProjection, eyeWorldPosition, aerialPerspectiveStrength,
        maxDistanceKm, depthExponent, extent, outputTextureName, viewScope, toggleRegistry);
}

} // namespace gte
