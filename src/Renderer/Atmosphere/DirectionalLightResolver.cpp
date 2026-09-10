#include "DirectionalLightResolver.h"

#include "../../ECS/Components/DirectionalLight.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/TransformHierarchy.h"

namespace gte {

namespace {

// The lux value DirectionalLight::illuminanceLux's own default (100,000 -
// see DirectionalLight.h) maps to, and kReferenceShaderIlluminanceScale is
// the overall shader-space intensity that reference value produces - both
// chosen so a freshly-spawned, default-valued DirectionalLight entity (see
// Game::CreateDirectionalLightEntity()) reproduces the exact same OVERALL
// magnitude as Phase 5's original hardcoded placeholder
// ((1,0.95,0.85) * 3.0), just with this component's own default color
// (Vec3::One(), i.e. neutral white) instead of that placeholder's warm
// tint - a deliberate, documented difference now that a real entity (with
// its own user-editable color) drives this instead of a fixed constant.
constexpr float kReferenceIlluminanceLux = 100000.0f;
constexpr float kReferenceShaderIlluminanceScale = 3.0f;

} // namespace

ResolvedDirectionalLight ResolveActiveDirectionalLight(Registry& registry)
{
    ComponentStorage<DirectionalLight>& lights = registry.Storage<DirectionalLight>();

    for (std::size_t i = 0; i < lights.Size(); ++i) {
        const DirectionalLight& light = lights.ComponentAt(i);
        if (!light.active) {
            continue;
        }

        const Entity entity = lights.EntityAt(i);

        // ComputeWorldTransform() (ECS/TransformHierarchy.h) resolves this
        // Sun entity's Transform through its whole parent chain first -
        // a DirectionalLight parented under a moving entity genuinely
        // follows it, matching Camera's own identical convention (see
        // RenderSystem::ResolveActiveCameraViewProjection()). Falls back to
        // an identity Transform (origin, no rotation - i.e. Forward())
        // when this entity has no Transform of its own at all.
        Transform transform;
        if (registry.TryGetComponent<Transform>(entity) != nullptr) {
            transform = ComputeWorldTransform(registry, entity);
        }

        const Vec3 forward = transform.rotation.RotateVector(Vec3::Forward());

        ResolvedDirectionalLight resolved;
        // The entity's forward vector is the direction the light SHINES
        // IN; AtmosphereFrameUniforms::sunDirection wants the direction
        // TOWARD the sun instead - the negation (see DirectionalLight.h's
        // own doc comment for the full reasoning).
        resolved.directionTowardSun = Normalize(-forward);
        resolved.sunIlluminance =
            light.color * (light.illuminanceLux / kReferenceIlluminanceLux) * kReferenceShaderIlluminanceScale;
        return resolved;
    }

    // No active DirectionalLight anywhere in the Registry - reproduce
    // Phase 5's exact original hardcoded placeholder verbatim (see
    // AtmosphereLutRenderer.cpp's old ResolveAtmosphereFrameUniforms()
    // body, now replaced by a call into this function).
    ResolvedDirectionalLight fallback;
    fallback.directionTowardSun = Normalize(Vec3(0.70710678f, 0.70710678f, 0.0f));
    fallback.sunIlluminance = Vec3(1.0f, 0.95f, 0.85f) * 3.0f;
    return fallback;
}

} // namespace gte
