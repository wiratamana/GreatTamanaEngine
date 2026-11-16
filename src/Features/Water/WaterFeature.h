#pragma once

#include "WaterDebugEndpoint.h"
#include "WaterTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Game/Lighting/AtmosphereSkyLuts.h"
#include "../../Game/Lighting/SunLightModulation.h"

#include <cstdint>

namespace gte {

// Per-view targets and the parameter block. Parameter blocks are per view on purpose.
struct WaterViewState {
    fx::PersistentTarget color{ "Water.Color" }; // rgb radiance, a coverage.
    fx::PersistentTarget depth{ "Water.Depth" }; // Window depth of the surface.
    fx::GpuParams<WaterShadeParamsGpu> shadeParams;
    WaterViewSnapshot snapshot;
};

// Screen-space water: a compute pass traces the view rays against the wave height field and shades them,
// a fullscreen draw writes the result into the scene color and depth.
class WaterFeature final : public fx::Feature {
public:
    explicit WaterFeature(fx::FeatureHost& host);

    const char* Name() const override { return kWaterFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const WaterSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const WaterSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    WaterViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void SurfacePass(fx::PostOpaqueFrame& f);
    void AdvanceClock(const fx::Frame& f);

    WaterSettings m_settings;
    double m_time = 0.0;
    std::uint64_t m_clockFrame = ~std::uint64_t{ 0 };
    std::uint64_t m_newestFrame = 0;
    WaterDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<WaterViewState> m_views;
    fx::ComputeShader m_shadeShader{ "WaterShade" };
    fx::FullscreenShader m_compositeShader{ "WaterComposite" };
    fx::ChannelReader<AtmosphereViewLuts> m_viewLuts{ kAtmosphereLutOwner, kAtmosphereViewLutsChannel };
    fx::ChannelReader<AtmosphereSharedLuts> m_sharedLuts{ kAtmosphereLutOwner, kAtmosphereSharedLutsChannel };
    fx::ChannelReader<SunLightModulation> m_sunLight{ kSunLightModulationOwner, kSunLightModulationChannel };
};

} // namespace gte
