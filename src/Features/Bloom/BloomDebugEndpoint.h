#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class BloomFeature;

// Text-driven read and tune access to the Bloom feature. Uses only its public API.
class BloomDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit BloomDebugEndpoint(BloomFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    BloomFeature& m_feature;
};

} // namespace gte
