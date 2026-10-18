#pragma once

#include "SceneLightingTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

namespace gte {

// Writes the sun block (scene-services binding 8) every frame for every view.
class SceneLightingFeature final : public fx::Feature {
public:
    explicit SceneLightingFeature(fx::FeatureHost& host);

    const char* Name() const override { return kSceneLightingFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;
};

} // namespace gte
