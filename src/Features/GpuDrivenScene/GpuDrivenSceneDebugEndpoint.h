#pragma once

#include "../../Core/FeatureKit/FeatureDebug.h"

namespace gte {

class GpuDrivenSceneFeature;
struct GpuDrivenSceneViewSnapshot;

// Appends the state fields of one view; every key starts with `prefix` ("game." or "scene.").
void AppendGpuDrivenSceneViewFields(
    fx::FeatureDebugResult& out, std::string_view prefix, const GpuDrivenSceneViewSnapshot& view);

// Text-driven read and tune access to the GpuDrivenScene feature. Uses only its public API.
class GpuDrivenSceneDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit GpuDrivenSceneDebugEndpoint(GpuDrivenSceneFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    GpuDrivenSceneFeature& m_feature;
};

} // namespace gte
