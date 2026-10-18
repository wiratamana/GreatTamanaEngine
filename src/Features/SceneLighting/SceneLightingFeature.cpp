#include "SceneLightingFeature.h"

#include "../../Game/Lighting/DirectionalLightResolver.h"

namespace gte {

namespace {
constexpr std::int32_t kUniformsPriority = 20;
} // namespace

SceneLightingFeature::SceneLightingFeature(fx::FeatureHost&) { }

void SceneLightingFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a pass toggle from silencing the uniform write.
    s.PreOpaque("SceneLighting.Uniforms", kUniformsPriority,
        [](fx::PreOpaqueFrame& f) {
            f.SetSceneUniforms(PackSceneLightingUniformData(ResolveActiveDirectionalLight(f.Scene())));
        },
        fx::Gate::Always());
}

GTE_REGISTER_RENDER_FEATURE("SceneLighting", SceneLightingFeature);

} // namespace gte
