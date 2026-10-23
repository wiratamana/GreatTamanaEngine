#include "SceneCameraFrustum.h"

#include "EditorContext.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Registry.h"
#include "../ECS/TransformHierarchy.h"

namespace gte {

float GameViewAspect(const EditorContext& ctx)
{
    const Extent2D extent = ctx.desiredExtent;
    return (extent.width > 0 && extent.height > 0) ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                                    : 16.0f / 9.0f;
}

FrustumInput MakeSceneCameraFrustum(Registry& registry, Entity entity, float aspect)
{
    FrustumInput input;
    const Camera* const lens = registry.TryGetComponent<Camera>(entity);
    if (lens == nullptr) {
        input.fovYDegrees = 0.0f; // fails FrustumInputIsValid
        return input;
    }
    const Transform world = registry.HasComponent<Transform>(entity) ? ComputeWorldTransform(registry, entity) : Transform{};
    input.position = world.position;
    input.rotation = world.rotation;
    input.fovYDegrees = lens->fovYDegrees;
    input.nearZ = lens->nearZ;
    input.farZ = lens->farZ;
    input.aspect = aspect;
    return input;
}

} // namespace gte
