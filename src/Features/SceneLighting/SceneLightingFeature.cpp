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
    // Runs after the cascade pass (priority 10), so the receiver is already published.
    s.PreOpaque("SceneLighting.Uniforms", kUniformsPriority,
        [this](fx::PreOpaqueFrame& f) {
            const std::optional<SunShadowReceiver> receiver = m_receiver.Fetch(f);
            f.SetSceneUniforms(PackSceneLightingUniformData(
                ResolveActiveDirectionalLight(f.Scene()), m_settings, receiver ? &*receiver : nullptr));
        },
        fx::Gate::Always());
}

GTE_REGISTER_RENDER_FEATURE("SceneLighting", SceneLightingFeature);

} // namespace gte
