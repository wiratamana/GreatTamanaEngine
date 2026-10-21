#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class AtmosphereFeature;

// Text-driven access to the sun light switch and the record published per view. Uses only the feature's public API.
class AtmosphereDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit AtmosphereDebugEndpoint(AtmosphereFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    AtmosphereFeature& m_feature;
};

} // namespace gte
