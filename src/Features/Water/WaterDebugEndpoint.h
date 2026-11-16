#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class WaterFeature;

// Text-driven read and tune access to the Water feature. Uses only the public API of WaterFeature.
class WaterDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit WaterDebugEndpoint(WaterFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    WaterFeature& m_feature;
};

} // namespace gte
