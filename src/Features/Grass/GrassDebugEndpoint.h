#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class GrassFeature;

// Text-driven read and tune access to the Grass feature. Uses only the public API of GrassFeature.
class GrassDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit GrassDebugEndpoint(GrassFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    GrassFeature& m_feature;
};

} // namespace gte
