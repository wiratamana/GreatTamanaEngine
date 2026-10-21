#pragma once

#include "SceneLightingDebugEndpoint.h"
#include "SceneLightingTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Game/Lighting/SunLightModulation.h"
#include "../../Game/Lighting/SunShadowReceiver.h"

namespace gte {

// Writes the sun, ambient and sun-cascade block (scene-services binding 8) every frame for every view.
class SceneLightingFeature final : public fx::Feature {
public:
    explicit SceneLightingFeature(fx::FeatureHost& host);

    const char* Name() const override { return kSceneLightingFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;
    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_endpoint; }

    const SceneLightingSettings& Settings() const noexcept { return m_settings; }
    // Sanitizes and stores the settings. Main thread only.
    void ApplySettings(const SceneLightingSettings& candidate);

private:
    SceneLightingSettings m_settings;
    fx::PerView<SunLightModulationSnapshot> m_applied;
    SceneLightingDebugEndpoint m_endpoint{ m_settings, &m_applied };
    fx::ChannelReader<SunShadowReceiver> m_receiver{ kSunShadowReceiverOwner, kSunShadowReceiverChannel };
    fx::ChannelReader<SunLightModulation> m_sunLight{ kSunLightModulationOwner, kSunLightModulationChannel };
};

} // namespace gte
