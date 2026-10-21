#pragma once

#include "SceneLightingTypes.h"
#include "../../Core/FeatureKit/FeatureDebug.h"
#include "../../Core/FeatureKit/PerView.h"
#include "../../Game/Lighting/SunLightModulation.h"

namespace gte {

// Text-driven read and tune access to the SceneLighting settings. Writes are sanitized.
// With `applied`, the state also reports the sun light record each view applied (read-only).
class SceneLightingDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit SceneLightingDebugEndpoint(
        SceneLightingSettings& settings, const fx::PerView<SunLightModulationSnapshot>* applied = nullptr) noexcept
        : m_settings(settings), m_applied(applied)
    {
    }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    SceneLightingSettings& m_settings;
    const fx::PerView<SunLightModulationSnapshot>* m_applied;
};

} // namespace gte
