#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class TemporalAaFeature;

// Text-driven read and tune access to the TemporalAa feature. Uses only its public API.
class TemporalAaDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit TemporalAaDebugEndpoint(TemporalAaFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    TemporalAaFeature& m_feature;
};

} // namespace gte
