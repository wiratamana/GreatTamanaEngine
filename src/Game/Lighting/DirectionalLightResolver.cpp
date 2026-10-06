#include "DirectionalLightResolver.h"

#include "../../ECS/Components/DirectionalLight.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/TransformHierarchy.h"

namespace gte {

namespace {
// 100,000 lux matches the engine's original hardcoded placeholder sun.
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
        Transform transform;
        if (registry.TryGetComponent<Transform>(entity) != nullptr) {
            transform = ComputeWorldTransform(registry, entity);
        }

        const Vec3 forward = transform.rotation.RotateVector(Vec3::Forward());

        ResolvedDirectionalLight resolved;
        resolved.directionTowardSun = Normalize(-forward);
        resolved.sunIlluminance =
            light.color * (light.illuminanceLux / kReferenceIlluminanceLux) * kReferenceShaderIlluminanceScale;
        return resolved;
    }

    // No active DirectionalLight - original placeholder sun.
    ResolvedDirectionalLight fallback;
    fallback.directionTowardSun = Normalize(Vec3(0.70710678f, 0.70710678f, 0.0f));
    fallback.sunIlluminance = Vec3(1.0f, 0.95f, 0.85f) * 3.0f;
    return fallback;
}

} // namespace gte
