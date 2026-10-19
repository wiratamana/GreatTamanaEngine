#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class VolumetricLightFeature;

// Text-driven read and tune access to the VolumetricLight feature. Uses only its public API.
class VolumetricLightDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit VolumetricLightDebugEndpoint(VolumetricLightFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    VolumetricLightFeature& m_feature;
};

} // namespace gte
