#pragma once

#include "VolumetricLightDebugEndpoint.h"
#include "VolumetricLightTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Game/Lighting/SunLightModulation.h"
#include "../../Game/Lighting/SunShadowReceiver.h"

#include <array>
#include <cstdint>

namespace gte {

inline constexpr fx::VolumeSize kVolumetricLightVolumeSize{
    kVolumetricLightGridWidth, kVolumetricLightGridHeight, kVolumetricLightGridDepth
};

// Per-view resources and last-frame memory. Parameter blocks are per view on purpose.
struct VolumetricLightViewState {
    fx::PersistentVolume raw{ "VolumetricLight.Raw", kVolumetricLightVolumeSize, PixelFormat::RGBA16_Float };
    fx::PersistentVolume blended{ "VolumetricLight.Blended", kVolumetricLightVolumeSize, PixelFormat::RGBA16_Float };
    fx::PersistentVolume integrated{ "VolumetricLight.Integrated", kVolumetricLightVolumeSize, PixelFormat::RGBA16_Float };
    std::array<fx::PersistentTarget, 2> history{
        fx::PersistentTarget{ "VolumetricLight.History0" },
        fx::PersistentTarget{ "VolumetricLight.History1" },
    };
    fx::GpuParams<VolumetricLightInjectParamsGpu> injectParams;
    fx::GpuParams<VolumetricLightTemporalParamsGpu> temporalParams;
    VolumetricLightHistory previous;
    VolumetricLightViewSnapshot snapshot;
    std::array<double, 3> windOffset{};
};

// Light shafts: a froxel medium lit by the sun's cascade shadows. PostOpaque compute passes build and
// integrate the volume; a PostComposite compute pass applies it to the picture.
class VolumetricLightFeature final : public fx::Feature {
public:
    explicit VolumetricLightFeature(fx::FeatureHost& host);

    const char* Name() const override { return kVolumetricLightFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const VolumetricLightSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const VolumetricLightSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    VolumetricLightViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    // A volume stage's output. `failure` is Ok when `texture` is valid.
    struct StageResult {
        fx::Texture texture;
        VolumetricLightFrameStatus failure = VolumetricLightFrameStatus::Ok;
    };

    struct TemporalResult {
        fx::Texture blended; // Invalid when the pass did not run.
        bool historyUsed = false;
    };

    void VolumePass(fx::PostOpaqueFrame& f);
    void CompositePass(fx::CompositeFrame& f);

    StageResult RunInject(fx::PostOpaqueFrame& f, VolumetricLightViewState& view, const VolumetricLightFrameFacts& facts,
        const SunShadowReceiver* receiver);
    TemporalResult RunTemporal(fx::PostOpaqueFrame& f, VolumetricLightViewState& view,
        const VolumetricLightFrameFacts& facts, fx::Texture raw, std::uint64_t lightingKey);
    StageResult RunIntegrate(fx::PostOpaqueFrame& f, VolumetricLightViewState& view, float maxDistance, fx::Texture lit);

    VolumetricLightSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    VolumetricLightDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<VolumetricLightViewState> m_views;
    fx::ComputeShader m_injectShader{ "VolumetricLightInject" };
    fx::ComputeShader m_temporalShader{ "VolumetricLightTemporal" };
    fx::ComputeShader m_integrateShader{ "VolumetricLightIntegrate" };
    fx::ComputeShader m_compositeShader{ "VolumetricLightComposite" };
    fx::ViewOutput m_composited{ "VolumetricLight" };
    fx::ChannelReader<SunShadowReceiver> m_receiver{ kSunShadowReceiverOwner, kSunShadowReceiverChannel };
    fx::ChannelReader<SunLightModulation> m_sunLight{ kSunLightModulationOwner, kSunLightModulationChannel };
    fx::Channel<VolumetricLightResult> m_result{ "Result" };
};

} // namespace gte
