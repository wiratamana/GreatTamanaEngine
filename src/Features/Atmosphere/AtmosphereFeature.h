#pragma once

#include "AtmosphereAerialPerspectiveMath.h"
#include "AtmosphereDebugEndpoint.h"
#include "AtmosphereTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Game/Lighting/SunLightModulation.h"
#include "../../Game/Lighting/SunShadowReceiver.h"

namespace gte {

inline constexpr const char* kAtmosphereFeatureName = "Atmosphere";

// View-independent LUTs of one frame, made once by the shared callback and read by every view.
struct AtmosphereSharedLuts {
    fx::Texture transmittance;
    fx::Texture multiScattering;
    AtmosphereParametersGpu parameters;
};

// One view's sky-view LUT and frame uniforms. The texture is invalid when its pass did not run this frame.
struct AtmosphereViewLuts {
    fx::Texture skyView;
    AtmosphereFrameUniforms frameUniforms;
};

// One view's aerial perspective volume and shaft delta image, with the values the composite must use.
struct AtmosphereAerialResult {
    fx::Texture volume;
    fx::Texture shaftDelta;
    float maxDistanceKm = 0.0f;
    float depthExponent = 0.0f;
};

// Size of the per-view aerial perspective volume in texels. The shaft delta image shares its X and Y.
inline constexpr fx::VolumeSize kAerialVolumeSize{ 128, 128, 32 };

// Per-view resources and state. Each pass owns its own parameter block.
struct AtmosphereViewState {
    fx::PersistentTarget skyView{ "SkyViewLut" };
    fx::PersistentVolume aerialVolume{ "AerialPerspectiveVolume", kAerialVolumeSize, PixelFormat::RGBA16_Float };
    fx::PersistentTarget shaftDelta{ "AerialShaftDelta" };
    fx::GpuParams<AtmosphereFrameUniforms> skyFrameParams;
    fx::GpuParams<AtmosphereParametersGpu> aerialAtmosphereParams;
    fx::GpuParams<AtmosphereFrameUniforms> aerialFrameParams;
    fx::GpuParams<AerialShadowParamsGpu> aerialShadowParams;
    AerialScaleState scaleState;
    AtmosphereAerialSnapshot aerialSnapshot;
    SunLightModulationSnapshot sunLight;
};

// Physically based sky: shared transmittance and multi-scattering LUTs, a per-view sky-view LUT, an aerial
// perspective volume lit by the sun cascades, a sky background drawn behind the scene and a composite.
class AtmosphereFeature final : public fx::Feature {
public:
    explicit AtmosphereFeature(fx::FeatureHost& host);

    const char* Name() const override { return kAtmosphereFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const AtmosphereSettings& Settings() const noexcept { return m_settings; }

    // Stores the candidate after sanitizing it.
    void ApplySettings(const AtmosphereSettings& candidate);

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

    // The record published to `scope`'s view in its last rendered frame.
    SunLightModulationSnapshot SunLightSnapshot(rg::ViewScope scope) const { return m_views.For(scope).sunLight; }

    // The aerial perspective values `scope`'s view used in its last frame.
    AtmosphereAerialSnapshot AerialSnapshot(rg::ViewScope scope) const { return m_views.For(scope).aerialSnapshot; }

private:
    void SharedLuts(fx::SharedFrame& f);
    void ViewLuts(fx::PreOpaqueFrame& f);
    void PublishSunLight(fx::PreOpaqueFrame& f);
    void AerialVolume(fx::PostOpaqueFrame& f);
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
    fx::Channel<AtmosphereAerialResult> m_aerial{ "AerialVolume" };
    fx::Channel<SunLightModulation> m_sunLight{ kSunLightModulationChannel };
    fx::ChannelReader<SunShadowReceiver> m_receiver{ kSunShadowReceiverOwner, kSunShadowReceiverChannel };
    AtmosphereDebugEndpoint m_debugEndpoint{ *this };
};

} // namespace gte
