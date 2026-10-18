#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class ShadowFeature;

// Text-driven read and tune access to the Shadow feature. Uses only the public API of ShadowFeature.
class ShadowDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit ShadowDebugEndpoint(ShadowFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    ShadowFeature& m_feature;
};

} // namespace gte
