#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class GtaoFeature;

// Text-driven read and tune access to the Gtao feature. Uses only the public API of GtaoFeature.
class GtaoDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit GtaoDebugEndpoint(GtaoFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    GtaoFeature& m_feature;
};

} // namespace gte
