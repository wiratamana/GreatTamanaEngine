#pragma once

#include "SceneLightingTypes.h"
#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

// Text-driven read and tune access to the SceneLighting settings. Writes are sanitized.
class SceneLightingDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit SceneLightingDebugEndpoint(SceneLightingSettings& settings) noexcept : m_settings(settings) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    SceneLightingSettings& m_settings;
};

} // namespace gte
