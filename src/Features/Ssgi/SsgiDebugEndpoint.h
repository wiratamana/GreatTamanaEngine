#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class SsgiFeature;

// Text-driven read and tune access to the Ssgi feature. Uses only the public API of SsgiFeature.
class SsgiDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit SsgiDebugEndpoint(SsgiFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    SsgiFeature& m_feature;
};

} // namespace gte
