#pragma once

#include "AtmosphereDebugEndpoint.h"
#include "AtmosphereTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Game/Lighting/SunLightModulation.h"

namespace gte {

inline constexpr const char* kAtmosphereFeatureName = "Atmosphere";

// View-independent LUTs of one frame, made once by the shared callback and read by every view.
struct AtmosphereSharedLuts {
    fx::Texture transmittance;
    fx::Texture multiScattering;
    AtmosphereParametersGpu parameters;
};

// One view's LUTs and frame uniforms. A texture is invalid when its pass did not run this frame.
struct AtmosphereViewLuts {
    fx::Texture skyView;
    fx::Texture aerialVolume;
    AtmosphereFrameUniforms frameUniforms;
};

// Size of the per-view aerial perspective volume in texels.
inline constexpr fx::VolumeSize kAerialVolumeSize{ 128, 128, 32 };

// Per-view resources.
struct AtmosphereViewState {
    fx::PersistentTarget skyView{ "SkyViewLut" };
    fx::PersistentVolume aerialVolume{ "AerialPerspectiveVolume", kAerialVolumeSize, PixelFormat::RGBA16_Float };
    fx::GpuParams<AtmosphereFrameUniforms> frameParams;
    SunLightModulationSnapshot sunLight;
};

// Physically based sky: shared transmittance and multi-scattering LUTs, per-view sky-view LUT and aerial
// perspective volume, a sky background drawn behind the scene and an aerial perspective composite.
class AtmosphereFeature final : public fx::Feature {
public:
    explicit AtmosphereFeature(fx::FeatureHost& host);

    const char* Name() const override { return kAtmosphereFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    AtmosphereSettings& Settings() noexcept { return m_settings; }

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

    // The record published to `scope`'s view in its last rendered frame.
    SunLightModulationSnapshot SunLightSnapshot(rg::ViewScope scope) const { return m_views.For(scope).sunLight; }

private:
    void SharedLuts(fx::SharedFrame& f);
    void ViewLuts(fx::PreOpaqueFrame& f);
    void PublishSunLight(fx::PreOpaqueFrame& f);
    void SkyBackground(fx::PostOpaqueFrame& f);
    void Composite(fx::CompositeFrame& f);

    AtmosphereSettings m_settings;

    fx::PerView<AtmosphereViewState> m_views;
    fx::PersistentTarget m_transmittance{ "TransmittanceLut" };
    fx::PersistentTarget m_multiScattering{ "MultiScatteringLut" };
    fx::ViewOutput m_composited{ "Composited" };
    fx::GpuParams<AtmosphereParametersGpu> m_sharedParams;

    fx::ComputeShader m_transmittanceShader{ "AtmosphereTransmittanceLut" };
    fx::ComputeShader m_multiScatteringShader{ "AtmosphereMultiScatteringLut" };
    fx::ComputeShader m_skyViewShader{ "AtmosphereSkyViewLut" };
    fx::ComputeShader m_aerialVolumeShader{ "AtmosphereAerialPerspectiveVolume" };
    fx::ComputeShader m_compositeShader{ "AtmosphereAerialPerspectiveComposite" };
    fx::FullscreenShader m_skyBackgroundShader{ "AtmosphereSkyBackground" };

    fx::Channel<AtmosphereSharedLuts> m_sharedLuts{ "SharedLuts" };
    fx::Channel<AtmosphereViewLuts> m_viewLuts{ "ViewLuts" };
    fx::Channel<SunLightModulation> m_sunLight{ kSunLightModulationChannel };
    AtmosphereDebugEndpoint m_debugEndpoint{ *this };
};

} // namespace gte
