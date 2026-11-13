#pragma once

#include "VolumetricCloudDebugEndpoint.h"
#include "VolumetricCloudMath.h"
#include "VolumetricCloudTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Core/FeatureKit/ScreenSpaceView.h"
#include "../../Game/Lighting/AtmosphereSkyLuts.h"
#include "../../Game/Lighting/CloudShadowMap.h"
#include "../../Game/Lighting/SunLightModulation.h"

#include <array>
#include <cstdint>

namespace gte {

// Per-view memory. Parameter blocks are per view on purpose.
struct VolumetricCloudViewState {
    fx::GpuParams<VolumetricCloudParamsGpu> params;
    fx::GpuParams<VolumetricCloudResolveParamsGpu> resolveParams;
    fx::GpuParams<AtmosphereParametersGpu> atmosphereParams;
    VolumetricCloudViewSnapshot snapshot;
    double windOffset[2] = {};
    double windStep[2] = {};
    fx::TemporalHistory previous; // Valid only when the resolve pass wrote the history last frame.

    // Cloud shadow map: two maps ping-pong by frame index; the dispatch uses its own parameter blocks.
    fx::GpuParams<VolumetricCloudParamsGpu> shadowParams;
    fx::GpuParams<AtmosphereParametersGpu> shadowAtmosphereParams;
    std::array<fx::PersistentTarget, 2> shadowMap{
        fx::PersistentTarget{ "VolumetricCloud.ShadowMap0" }, fx::PersistentTarget{ "VolumetricCloud.ShadowMap1" } };
    fx::PersistentTarget shadowAux{ "VolumetricCloud.ShadowAux" };
    CloudShadowGrid shadowGrid;       // Grid of the last map.
    std::uint64_t shadowFrame = 0;    // Frame that wrote it.
    bool shadowGridValid = false;     // The last map is usable as history.
    CloudShadowMap shadowPublished;   // The map published in `shadowPublishedFrame`; the march reads it for the air.
    std::uint64_t shadowPublishedFrame = ~std::uint64_t{ 0 };

    // March target at 1/divisor of the view size: cloud light + transmittance, and distance + completeness.
    fx::PersistentTarget lowColor{ "VolumetricCloud.LowColor" };
    fx::PersistentTarget lowAux{ "VolumetricCloud.LowAux" };
    // Full size history of the resolved cloud, ping-pong by frame index.
    std::array<fx::PersistentTarget, 2> historyColor{
        fx::PersistentTarget{ "VolumetricCloud.HistoryColor0" }, fx::PersistentTarget{ "VolumetricCloud.HistoryColor1" } };
    std::array<fx::PersistentTarget, 2> historyAux{
        fx::PersistentTarget{ "VolumetricCloud.HistoryAux0" }, fx::PersistentTarget{ "VolumetricCloud.HistoryAux1" } };
};

// Volumetric clouds: one ray marched layer between two spheres, composited over the scene with the scene
// depth as the far limit of every ray. The slice debug views show the baked noise instead.
class VolumetricCloudFeature final : public fx::Feature {
public:
    explicit VolumetricCloudFeature(fx::FeatureHost& host);

    const char* Name() const override { return kVolumetricCloudFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const VolumetricCloudSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const VolumetricCloudSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    VolumetricCloudViewSnapshot Snapshot(rg::ViewScope scope) const;

    // Size of a loaded asset, zero while it is not loaded.
    fx::VolumeSize ShapeSize() const noexcept { return m_shape.Size(); }
    fx::VolumeSize DetailSize() const noexcept { return m_detail.Size(); }
    Extent2D WeatherSize() const noexcept { return m_weather.Size(); }
    Extent2D BlueNoiseSize() const noexcept { return m_blueNoise.Size(); }
    Extent2D CurlSize() const noexcept { return m_curl.Size(); }

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void CompositePass(fx::CompositeFrame& f);
    // Marches the sun ray of every texel of the shadow map through the layer and shares the map with the lit materials.
    void ShadowMapPass(fx::PreOpaqueFrame& f);
    // Draws the chosen noise texture instead of the clouds. False when nothing was declared.
    bool RunSliceView(fx::CompositeFrame& f, fx::Texture output, VolumetricCloudViewSnapshot& snapshot);
    // Marches the layer at low resolution, resolves it against the history and composites into `output`.
    // False when nothing was declared.
    bool RunMarch(fx::CompositeFrame& f, VolumetricCloudViewState& view, fx::Texture output,
        VolumetricCloudViewSnapshot& snapshot);
    // The asset behind the chosen debug view; invalid while it is missing.
    fx::Texture AcquireDebugSource(fx::CompositeFrame& f);

    VolumetricCloudSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    VolumetricCloudDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<VolumetricCloudViewState> m_views;
    fx::StaticVolume m_shape{ kCloudShapeAsset };
    fx::StaticVolume m_detail{ kCloudDetailAsset };
    fx::StaticTexture m_weather{ kCloudWeatherAsset };
    fx::StaticTexture m_blueNoise{ kCloudBlueNoiseAsset };
    fx::StaticTexture m_curl{ kCloudCurlAsset };
    fx::ComputeShader m_marchShader{ "VolumetricCloud" };
    fx::ComputeShader m_resolveShader{ "VolumetricCloudResolve" };
    fx::ComputeShader m_slice3dShader{ "VolumetricCloudSlice3D" };
    fx::ComputeShader m_slice2dShader{ "VolumetricCloudSlice2D" };
    fx::ViewOutput m_output{ "VolumetricCloud" };
    fx::SceneSlot m_shadowSlot;
    fx::Channel<CloudShadowMap> m_shadowData{ kCloudShadowChannel };
    fx::ChannelReader<SunLightModulation> m_sunLight{ kSunLightModulationOwner, kSunLightModulationChannel };
    fx::ChannelReader<AtmosphereSharedLuts> m_sharedLuts{ kAtmosphereLutOwner, kAtmosphereSharedLutsChannel };
    fx::ChannelReader<AtmosphereViewLuts> m_viewLuts{ kAtmosphereLutOwner, kAtmosphereViewLutsChannel };
};

} // namespace gte
