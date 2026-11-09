#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class VolumetricCloudFeature;

// Text-driven read and tune access to the VolumetricCloud feature. Uses only its public API.
class VolumetricCloudDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit VolumetricCloudDebugEndpoint(VolumetricCloudFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    VolumetricCloudFeature& m_feature;
};

} // namespace gte
