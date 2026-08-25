#include "EntityInstantiator.h"

#include "../../ECS/Components/MeshAssetSource.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/Name.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/TransformHierarchy.h"

namespace gte {

Entity Instantiate(Registry& registry, const EntityBlueprint& blueprint, Entity parent)
{
    const Entity entity = registry.CreateEntity();

    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.position = blueprint.localTransform.position;
    transform.rotation = blueprint.localTransform.rotation;
    transform.scale = blueprint.localTransform.scale;

    if (blueprint.mesh.IsValid()) {
        registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ blueprint.mesh, blueprint.pipeline, blueprint.texture });
    }

    if (!blueprint.name.empty()) {
        registry.AddComponent<Name>(entity, Name{ blueprint.name });
    }

    if (!blueprint.meshAssetSourcePath.empty()) {
        registry.AddComponent<MeshAssetSource>(entity, MeshAssetSource{ blueprint.meshAssetSourcePath });
    }

    if (parent != kInvalidEntity) {
        // worldPositionStays=true mirrors the pre-refactor behavior this
        // replaces - a no-op in practice for a freshly-spawned identity-
        // transform child, but still the semantically correct call (a real
        // attach, not just field assignment), and what also appends this
        // entity as `parent`'s next sibling-ordered child.
        SetParent(registry, entity, parent, /*worldPositionStays=*/true);
    }

    for (const EntityBlueprintNode& child : blueprint.children) {
        Instantiate(registry, child, entity);
    }

    return entity;
}

} // namespace gte
