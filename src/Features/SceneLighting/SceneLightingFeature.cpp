#include "SceneLightingFeature.h"

#include "../../Game/Lighting/DirectionalLightResolver.h"

namespace gte {

namespace {
constexpr std::int32_t kUniformsPriority = 20;
static_assert(kSunLightModulationPriority < kUniformsPriority, "the sun light must be published first");
} // namespace

SceneLightingFeature::SceneLightingFeature(fx::FeatureHost&) { }

void SceneLightingFeature::ApplySettings(const SceneLightingSettings& candidate)
{
    m_settings = SanitizeSceneLightingSettings(candidate);
}

void SceneLightingFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a pass toggle from silencing the uniform write.
    // Runs after the sun light record (5) and the cascade pass (10), so both are published.
    s.PreOpaque("SceneLighting.Uniforms", kUniformsPriority,
        [this](fx::PreOpaqueFrame& f) {
            const std::optional<SunLightModulation> sunLight = m_sunLight.Fetch(f);
            const SunLightModulation applied = sunLight ? SanitizeSunLightModulation(*sunLight) : SunLightModulation{};
            m_applied.For(f.Scope()) = SunLightModulationSnapshot{ applied, f.FrameIndex() };
            const std::optional<SunShadowReceiver> receiver = m_receiver.Fetch(f);
            f.SetSceneUniforms(PackSceneLightingUniformData(ResolveActiveDirectionalLight(f.Scene()), m_settings,
                f.View().eye, receiver ? &*receiver : nullptr, &applied));
        },
        fx::Gate::Always());
}

GTE_REGISTER_RENDER_FEATURE("SceneLighting", SceneLightingFeature);

} // namespace gte
